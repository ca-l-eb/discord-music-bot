cmake_minimum_required(VERSION 3.6)
project(discord)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")

find_package(Threads REQUIRED)

set(SOURCE_FILES src/main.cc src/gateway.cc src/gateway.h src/api.cc src/api.h src/gateway_event_listeners.cc src/gateway_event_listeners.h)
add_executable(discord ${SOURCE_FILES})
target_link_libraries(discord /usr/local/lib/libcmdsock.so ${CMAKE_THREAD_LIBS_INIT})
target_include_directories(discord PUBLIC "$ENV{HOME}/code/cc/cmdsock/src")
