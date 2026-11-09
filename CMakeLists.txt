cmake_minimum_required(VERSION 3.14)

project(minitiger_db VERSION 0.3.0 LANGUAGES CXX)



set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)



if(NOT CMAKE_BUILD_TYPE)

    set(CMAKE_BUILD_TYPE Release)

endif()



find_package(Threads REQUIRED)

add_compile_options(-Wall -Wextra -Wpedantic -O3)



# Core MiniTiger Library (Parts 1, 2 & 3: Document Codec + WAL + Striped Storage Engine)

add_library(minitiger_core STATIC

    src/doc/document.cpp

    src/wal/wal.cpp

    src/storage/storage_engine.cpp

)



target_include_directories(minitiger_core PUBLIC

    ${CMAKE_CURRENT_SOURCE_DIR}/include

    ${CMAKE_CURRENT_SOURCE_DIR}/include/minitiger/doc

    ${CMAKE_CURRENT_SOURCE_DIR}/include/minitiger/wal

    ${CMAKE_CURRENT_SOURCE_DIR}/include/minitiger/storage

)

target_link_libraries(minitiger_core PUBLIC Threads::Threads)



# Part 1 Unit Test & Benchmark Executable

add_executable(part1_doc_test tests/part1_doc_test.cpp)

target_link_libraries(part1_doc_test PRIVATE minitiger_core)



# Part 2 Unit Test & Crash Recovery Executable

add_executable(part2_wal_test tests/part2_wal_test.cpp)

target_link_libraries(part2_wal_test PRIVATE minitiger_core)



# Part 3 Unit Test & Multithreaded Concurrency Benchmark Executable

add_executable(part3_storage_test tests/part3_storage_test.cpp)

target_link_libraries(part3_storage_test PRIVATE minitiger_core)

