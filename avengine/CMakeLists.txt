cmake_minimum_required(VERSION 3.20)
project(avengine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Needed for clang-tidy / tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Optional warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(avengine PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(vector_demo PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(index_reader PRIVATE -Wall -Wextra -Wpedantic)
endif()
