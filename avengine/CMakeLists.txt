cmake_minimum_required(VERSION 3.20)
project(avengine LANGUAGES CXX)

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
  src/expression.cpp
  src/HashBlob.cpp
  src/HashTable.cpp
  src/HtmlParser.cpp
  src/HtmlTags.cpp
  src/index.cpp
  src/parser.cpp
  src/tokenstream.cpp
  src/Utf8.cpp
)

target_include_directories(avengine
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

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

# Optional warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(avengine PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(vector_demo PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(index_reader PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(probing_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (BUILD_TESTING)
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  find_package(doctest CONFIG QUIET)
  if (NOT doctest_FOUND)
    message(WARNING "doctest not found; doctest-based tests will be skipped.")
  endif()

  foreach(test_src IN LISTS TEST_SOURCES)
    get_filename_component(test_name "${test_src}" NAME_WE)

    if (test_name MATCHES "doctest|smoke|index_test" AND NOT doctest_FOUND)
      message(STATUS "Skipping ${test_name}: doctest not found")
      continue()
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
