cmake_minimum_required(VERSION 3.14)

project(minitiger_db VERSION 0.1.0 LANGUAGES CXX)



set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)



if(NOT CMAKE_BUILD_TYPE)

    set(CMAKE_BUILD_TYPE Release)

endif()



add_compile_options(-Wall -Wextra -Wpedantic -O3)



# Core MiniTiger Library (Part 1: Document & Binary Serializer)

add_library(minitiger_core STATIC

    src/doc/document.cpp

)



target_include_directories(minitiger_core PUBLIC

    ${CMAKE_CURRENT_SOURCE_DIR}/include

    ${CMAKE_CURRENT_SOURCE_DIR}/include/minitiger/doc

)



# Part 1 Unit Test & Benchmark Executable

add_executable(part1_doc_test

    tests/part1_doc_test.cpp

)

target_link_libraries(part1_doc_test PRIVATE minitiger_core)

