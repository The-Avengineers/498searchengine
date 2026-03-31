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

# Needed for clang-tidy / tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

add_library(
  avengine
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
  src/crawler/logger.cpp
  src/crawler/RobotsTxt.cpp
  src/crawler/robots_helper.cpp
  src/html_parser/HtmlParser.cpp
  src/html_parser/HtmlTags.cpp
  src/indexer/indexer.cpp
  src/index_stream_reader.cpp
  src/ranker/ranker.cpp
  src/search/index_shard_source.cpp
  src/search/search_service.cpp
  src/tokenstream.cpp
  src/Utf8.cpp
  src/crawler/url_normalizer.cpp
  src/crawler/domain_filter.cpp
  src/crawler/fetcher.cpp)

target_include_directories(avengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(avengine PUBLIC OpenSSL::SSL OpenSSL::Crypto
                                      Threads::Threads ZLIB::ZLIB)

# Simple tool for building index from text files.
add_executable(index_builder src/indexer/indexbuilder.cpp)
target_link_libraries(index_builder PRIVATE avengine)
target_include_directories(index_builder
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Minimal index inspection tool.
add_executable(index_reader src/indexer/indexreader.cpp)
target_include_directories(index_reader
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Helper tool for comparing probing strategies at different load factors.
add_executable(probing_benchmark src/probing_benchmark.cpp)
target_include_directories(probing_benchmark
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

# Optional warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(avengine PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(index_reader PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(probing_benchmark PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(crawler_main PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(search_server PRIVATE -Wall -Wextra -Wpedantic)
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
