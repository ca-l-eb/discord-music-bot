add_executable(serialize_test
    json_serialize_test.cc
)

target_link_libraries(serialize_test discordcpp)

add_test(
    NAME serialization
    COMMAND serialize_test
)
