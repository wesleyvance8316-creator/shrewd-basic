# Compile-time configuration options for the library.
#
# Declare an option here and reference it from the template in
# shrewd-basic/config.h.in; both are kept in this tree so the public
# configuration surface is managed in one place. configure_file below runs at
# configure time and writes shrewd-basic/config.h next to its template.

option(SHREWD_BASIC_ENABLE_ASSERTS "Enable internal consistency checks" OFF)

set(SHREWD_BASIC_LOG_LEVEL "1" CACHE STRING
        "Log verbosity: 0=off 1=error 2=warning 3=info 4=debug")
set_property(CACHE SHREWD_BASIC_LOG_LEVEL PROPERTY STRINGS 0 1 2 3 4)

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/shrewd-basic/config.h.in
        ${CMAKE_CURRENT_SOURCE_DIR}/shrewd-basic/config.h
        @ONLY)
