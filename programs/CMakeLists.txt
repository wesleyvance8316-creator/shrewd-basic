add_executable(shrewd-basic-cli main.c)

target_link_libraries(shrewd-basic-cli
        PRIVATE
            shrewd-basic
            shrewd-basic-warnings)
