cmake_minimum_required(VERSION 3.4)
project(discord)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-ignored-qualifiers -pedantic -pipe")

add_subdirectory(test)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost 1.4 COMPONENTS system REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavutil libswresample libavcodec libavformat)

set(SOURCE_FILES
        src/api.cc
        src/audio/decoding.cc
        src/audio/file_source.cc
        src/audio/opus_encoder.cc
        src/audio/source.cc
        src/audio/youtube_dl.cc
        src/callbacks.cc
        src/discord.cc
        src/errors.cc
        src/gateway.cc
        src/gateway_store.cc
        src/net/connection.cc
        src/net/resource_parser.cc
        src/net/rtp.cc
        src/voice/crypto.cc
        src/voice/voice_gateway.cc
        src/voice/voice_state_listener.cc
        )

set(HEADER_FILES
        src/aliases.h
        src/api.h
        src/audio/decoding.h
        src/audio/file_source.h
        src/audio/opus_encoder.h
        src/audio/source.h
        src/audio/youtube_dl.h
        src/callbacks.h
        src/discord.h
        src/errors.h
        src/gateway.h
        src/gateway_store.h
        src/heartbeater.h
        src/net/connection.h
        src/net/resource_parser.h
        src/net/rtp.h
        src/voice/crypto.h
        src/voice/voice_gateway.h
        src/voice/voice_state_listener.h
        )

add_executable(discordbot src/main.cc ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(discordbot ${CMAKE_THREAD_LIBS_INIT} ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${FFMPEG_LIBRARIES} opus sodium)
target_include_directories(discordbot PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/libs)
