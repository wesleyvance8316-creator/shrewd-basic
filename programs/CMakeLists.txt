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

# Data-format demos: built only for the enabled formats.
if(SHREWD_BASIC_FORMAT_JSON)

# JSON demo: value model (parse/query/modify/serialize) and struct mapping.
add_executable(shrewd-basic-json-demo json_demo.c)

target_link_libraries(shrewd-basic-json-demo
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

endif()

if(SHREWD_BASIC_FORMAT_BIN)

# Bin demo: wire bytes, round trip, flexible tails via value_size.
add_executable(shrewd-basic-bin-demo bin_demo.c)

target_link_libraries(shrewd-basic-bin-demo
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

endif()

# Formats demo: every interface of the text layer and the six formats
# (built when the whole family is enabled).
if(SHREWD_BASIC_FORMAT_INI AND SHREWD_BASIC_FORMAT_ENV AND SHREWD_BASIC_FORMAT_CSV
   AND SHREWD_BASIC_FORMAT_TOML AND SHREWD_BASIC_FORMAT_MSGPACK AND SHREWD_BASIC_FORMAT_CBOR
   AND SHREWD_BASIC_FORMAT_XML)

add_executable(shrewd-basic-formats-demo formats_demo.c)

target_link_libraries(shrewd-basic-formats-demo
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

endif()

# dbus demo: every sb_dbus interface (policy hooks need json).
if(SHREWD_BASIC_DBUS AND SHREWD_BASIC_FORMAT_JSON)

add_executable(shrewd-basic-dbus-demo dbus_demo.c)

target_link_libraries(shrewd-basic-dbus-demo
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)

endif()
