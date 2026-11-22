# Minimal shrewd-basic consumer: build descriptors at build time and
# link the library. Usage:
#   cmake -S demo/consumer-cmake -B /tmp/demo-build
#   cmake --build /tmp/demo-build && /tmp/demo-build/config_demo
cmake_minimum_required(VERSION 3.20)
project(shrewd-basic-consumer-cmake LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The library itself - adapt to your integration (find_package /
# FetchContent) once shrewd-basic is installed system-wide.
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
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/reflect_gen.py
                ${CMAKE_CURRENT_SOURCE_DIR}/config.h
                -o ${CMAKE_CURRENT_BINARY_DIR}/config_reflect --quiet
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
