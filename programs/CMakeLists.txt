add_executable(shrewd-basic-cli main.c)

target_link_libraries(shrewd-basic-cli
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# Log demo: every log interface, colored on a terminal, plain in files.
add_executable(shrewd-basic-log-demo log_demo.c log_demo_ext.c)

target_link_libraries(shrewd-basic-log-demo
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# Reflect demo: every reflect interface (metadata, dump, paths, ops).
add_executable(shrewd-basic-reflect-demo reflect_demo.c reflect_demo_ext.c)

target_link_libraries(shrewd-basic-reflect-demo
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# JSON demo: value model (parse/query/modify/serialize) and struct mapping.
add_executable(shrewd-basic-json-demo json_demo.c)

target_link_libraries(shrewd-basic-json-demo
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

# Bin demo: wire bytes, round trip, flexible tails via value_size.
add_executable(shrewd-basic-bin-demo bin_demo.c)

target_link_libraries(shrewd-basic-bin-demo
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)
