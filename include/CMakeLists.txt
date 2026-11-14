# Public headers live in shrewd-basic/ and shrewd-basic-internal/. Build
# information and compile-time configuration are managed in
# shrewd-basic/config/, which emits the generated build.h and config.h next to
# their templates.
add_subdirectory(shrewd-basic/config)
