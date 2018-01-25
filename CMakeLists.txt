cmake_minimum_required(VERSION 3.6)
project(discord)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")

add_subdirectory(test)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost 1.4 COMPONENTS system REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavutil libswresample libavcodec libavformat)

set(SOURCE_FILES
        src/api.cc
        src/audio_source/youtube_dl.cc
        src/callbacks.cc
        src/delayed_message_sender.cc
        src/discord.cc
        src/events/echo_listener.cc
        src/events/hello_responder.cc
        src/gateway.cc
        src/heartbeater.cc
        src/net/base64.cc
        src/net/http_response.cc
        src/net/resource_parser.cc
        src/net/send_queue.cc
        src/net/websocket.cc
        src/voice/crypto.cc
        src/voice/decoding.cc
        src/voice/opus_encoder.cc
        src/voice/voice_gateway.cc
        src/voice/voice_state_listener.cc
        src/gateway_store.cc
        )

set(HEADER_FILES
        src/api.h
        src/audio_source/source.h
        src/audio_source/youtube_dl.h
        src/callbacks.h
        src/delayed_message_sender
        src/discord.h
        src/events/echo_listener.h
        src/events/event_listener.h
        src/events/hello_responder.h
        src/gateway.h
        src/heartbeater.h
        src/net/base64.h
        src/net/http_request.h
        src/net/http_response.h
        src/net/resource_parser.h
        src/net/send_queue.h
        src/net/websocket.h
        src/voice/crypto.h
        src/voice/decoding.h
        src/voice/opus_encoder.h
        src/voice/voice_gateway.h
        src/voice/voice_state_listener.h
        src/gateway_store.h
        )

add_executable(discord src/main.cc ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(discord ${CMAKE_THREAD_LIBS_INIT} ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${FFMPEG_LIBRARIES} opus sodium)
target_include_directories(discord PUBLIC ${CMAKE_SOURCE_DIR}/src)
