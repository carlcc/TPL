cmake_minimum_required(VERSION 3.2)

project(TPL)

file(GLOB_RECURSE TPL_SOURCE include/* src/*)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS -Wall)

add_library(TPL STATIC ${TPL_SOURCE})
target_include_directories(TPL
PUBLIC
    include
)

add_executable(monad example/example1.cpp)
target_link_libraries(monad TPL)