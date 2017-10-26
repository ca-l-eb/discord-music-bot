find_package(GTest REQUIRED)

add_executable(test_json json_serialize_test.cc ../src/user.h ../src/guild.h ../src/channel.h)
target_link_libraries(test_json ${GTEST_LIBRARIES})
target_include_directories(test_json PUBLIC ${CMAKE_SOURCE_DIR}/src)
