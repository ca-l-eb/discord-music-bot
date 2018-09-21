cmake_minimum_required(VERSION 3.0)
project(discord)

if (testing_enabled)
    add_subdirectory(test)
endif()

add_executable(discordbot)
target_compile_features(discordbot PUBLIC cxx_std_17)
target_compile_options(discordbot PUBLIC -Wall -Wextra -pedantic -pipe)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost 1.66 COMPONENTS system REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFmpeg REQUIRED libavutil libswresample libavcodec libavformat)
pkg_check_modules(Opus REQUIRED opus)
pkg_check_modules(Sodium REQUIRED libsodium)

target_sources(discordbot
PRIVATE
    src/aliases.h
    src/api.cc
    src/api.h
    src/audio/decoding.cc
    src/audio/decoding.h
    src/audio/file_source.cc
    src/audio/file_source.h
    src/audio/opus_encoder.cc
    src/audio/opus_encoder.h
    src/audio/source.cc
    src/audio/source.h
    src/audio/youtube_dl.cc
    src/audio/youtube_dl.h
    src/callbacks.cc
    src/callbacks.h
    src/discord.cc
    src/discord.h
    src/errors.cc
    src/errors.h
    src/gateway.cc
    src/gateway.h
    src/gateway_store.cc
    src/gateway_store.h
    src/heartbeater.h
    src/main.cc
    src/net/connection.cc
    src/net/connection.h
    src/net/rtp.cc
    src/net/rtp.h
    src/net/uri.cc
    src/net/uri.h
    src/voice/crypto.cc
    src/voice/crypto.h
    src/voice/voice_gateway.cc
    src/voice/voice_gateway.h
    src/voice/voice_connector.cc
    src/voice/voice_connector.h
)

target_link_libraries(discordbot
    Boost::system
    Threads::Threads
    ${OPENSSL_LIBRARIES}
    ${FFmpeg_LIBRARIES}
    ${Opus_LIBRARIES}
    ${Sodium_LIBRARIES}
)

target_include_directories(discordbot
PRIVATE
    ${OPENSSL_INCLUDE_DIRS}
    ${FFmpeg_INCLUDE_DIRS}
    ${Opus_INCLUDE_DIRS}
    ${Sodium_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs
)
