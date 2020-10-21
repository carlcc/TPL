cmake_minimum_required(VERSION 3.2)

project(TPL)

file(GLOB_RECURSE TPL_SOURCE include/*)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(monad example/tpl.cpp)