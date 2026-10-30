# Generates include/shrewd-basic/build.h at configure time.
# Included from the top-level CMakeLists.txt.

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
        ${PROJECT_SOURCE_DIR}/cmake/build.h.in
        ${PROJECT_SOURCE_DIR}/include/shrewd-basic/build.h
        @ONLY)
