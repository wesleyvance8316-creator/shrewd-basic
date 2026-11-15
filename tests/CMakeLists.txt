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

# Log module tests (two translation units: shared module DEFINE/DECLARE).
add_executable(shrewd-basic-log-tests test_log.c test_log_ext.c)

target_link_libraries(shrewd-basic-log-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# Reflect module tests (two translation units: descriptor sharing).
add_executable(shrewd-basic-reflect-tests test_reflect.c test_reflect_ext.c)

target_link_libraries(shrewd-basic-reflect-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# JSON module tests (value model, parser, serializer).
add_executable(shrewd-basic-json-tests test_json.c)

target_link_libraries(shrewd-basic-json-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# JSON struct mapping tests (encode/decode/free over reflect metadata).
add_executable(shrewd-basic-json-map-tests test_json_map.c)

target_link_libraries(shrewd-basic-json-map-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# Test binary returns non-zero on failure, so a plain COMMAND run is enough.
add_test(NAME shrewd-basic.unit COMMAND shrewd-basic-tests)
add_test(NAME shrewd-basic.ds COMMAND shrewd-basic-ds-tests)
add_test(NAME shrewd-basic.log COMMAND shrewd-basic-log-tests)
add_test(NAME shrewd-basic.reflect COMMAND shrewd-basic-reflect-tests)
add_test(NAME shrewd-basic.json COMMAND shrewd-basic-json-tests)
add_test(NAME shrewd-basic.json-map COMMAND shrewd-basic-json-map-tests)
