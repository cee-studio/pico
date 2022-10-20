cmake_minimum_required(VERSION 3.0.2)

project(pico C)

set(CMAKE_C_STANDARD 99)

set(PROJECT_DESCRIPTION
        "Cross-platform library which helps to develop web servers or frameworks.")
set(PROJECT_VENDOR "TinyCode.Club")
set(PROJECT_URL "https://github.com/tinycode-club")
set(PROJECT_ISSUES_URL "${PROJECT_URL}/issues")
set(SG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

set(CMAKE_BUILD_TYPE Debug)
set(BUILD_SHARED_LIBS OFF)

set(CMAKE_FIND_LIBRARY_SUFFIXES  ".a")
set(CMAKE_EXE_LINKER_FLAGS "-static")
set(CMAKE_C_FLAGS "-m32")


add_library(pico httpd.h httpd.c picohttpparser.c picohttpparser.h)

add_executable(server main.c)
target_link_libraries(server pico)