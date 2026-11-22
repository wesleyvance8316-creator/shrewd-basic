include(GNUInstallDirs)

# Library sources, one set per component (CONFIGURE_DEPENDS re-runs the
# glob when files are added or removed):
#   core components - ds, log, platform, reflect - always build;
#   data-format applications - bin, json, ... - contribute their sources
#   only while the matching SHREWD_BASIC_FORMAT_* option is on, an empty
#   set otherwise (no glob runs for a disabled format).
# Internal headers live in include/shrewd-basic-internal and are only
# visible to this target; public headers are in include/shrewd-basic.
file(GLOB_RECURSE SHREWD_BASIC_DS_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ds/*.c")
file(GLOB_RECURSE SHREWD_BASIC_LOG_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/log/*.c")
file(GLOB_RECURSE SHREWD_BASIC_PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.c")
file(GLOB_RECURSE SHREWD_BASIC_REFLECT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/reflect/*.c")

set(SHREWD_BASIC_FORMAT_BIN_SOURCES "")
if(SHREWD_BASIC_FORMAT_BIN)
    file(GLOB_RECURSE SHREWD_BASIC_FORMAT_BIN_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/bin/*.c")
endif()

set(SHREWD_BASIC_FORMAT_JSON_SOURCES "")
if(SHREWD_BASIC_FORMAT_JSON)
    file(GLOB_RECURSE SHREWD_BASIC_FORMAT_JSON_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/json/*.c")
endif()

set(SHREWD_BASIC_FORMAT_INI_SOURCES "")
if(SHREWD_BASIC_FORMAT_INI)
    file(GLOB_RECURSE SHREWD_BASIC_FORMAT_INI_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/ini/*.c")
endif()

set(SHREWD_BASIC_FORMAT_ENV_SOURCES "")
if(SHREWD_BASIC_FORMAT_ENV)
    file(GLOB_RECURSE SHREWD_BASIC_FORMAT_ENV_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/env/*.c")
endif()

set(SHREWD_BASIC_FORMAT_CSV_SOURCES "")
if(SHREWD_BASIC_FORMAT_CSV)
    file(GLOB_RECURSE SHREWD_BASIC_FORMAT_CSV_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/csv/*.c")
endif()

set(SHREWD_BASIC_FORMAT_TOML_SOURCES "")
if(SHREWD_BASIC_FORMAT_TOML)
    file(GLOB_RECURSE SHREWD_BASIC_FORMAT_TOML_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/toml/*.c")
endif()

set(SHREWD_BASIC_FORMAT_MSGPACK_SOURCES "")
if(SHREWD_BASIC_FORMAT_MSGPACK)
    file(GLOB_RECURSE SHREWD_BASIC_FORMAT_MSGPACK_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/msgpack/*.c")
endif()

set(SHREWD_BASIC_FORMAT_CBOR_SOURCES "")
if(SHREWD_BASIC_FORMAT_CBOR)
    file(GLOB_RECURSE SHREWD_BASIC_FORMAT_CBOR_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/cbor/*.c")
endif()

set(SHREWD_BASIC_FORMAT_XML_SOURCES "")
if(SHREWD_BASIC_FORMAT_XML)
    file(GLOB_RECURSE SHREWD_BASIC_FORMAT_XML_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/xml/*.c")
endif()

set(SHREWD_BASIC_DBUS_SOURCES "")
set(SHREWD_BASIC_DBUS_LIBS "")
if(SHREWD_BASIC_DBUS)
    file(GLOB_RECURSE SHREWD_BASIC_DBUS_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/dbus/*.c")
    set(SHREWD_BASIC_DBUS_LIBS Threads::Threads)
endif()

set(SHREWD_BASIC_SOURCES
        ${SHREWD_BASIC_DS_SOURCES}
        ${SHREWD_BASIC_LOG_SOURCES}
        ${SHREWD_BASIC_PLATFORM_SOURCES}
        ${SHREWD_BASIC_REFLECT_SOURCES}
        ${SHREWD_BASIC_FORMAT_BIN_SOURCES}
        ${SHREWD_BASIC_FORMAT_JSON_SOURCES}
        ${SHREWD_BASIC_FORMAT_INI_SOURCES}
        ${SHREWD_BASIC_FORMAT_ENV_SOURCES}
        ${SHREWD_BASIC_FORMAT_CSV_SOURCES}
        ${SHREWD_BASIC_FORMAT_TOML_SOURCES}
        ${SHREWD_BASIC_FORMAT_MSGPACK_SOURCES}
        ${SHREWD_BASIC_FORMAT_CBOR_SOURCES}
        ${SHREWD_BASIC_FORMAT_XML_SOURCES}
        ${SHREWD_BASIC_DBUS_SOURCES})

if(NOT SHREWD_BASIC_BUILD_STATIC AND NOT SHREWD_BASIC_BUILD_SHARED)
    message(FATAL_ERROR
            "Enable SHREWD_BASIC_BUILD_STATIC and/or SHREWD_BASIC_BUILD_SHARED")
endif()

# Both variants compile the same sources with the same configuration.
function(shrewd_basic_setup_library target)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include/shrewd-basic-internal)
    target_link_libraries(${target} PRIVATE $<BUILD_INTERFACE:shrewd-basic-warnings>)
    set_target_properties(${target} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
            WINDOWS_EXPORT_ALL_SYMBOLS ON)
    if(SHREWD_BASIC_INSTALL)
        install(TARGETS ${target}
                EXPORT shrewd-basic-targets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endfunction()

if(SHREWD_BASIC_BUILD_STATIC)
    add_library(shrewd-basic-static STATIC ${SHREWD_BASIC_SOURCES})
    target_link_libraries(shrewd-basic-static PUBLIC ${SHREWD_BASIC_DBUS_LIBS})
    shrewd_basic_setup_library(shrewd-basic-static)
    add_library(shrewd-basic::shrewd-basic-static ALIAS shrewd-basic-static)
    set_target_properties(shrewd-basic-static PROPERTIES OUTPUT_NAME "shrewd-basic")
    if(SHREWD_BASIC_BUILD_SHARED AND WIN32)
        # Keep the static archive from colliding with the import library.
        set_target_properties(shrewd-basic-static PROPERTIES
                OUTPUT_NAME "shrewd-basic-static")
    endif()
endif()

if(SHREWD_BASIC_BUILD_SHARED)
    add_library(shrewd-basic-shared SHARED ${SHREWD_BASIC_SOURCES})
    target_link_libraries(shrewd-basic-shared PUBLIC ${SHREWD_BASIC_DBUS_LIBS})
    shrewd_basic_setup_library(shrewd-basic-shared)
    add_library(shrewd-basic::shrewd-basic-shared ALIAS shrewd-basic-shared)
    set_target_properties(shrewd-basic-shared PROPERTIES OUTPUT_NAME "shrewd-basic")
endif()

# The unqualified names resolve to one primary variant so in-tree consumers
# (tests, programs) need no changes: the static library when it is built,
# otherwise the shared one. Link shrewd-basic::shrewd-basic-static or
# shrewd-basic::shrewd-basic-shared to pick a variant explicitly.
if(SHREWD_BASIC_BUILD_STATIC)
    add_library(shrewd-basic ALIAS shrewd-basic-static)
    add_library(shrewd-basic::shrewd-basic ALIAS shrewd-basic-static)
else()
    add_library(shrewd-basic ALIAS shrewd-basic-shared)
    add_library(shrewd-basic::shrewd-basic ALIAS shrewd-basic-shared)
endif()

# Public headers: the shrewd-basic/ tree (templates and the config/ CMake
# script excluded) plus the generated config/build.h and config/config.h
# emitted into the build tree by include/shrewd-basic/config/CMakeLists.txt.
if(SHREWD_BASIC_INSTALL)
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/shrewd-basic
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            PATTERN "*.in" EXCLUDE
            PATTERN "CMakeLists.txt" EXCLUDE)
    install(FILES
            ${PROJECT_BINARY_DIR}/include/shrewd-basic/config/build.h
            ${PROJECT_BINARY_DIR}/include/shrewd-basic/config/config.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/shrewd-basic/config)
endif()

# Package config: find_package(shrewd-basic) support.
if(SHREWD_BASIC_INSTALL)
    include(CMakePackageConfigHelpers)
    install(EXPORT shrewd-basic-targets
            FILE shrewd-basic-targets.cmake
            NAMESPACE shrewd-basic::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shrewd-basic)
    configure_package_config_file(
            ${PROJECT_SOURCE_DIR}/cmake/shrewd-basic-config.cmake.in
            ${CMAKE_CURRENT_BINARY_DIR}/shrewd-basic-config.cmake
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shrewd-basic)
    write_basic_package_version_file(
            ${CMAKE_CURRENT_BINARY_DIR}/shrewd-basic-config-version.cmake
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion)
    install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/shrewd-basic-config.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/shrewd-basic-config-version.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shrewd-basic)
endif()
