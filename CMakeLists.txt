cmake_minimum_required(VERSION 3.6)
project(discord)

set(CMAKE_CXX_STANDARD 14)

find_package(Threads REQUIRED)

set(SOURCE_FILES src/main.cc src/gateway.cc src/gateway.h src/api.cc src/api.h)
add_executable(discord ${SOURCE_FILES})
target_link_libraries(discord /usr/local/lib/libcmdsock.so ${CMAKE_THREAD_LIBS_INIT})
target_include_directories(discord PUBLIC "$ENV{HOME}/code/cc/cmdsock/src")
