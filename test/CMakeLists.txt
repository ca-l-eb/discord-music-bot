find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost 1.4 COMPONENTS system REQUIRED)

add_executable(test_json
    json_serialize_test.cc
    ../src/discord.cc
    ../src/discord.h
    ../src/gateway_store.cc
    ../src/gateway_store.h
    )

target_link_libraries(test_json ${GTEST_LIBRARIES})
target_include_directories(test_json PUBLIC ${CMAKE_SOURCE_DIR}/src)
