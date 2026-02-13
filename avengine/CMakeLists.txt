cmake_minimum_required(VERSION 3.20)
project(avengine LANGUAGES CXX)

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

# Optional warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(avengine PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(vector_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (BUILD_TESTING)
  find_package(doctest CONFIG QUIET)
  if (doctest_FOUND)
    add_executable(avengine_tests tests/utf8_smoke_tests.cpp)
    target_link_libraries(avengine_tests PRIVATE avengine doctest::doctest)
    target_include_directories(avengine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_test(NAME avengine_tests COMMAND avengine_tests)
  else()
    message(WARNING "BUILD_TESTING=ON but doctest was not found; tests will not be built.")
  endif()
endif()
