cmake_minimum_required(VERSION 3.0)
project(discord)

find_package(Threads REQUIRED)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

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
    src/net/rtp.cc
    src/net/uri.cc
    src/voice/crypto.cc
    src/voice/voice_connector.cc
    src/voice/voice_gateway.cc
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
    src/net/rtp.h
    src/net/uri.h
    src/voice/crypto.h
    src/voice/voice_connector.h
    src/voice/voice_gateway.h
)
 
add_executable(discordbot src/main.cc ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(discordbot Threads::Threads ${CONAN_LIBS})
target_include_directories(discordbot PRIVATE ${CMAKE_SOURCE_DIR}/src ${CONAN_INCLUDE_DIRS})
target_compile_features(discordbot PUBLIC cxx_std_17)

if(MSVC)
#   target_compile_options(discordbot PRIVATE /W4 /WX)
else()
  target_compile_options(discordbot PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
