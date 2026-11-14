add_executable(shrewd-basic-tests test_shrewd_basic.c)

target_link_libraries(shrewd-basic-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# Data-structure component tests.
add_executable(shrewd-basic-ds-tests test_ds.c)

target_link_libraries(shrewd-basic-ds-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# Test binary returns non-zero on failure, so a plain COMMAND run is enough.
add_test(NAME shrewd-basic.unit COMMAND shrewd-basic-tests)
add_test(NAME shrewd-basic.ds COMMAND shrewd-basic-ds-tests)
