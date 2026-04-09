cmake_minimum_required(VERSION 3.20)
project(avengine LANGUAGES CXX)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

include(CTest)
enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_PROFILING "Build with gprof instrumentation (-pg -fno-omit-frame-pointer -O2)" OFF)
if(ENABLE_PROFILING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(PROFILING_FLAGS -pg -fno-omit-frame-pointer -O2 -DNDEBUG)
  message(STATUS "CPU profiling ON: ${PROFILING_FLAGS}")
endif()

# Needed for clang-tidy / tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

add_library(
  avengine
  src/codec.cpp
  src/Common.cpp
  src/constraint_solver/constraint_solver.cpp
  src/constraint_solver/isr_tree.cpp
  src/constraint_solver/query_ast.cpp
  src/constraint_solver/query_lexer.cpp
  src/constraint_solver/query_parser.cpp
  src/expression.cpp
  src/HashBlob.cpp
  src/HashTable.cpp
  src/crawler/frontier.cpp
  src/crawler/inbound_url_queue.cpp
  src/crawler/logger.cpp
  src/crawler/RobotsTxt.cpp
  src/crawler/robots_helper.cpp
  src/html_parser/HtmlParser.cpp
  src/html_parser/HtmlTags.cpp
  src/indexer/indexer.cpp
  src/indexer/anchor_indexer.cpp
  src/index_stream_reader.cpp
  src/ranker/ranker.cpp
  src/search/index_shard_source.cpp
  src/search/search_service.cpp
  src/tokenstream.cpp
  src/Utf8.cpp
  src/crawler/url_normalizer.cpp
  src/crawler/domain_filter.cpp
  src/crawler/fetcher.cpp
  src/node.cpp
  src/gradient_descent/search_logger.cpp
  src/gradient_descent/weight_tuner.cpp)

target_include_directories(avengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(avengine PUBLIC OpenSSL::SSL OpenSSL::Crypto
                                      Threads::Threads ZLIB::ZLIB)

if(ENABLE_PROFILING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(avengine PRIVATE ${PROFILING_FLAGS})
  target_link_options(avengine PRIVATE -pg)
endif()

# Simple tool for building index from text files.
add_executable(index_builder src/indexer/indexbuilder.cpp)
target_link_libraries(index_builder PRIVATE avengine)
target_include_directories(index_builder
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Minimal index inspection tool.
add_executable(index_reader src/indexer/indexreader.cpp)
target_link_libraries(index_reader PRIVATE avengine)
target_include_directories(index_reader
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Offline post-crawl anchor-text index builder.
add_executable(anchor_processor src/indexer/anchor_processor.cpp)
target_link_libraries(anchor_processor PRIVATE avengine)
target_include_directories(anchor_processor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Helper tool for comparing probing strategies at different load factors.
add_executable(probing_benchmark src/probing_benchmark.cpp)
target_include_directories(probing_benchmark
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Offline profiling harness for parser/link-normalization/indexing throughput.
add_executable(crawler_index_profile src/crawler_index_profile.cpp)
target_link_libraries(crawler_index_profile PRIVATE avengine)
target_include_directories(crawler_index_profile
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Crawler entrypoint executable.
add_executable(crawler_main src/crawler/crawler_main.cpp)
target_link_libraries(crawler_main PRIVATE avengine)
target_include_directories(crawler_main
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Minimal online search server.
add_executable(search_server src/search_server.cpp)
target_link_libraries(search_server PRIVATE avengine)
target_include_directories(search_server
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone benchmark binary for CPU profiling.
add_executable(search_benchmark src/search_benchmark.cpp)
target_link_libraries(search_benchmark PRIVATE avengine)
target_include_directories(search_benchmark
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Offline weight tuner to update weights.cfg.
add_executable(tune src/gradient_descent/tune.cpp)
target_link_libraries(tune PRIVATE avengine)
target_include_directories(tune PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Optional warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(avengine PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(index_reader PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(anchor_processor PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(probing_benchmark PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(crawler_index_profile PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(crawler_main PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(search_server PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(search_benchmark PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tune PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_PROFILING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(search_benchmark PRIVATE ${PROFILING_FLAGS})
  target_link_options(search_benchmark PRIVATE -pg)
endif()

if(BUILD_TESTING)
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  find_package(doctest CONFIG QUIET)
  if(NOT doctest_FOUND)
    message(WARNING "doctest not found; doctest-based tests will be skipped.")
  endif()

  foreach(test_src IN LISTS TEST_SOURCES)
    get_filename_component(test_name "${test_src}" NAME_WE)

    if(NOT doctest_FOUND)
      file(READ "${test_src}" test_src_contents)
      if(test_src_contents MATCHES "#include <doctest/doctest.h>")
        message(STATUS "Skipping ${test_name}: doctest not found")
        continue()
      endif()
    endif()

    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE avengine)
    target_include_directories(${test_name}
                               PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    if(doctest_FOUND)
      target_link_libraries(${test_name} PRIVATE doctest::doctest)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  find_program(NODE_EXECUTABLE NAMES node nodejs)
  if (NODE_EXECUTABLE)
    add_test(
      NAME frontend_results_test
      COMMAND ${NODE_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_results_test.js"
    )
  else()
    message(STATUS "Skipping frontend_results_test: node runtime not found")
  endif()
endif()
