include(GNUInstallDirs)

# Library sources. Internal headers live in include/shrewd-basic-internal and
# are only visible to this target; public headers are in include/shrewd-basic.
add_library(shrewd-basic
        src/shrewd-basic.c
        src/shrewd-basic-internal.c)

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
        SOVERSION ${PROJECT_VERSION_MAJOR})

install(TARGETS shrewd-basic
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Public headers, including the generated build.h and config.h.
# The *.in templates are not installed.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/shrewd-basic
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.in" EXCLUDE)
