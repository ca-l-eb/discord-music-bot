cmake_minimum_required(VERSION 3.6)
project(discord)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")

find_package(Threads REQUIRED)
find_package(Boost 1.4 COMPONENTS system REQUIRED)
link_directories(/usr/local/lib)

set(SOURCE_FILES
        src/api.cc
        src/events/echo_listener.cc
        src/heartbeater.cc
        src/events/hello_responder.cc
        src/events/voice_state_listener.cc
        src/gateway.cc
        src/voice_gateway.cc
        src/opus_encoder.cc
        src/crypto.cc
        )

set(HEADER_FILES
        src/api.h
        src/events/echo_listener.h
        src/events/event_listener.h
        src/heartbeater.h
        src/events/hello_responder.h
        src/events/voice_state_listener.h
        src/gateway.h
        src/voice_gateway.h
        src/opus_encoder.h
        src/crypto.h
        )

add_executable(discord src/main.cc ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(discord cmdsock ${CMAKE_THREAD_LIBS_INIT} ${Boost_LIBRARIES} opus sodium)
target_include_directories(discord PUBLIC "$ENV{HOME}/code/cc/cmdsock/src" ${CMAKE_SOURCE_DIR}/src)
