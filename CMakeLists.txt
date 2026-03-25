cmake_minimum_required(VERSION 3.20)
project(avengine LANGUAGES CXX)

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

include(CTest)
enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Needed for clang-tidy / tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

add_library(avengine
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
  src/html_parser/HtmlParser.cpp
  src/html_parser/HtmlTags.cpp
  src/index.cpp
  src/index_stream_reader.cpp
  src/parser.cpp
  src/ranker/ranker.cpp
  src/tokenstream.cpp
  src/Utf8.cpp
  src/crawler/url_normalizer.cpp
  src/crawler/domain_filter.cpp
  src/crawler/fetcher.cpp
)

target_include_directories(avengine
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(avengine PUBLIC OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB)

# Example for usage
# NOTE: not filled yet unsure end compilation
add_executable(vector_demo examples/vector_demo.cpp)
target_link_libraries(vector_demo PRIVATE avengine)

# Simple tool for building index from text files.
add_executable(index_builder src/index_builder.cpp)
target_link_libraries(index_builder PRIVATE avengine)
target_include_directories(index_builder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Minimal index inspection tool.
add_executable(index_reader src/index_reader.cpp)
target_include_directories(index_reader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Helper tool for comparing probing strategies at different load factors.
add_executable(probing_benchmark src/probing_benchmark.cpp)
target_include_directories(probing_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Crawler entrypoint executable.
add_executable(crawler_main src/crawler/crawler_main.cpp)
target_link_libraries(crawler_main PRIVATE avengine)
target_include_directories(crawler_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Optional warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(avengine PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(vector_demo PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(index_reader PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(probing_benchmark PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(crawler_main PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (BUILD_TESTING)
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  find_package(doctest CONFIG QUIET)
  if (NOT doctest_FOUND)
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
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    if (doctest_FOUND)
      target_link_libraries(${test_name} PRIVATE doctest::doctest)
    endif()

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
