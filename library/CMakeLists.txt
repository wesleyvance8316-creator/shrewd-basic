include(GNUInstallDirs)

# Library sources: every .c below library/ - core code in src/,
# data-structure components in ds/, the log module in log/, platform
# wrappers in platform/. CONFIGURE_DEPENDS re-runs the glob when files
# are added or removed, so no manual source list is needed.
# Internal headers live in include/shrewd-basic-internal and are only visible
# to this target; public headers are in include/shrewd-basic.
file(GLOB_RECURSE SHREWD_BASIC_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

if(NOT SHREWD_BASIC_BUILD_STATIC AND NOT SHREWD_BASIC_BUILD_SHARED)
    message(FATAL_ERROR
            "Enable SHREWD_BASIC_BUILD_STATIC and/or SHREWD_BASIC_BUILD_SHARED")
endif()

# Both variants compile the same sources with the same configuration.
function(shrewd_basic_setup_library target)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include/shrewd-basic-internal)
    target_link_libraries(${target} PRIVATE shrewd-basic-warnings)
    set_target_properties(${target} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
            WINDOWS_EXPORT_ALL_SYMBOLS ON)
    install(TARGETS ${target}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()

if(SHREWD_BASIC_BUILD_STATIC)
    add_library(shrewd-basic-static STATIC ${SHREWD_BASIC_SOURCES})
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
