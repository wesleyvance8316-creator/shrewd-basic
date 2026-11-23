# Core tests (always built).
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

# Reactive primitives tests (ref/effect/computed/watch dependency graph).
add_executable(shrewd-basic-react-tests test_react.c)

target_link_libraries(shrewd-basic-react-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# --- Data-format tests: built only for the enabled formats. ---

if(SHREWD_BASIC_FORMAT_JSON)

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

# JSON policy tests (aliases, flags, ranges, custom ops, union styles).
add_executable(shrewd-basic-json-policy-tests test_json_policy.c)

target_link_libraries(shrewd-basic-json-policy-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

endif()

if(SHREWD_BASIC_FORMAT_BIN)

# Bin format tests (binary struct mapping over reflect metadata).
add_executable(shrewd-basic-bin-tests test_bin.c)

target_link_libraries(shrewd-basic-bin-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

endif()

if(SHREWD_BASIC_FORMAT_INI)

# INI format tests (sections/keys through the shared text layer).
add_executable(shrewd-basic-ini-tests test_ini.c)

target_link_libraries(shrewd-basic-ini-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.ini COMMAND shrewd-basic-ini-tests)

endif()

if(SHREWD_BASIC_FORMAT_ENV)

# env/args tests (twelve-factor configuration mapping).
add_executable(shrewd-basic-env-tests test_env.c)

target_link_libraries(shrewd-basic-env-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.env COMMAND shrewd-basic-env-tests)

endif()

if(SHREWD_BASIC_FORMAT_CSV)

# CSV format tests (RFC 4180 rows through the shared text layer).
add_executable(shrewd-basic-csv-tests test_csv.c)

target_link_libraries(shrewd-basic-csv-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.csv COMMAND shrewd-basic-csv-tests)

endif()

if(SHREWD_BASIC_FORMAT_TOML)

# TOML format tests (TOML 1.0 subset through the shared text layer).
add_executable(shrewd-basic-toml-tests test_toml.c)

target_link_libraries(shrewd-basic-toml-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.toml COMMAND shrewd-basic-toml-tests)

endif()

if(SHREWD_BASIC_FORMAT_MSGPACK)

# MessagePack format tests (binary map/array mapping).
add_executable(shrewd-basic-msgpack-tests test_msgpack.c)

target_link_libraries(shrewd-basic-msgpack-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.msgpack COMMAND shrewd-basic-msgpack-tests)

endif()

if(SHREWD_BASIC_FORMAT_CBOR)

# CBOR format tests (RFC 8949 subset over the shared binary skeleton).
add_executable(shrewd-basic-cbor-tests test_cbor.c)

target_link_libraries(shrewd-basic-cbor-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.cbor COMMAND shrewd-basic-cbor-tests)

endif()

if(SHREWD_BASIC_FORMAT_XML)

# XML format tests (strict subset, XXE-safe parser).
add_executable(shrewd-basic-xml-tests test_xml.c)

target_link_libraries(shrewd-basic-xml-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.xml COMMAND shrewd-basic-xml-tests)

endif()

if(SHREWD_BASIC_FORMAT_BSON)

# BSON format tests (little-endian wire, length backfill).
add_executable(shrewd-basic-bson-tests test_bson.c)

target_link_libraries(shrewd-basic-bson-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.bson COMMAND shrewd-basic-bson-tests)

endif()

if(SHREWD_BASIC_DBUS)

# sb_dbus core flow tests (mount/get/peek/set/touch, hooks, notify).
add_executable(shrewd-basic-dbus-tests test_dbus.c)

target_link_libraries(shrewd-basic-dbus-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.dbus COMMAND shrewd-basic-dbus-tests)

# sb_dbus guard/policy hook builders (phase 3).
add_executable(shrewd-basic-dbus-ops-tests test_dbus_ops.c)

target_link_libraries(shrewd-basic-dbus-ops-tests
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

add_test(NAME shrewd-basic.dbus-ops COMMAND shrewd-basic-dbus-ops-tests)

if(NOT WIN32)
    # sb_dbus shm transport (phase 2): needs shm_open + robust mutex.
    add_executable(shrewd-basic-dbus-shm-tests test_dbus_shm.c)

    target_link_libraries(shrewd-basic-dbus-shm-tests
            PRIVATE
                shrewd-basic
                shrewd-basic-warnings)

    add_test(NAME shrewd-basic.dbus-shm COMMAND shrewd-basic-dbus-shm-tests)
endif()

endif()

# Test binary returns non-zero on failure, so a plain COMMAND run is enough.
add_test(NAME shrewd-basic.unit COMMAND shrewd-basic-tests)
add_test(NAME shrewd-basic.ds COMMAND shrewd-basic-ds-tests)
add_test(NAME shrewd-basic.log COMMAND shrewd-basic-log-tests)
add_test(NAME shrewd-basic.reflect COMMAND shrewd-basic-reflect-tests)
add_test(NAME shrewd-basic.react COMMAND shrewd-basic-react-tests)

if(SHREWD_BASIC_FORMAT_JSON)
add_test(NAME shrewd-basic.json COMMAND shrewd-basic-json-tests)
add_test(NAME shrewd-basic.json-map COMMAND shrewd-basic-json-map-tests)
add_test(NAME shrewd-basic.json-policy COMMAND shrewd-basic-json-policy-tests)
endif()

if(SHREWD_BASIC_FORMAT_BIN)
add_test(NAME shrewd-basic.bin COMMAND shrewd-basic-bin-tests)
endif()

# --- Generator dogfood: generate stubs at build time (lex backend,
# python3 only) into the build directory and smoke-test them.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/generated.h ${CMAKE_CURRENT_BINARY_DIR}/gen/generated.c
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/reflect_gen.py
                ${PROJECT_SOURCE_DIR}/tools/reflect_gen_sample.h
                -o ${CMAKE_CURRENT_BINARY_DIR}/gen/generated --quiet
        DEPENDS ${PROJECT_SOURCE_DIR}/tools/reflect_gen.py
                ${PROJECT_SOURCE_DIR}/tools/reflect_gen_sample.h
        COMMENT "reflect_gen: sample stubs")
    add_executable(shrewd-basic-gen-tests
            ${PROJECT_SOURCE_DIR}/tools/reflect_gen_smoke.c
            ${CMAKE_CURRENT_BINARY_DIR}/gen/generated.c)
    target_include_directories(shrewd-basic-gen-tests
            PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen ${PROJECT_SOURCE_DIR}/tools)
    target_link_libraries(shrewd-basic-gen-tests
            PRIVATE
                shrewd-basic
                shrewd-basic-warnings)
    add_test(NAME shrewd-basic.gen COMMAND shrewd-basic-gen-tests)

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen_ann/generated.h
               ${CMAKE_CURRENT_BINARY_DIR}/gen_ann/generated.c
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen_ann
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/reflect_gen.py
                ${PROJECT_SOURCE_DIR}/tools/reflect_gen_sample_ann.h
                -o ${CMAKE_CURRENT_BINARY_DIR}/gen_ann/generated --quiet
        DEPENDS ${PROJECT_SOURCE_DIR}/tools/reflect_gen.py
                ${PROJECT_SOURCE_DIR}/tools/reflect_gen_sample_ann.h
        COMMENT "reflect_gen: annotation stubs")
    add_executable(shrewd-basic-gen-ann-tests
            ${PROJECT_SOURCE_DIR}/tools/reflect_gen_ann_smoke.c
            ${CMAKE_CURRENT_BINARY_DIR}/gen_ann/generated.c)
    target_include_directories(shrewd-basic-gen-ann-tests
            PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen_ann ${PROJECT_SOURCE_DIR}/tools)
    target_link_libraries(shrewd-basic-gen-ann-tests
            PRIVATE
                shrewd-basic
                shrewd-basic-warnings)
    add_test(NAME shrewd-basic.gen-ann COMMAND shrewd-basic-gen-ann-tests)

    # json policy stubs (same lex backend); the smoke exercises the
    # policy runtime, so it rides with the json format option.
    if(SHREWD_BASIC_FORMAT_JSON)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen_json/generated.h
                   ${CMAKE_CURRENT_BINARY_DIR}/gen_json/generated.c
                   ${CMAKE_CURRENT_BINARY_DIR}/gen_json/generated_json.h
                   ${CMAKE_CURRENT_BINARY_DIR}/gen_json/generated_json.c
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen_json
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/reflect_gen.py
                    ${PROJECT_SOURCE_DIR}/tools/reflect_gen_sample_json.h
                    -o ${CMAKE_CURRENT_BINARY_DIR}/gen_json/generated --quiet
            DEPENDS ${PROJECT_SOURCE_DIR}/tools/reflect_gen.py
                    ${PROJECT_SOURCE_DIR}/tools/reflect_gen_sample_json.h
            COMMENT "reflect_gen: json policy stubs")
        add_executable(shrewd-basic-gen-json-tests
                ${PROJECT_SOURCE_DIR}/tools/reflect_gen_json_smoke.c
                ${CMAKE_CURRENT_BINARY_DIR}/gen_json/generated.c
                ${CMAKE_CURRENT_BINARY_DIR}/gen_json/generated_json.c)
        target_include_directories(shrewd-basic-gen-json-tests
                PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen_json ${PROJECT_SOURCE_DIR}/tools)
        target_link_libraries(shrewd-basic-gen-json-tests
                PRIVATE
                    shrewd-basic
                    shrewd-basic-warnings)
        add_test(NAME shrewd-basic.gen-json COMMAND shrewd-basic-gen-json-tests)
    endif()

    # Full generator self-test (dwarf backend, golden, layout). Skips
    # with a loud message when python3-pyelftools is absent (exit 77).
    if(NOT WIN32)
        add_test(NAME shrewd-basic.gen-check
                 COMMAND ${PROJECT_SOURCE_DIR}/tools/reflect_gen_check.sh)
        set_tests_properties(shrewd-basic.gen-check PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
