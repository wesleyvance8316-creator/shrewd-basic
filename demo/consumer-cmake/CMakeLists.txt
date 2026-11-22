# Minimal shrewd-basic consumer: build descriptors at build time and
# link the library. Usage:
#   cmake -S demo/consumer-cmake -B /tmp/demo-build
#   cmake --build /tmp/demo-build && /tmp/demo-build/config_demo
cmake_minimum_required(VERSION 3.20)
project(shrewd-basic-consumer-cmake LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The library itself. Once installed, the canonical integration is:
#     find_package(shrewd-basic REQUIRED)
#     target_link_libraries(app PRIVATE shrewd-basic::shrewd-basic)
# (this demo uses add_subdirectory to stay self-contained).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. shrewd-basic-build
                 EXCLUDE_FROM_ALL)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Generate the descriptor + json policy stubs into the build tree - the
# source tree stays clean. The annotated header is the only input
# reflect_gen needs (json: annotations add the policy pair).
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/config_reflect.h
               ${CMAKE_CURRENT_BINARY_DIR}/config_reflect.c
               ${CMAKE_CURRENT_BINARY_DIR}/config_reflect_json.h
               ${CMAKE_CURRENT_BINARY_DIR}/config_reflect_json.c
               ${CMAKE_CURRENT_BINARY_DIR}/config_reflect_schema_dump.c
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/reflect_gen.py
                ${CMAKE_CURRENT_SOURCE_DIR}/config.h
                -o ${CMAKE_CURRENT_BINARY_DIR}/config_reflect --quiet --schema-dump
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/reflect_gen.py
                ${CMAKE_CURRENT_SOURCE_DIR}/config.h
        COMMENT "reflect_gen: config descriptors + json policy")

add_executable(config_demo
        main.c
        ${CMAKE_CURRENT_BINARY_DIR}/config_reflect.c
        ${CMAKE_CURRENT_BINARY_DIR}/config_reflect_json.c)
target_include_directories(config_demo
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(config_demo PRIVATE shrewd-basic)

# Schema dump program: run it at build time to fix the schema as a
# file (single schema truth = the runtime exporter).
add_executable(config_schema_dump
        ${CMAKE_CURRENT_BINARY_DIR}/config_reflect_schema_dump.c
        ${CMAKE_CURRENT_BINARY_DIR}/config_reflect.c
        ${CMAKE_CURRENT_BINARY_DIR}/config_reflect_json.c)
target_include_directories(config_schema_dump
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(config_schema_dump PRIVATE shrewd-basic)
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/config_schema.json
        COMMAND config_schema_dump demo_config_t
                ${CMAKE_CURRENT_BINARY_DIR}/config_schema.json
        DEPENDS config_schema_dump
        COMMENT "schema: config_schema.json")
add_custom_target(config_schema ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/config_schema.json)
