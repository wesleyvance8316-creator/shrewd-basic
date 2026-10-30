cmake_minimum_required(VERSION 3.20)

project(shrewd-basic
        VERSION 0.1.0
        DESCRIPTION "shrewd-basic C library"
        HOMEPAGE_URL "https://example.invalid/shrewd-basic"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            Debug Release RelWithDebInfo MinSizeRel)
endif()

option(SHREWD_BASIC_BUILD_PROGRAMS "Build the command-line programs" ON)
option(SHREWD_BASIC_BUILD_TESTS "Build the tests" ON)

# Warnings applied to our own targets only (link against this INTERFACE target).
add_library(shrewd-basic-warnings INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(shrewd-basic-warnings INTERFACE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
    target_compile_options(shrewd-basic-warnings INTERFACE /W4)
endif()

enable_testing()

# Build information, emitted into shrewd-basic/build.h.
include(${PROJECT_SOURCE_DIR}/cmake/BuildHeader.cmake)

# Configuration options, emitted into shrewd-basic/config.h.
add_subdirectory(include)

add_subdirectory(library)

if(SHREWD_BASIC_BUILD_PROGRAMS)
    add_subdirectory(programs)
endif()

if(SHREWD_BASIC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
