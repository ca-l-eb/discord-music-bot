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
add_executable(http_request
    ../src/net/http_request.h
    ../src/net/http_request.cc
    ../src/net/http_response.h
    ../src/net/http_response.cc
    http_request.cc
    )

target_link_libraries(test_json ${GTEST_LIBRARIES})
target_include_directories(test_json PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(http_request ${CMAKE_THREAD_LIBS_INIT} ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES})
target_include_directories(http_request PUBLIC ${CMAKE_SOURCE_DIR}/src)
