# Build information and compile-time configuration options for the library.
# Included from include/CMakeLists.txt.
#
# The build block stamps build.h with the version and host/toolchain; declare an
# option in the config block and reference it from the template in config.h.in.
# Templates sit next to the headers they generate, which are included as
# <shrewd-basic/config/build.h> and <shrewd-basic/config/config.h>.
# configure_file calls below run at configure time and write build.h and
# config.h next to their templates.

# --- Build information, emitted into build.h. ---

string(TIMESTAMP BUILD_EPOCH "%s")
string(TIMESTAMP _build_time "%Y-%m-%d %H:%M:%S")

if(NOT BUILD_EPOCH MATCHES "^[0-9]+$")
    message(FATAL_ERROR "string(TIMESTAMP) does not support %s on this platform")
endif()

# Three-segment version taken verbatim from project(), e.g. "v0.1.0 2026-10-30 23:15:00".
set(BUILD_VERSION_STRING "v${PROJECT_VERSION} ${_build_time}")

# Build host and toolchain, e.g. "Linux x86_64 ubuntu.pve / GNU 15.2.0".
cmake_host_system_information(RESULT _host_name QUERY HOSTNAME)
cmake_host_system_information(RESULT _host_os QUERY OS_NAME)
cmake_host_system_information(RESULT _host_platform QUERY OS_PLATFORM)
if(NOT _host_name)
    set(_host_name "unknown")
endif()

set(_toolchain "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
if(CMAKE_C_COMPILER_TARGET)
    string(APPEND _toolchain " (${CMAKE_C_COMPILER_TARGET})")
endif()
string(STRIP "${_toolchain}" _toolchain)

set(BUILD_ENV_STRING "${_host_os} ${_host_platform} ${_host_name} / ${_toolchain}")

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/build.h.in
        ${CMAKE_CURRENT_SOURCE_DIR}/build.h
        @ONLY)

# --- Compile-time configuration options, emitted into config.h. ---

option(SHREWD_BASIC_ENABLE_ASSERTS "Enable internal consistency checks" OFF)

set(SHREWD_BASIC_LOG_LEVEL "1" CACHE STRING
        "Log verbosity: 0=off 1=error 2=warning 3=info 4=debug")
set_property(CACHE SHREWD_BASIC_LOG_LEVEL PROPERTY STRINGS 0 1 2 3 4)

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
        ${CMAKE_CURRENT_SOURCE_DIR}/config.h
        @ONLY)
