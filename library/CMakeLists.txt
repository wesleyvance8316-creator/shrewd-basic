include(GNUInstallDirs)

# Library sources: every .c below library/ - core code in src/,
# data-structure components in ds/, the log module in log/, platform
# wrappers in platform/. CONFIGURE_DEPENDS re-runs the glob when files
# are added or removed, so no manual source list is needed.
# Internal headers live in include/shrewd-basic-internal and are only visible
# to this target; public headers are in include/shrewd-basic.
file(GLOB_RECURSE SHREWD_BASIC_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

if(SHREWD_BASIC_BUILD_SHARED)
    add_library(shrewd-basic SHARED ${SHREWD_BASIC_SOURCES})
else()
    add_library(shrewd-basic STATIC ${SHREWD_BASIC_SOURCES})
endif()

add_library(shrewd-basic::shrewd-basic ALIAS shrewd-basic)

target_include_directories(shrewd-basic
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include/shrewd-basic-internal)

target_link_libraries(shrewd-basic PRIVATE shrewd-basic-warnings)

set_target_properties(shrewd-basic PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        WINDOWS_EXPORT_ALL_SYMBOLS ON)

install(TARGETS shrewd-basic
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Public headers, including the generated config/build.h and config/config.h.
# The *.in templates and the config/ CMake script are not installed.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/shrewd-basic
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.in" EXCLUDE
        PATTERN "CMakeLists.txt" EXCLUDE)
