cmake_minimum_required(VERSION 3.10)
project(substitute C)

add_library(substitute STATIC transform-dis.c)
target_include_directories(substitute PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# MSVC needs C11 mode for compound literals and designated initializers
if(MSVC)
    target_compile_options(substitute PRIVATE /std:c17)
endif()
