cmake_minimum_required(VERSION 3.20)

project(shrewd-basic
        VERSION 0.1.0
        DESCRIPTION "shrewd-basic C library"
        HOMEPAGE_URL "https://example.invalid/shrewd-basic"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Support being built as a subproject of a larger build (add_subdirectory /
# FetchContent): everything that belongs to a standalone build - the build
# type default, tests, programs and install rules - stays out of the parent
# project's way. As a subproject only the library targets are built.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SHREWD_BASIC_IS_TOP_LEVEL ON)
else()
    set(SHREWD_BASIC_IS_TOP_LEVEL OFF)
endif()

if(SHREWD_BASIC_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            Debug Release RelWithDebInfo MinSizeRel)
endif()

option(SHREWD_BASIC_BUILD_PROGRAMS "Build the command-line programs" ${SHREWD_BASIC_IS_TOP_LEVEL})
option(SHREWD_BASIC_BUILD_TESTS "Build the tests" ${SHREWD_BASIC_IS_TOP_LEVEL})
option(SHREWD_BASIC_INSTALL "Generate install rules" ${SHREWD_BASIC_IS_TOP_LEVEL})
option(SHREWD_BASIC_BUILD_STATIC "Build the static library (libshrewd-basic.a)" ON)
option(SHREWD_BASIC_BUILD_SHARED "Build the shared library (libshrewd-basic.so)" OFF)

# Data-format serialization applications (the formats/ layer). Core -
# reflect, log, platform, ds - is always built; enable exactly the
# formats the consuming project needs. Future formats (yaml, xml, ini,
# csv) get one option each here.
option(SHREWD_BASIC_FORMAT_JSON "Build the json format (formats/json.h)" ON)
option(SHREWD_BASIC_FORMAT_BIN "Build the bin format (formats/bin.h)" ON)
option(SHREWD_BASIC_FORMAT_INI "Build the ini format (formats/ini.h)" ON)
option(SHREWD_BASIC_FORMAT_ENV "Build the env format (formats/env.h)" ON)
option(SHREWD_BASIC_FORMAT_CSV "Build the csv format (formats/csv.h)" ON)
option(SHREWD_BASIC_FORMAT_TOML "Build the toml format (formats/toml.h)" ON)
option(SHREWD_BASIC_FORMAT_MSGPACK "Build the msgpack format (formats/msgpack.h)" ON)
option(SHREWD_BASIC_FORMAT_CBOR "Build the cbor format (formats/cbor.h)" ON)
option(SHREWD_BASIC_DBUS "Build the sb_dbus data bus (dbus/dbus.h)" ON)
if(SHREWD_BASIC_DBUS)
    find_package(Threads REQUIRED)
endif()

# Warnings applied to our own targets only (link against this INTERFACE target).
add_library(shrewd-basic-warnings INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(shrewd-basic-warnings INTERFACE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
    target_compile_options(shrewd-basic-warnings INTERFACE /W4
            /D_CRT_SECURE_NO_WARNINGS)
endif()

enable_testing()

# Build information and configuration options, emitted into shrewd-basic/
# build.h and shrewd-basic/config.h.
add_subdirectory(include)

add_subdirectory(library)

if(SHREWD_BASIC_BUILD_PROGRAMS)
    add_subdirectory(programs)
endif()

if(SHREWD_BASIC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Optional HTML documentation (docs/*.md + the public headers).
find_package(Doxygen QUIET)
if(Doxygen_FOUND)
    add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/doxygen/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Generating HTML documentation with Doxygen"
            VERBATIM)
endif()
