set(SOURCE_FILES
    api.cc
    audio/decoding.cc
    audio/file_source.cc
    audio/opus_encoder.cc
    audio/source.cc
    audio/youtube_dl.cc
    callbacks.cc
    discord.cc
    errors.cc
    gateway.cc
    gateway_store.cc
    net/connection.cc
    net/rtp.cc
    net/uri.cc
    voice/crypto.cc
    voice/voice_connector.cc
    voice/voice_gateway.cc
)

set(HEADER_FILES
    aliases.h
    api.h
    audio/decoding.h
    audio/file_source.h
    audio/opus_encoder.h
    audio/source.h
    audio/youtube_dl.h
    callbacks.h
    discord.h
    errors.h
    gateway.h
    gateway_store.h
    heartbeater.h
    net/connection.h
    net/rtp.h
    net/uri.h
    voice/crypto.h
    voice/voice_connector.h
    voice/voice_gateway.h
)

add_library(discordcpp ${SOURCE_FILES} ${HEADER_FILES})
target_compile_features(discordcpp PUBLIC cxx_std_17)
target_include_directories(discordcpp PUBLIC ${CMAKE_SOURCE_DIR}/src ${CONAN_INCLUDE_DIRS})
target_link_libraries(discordcpp Threads::Threads ${CONAN_LIBS})

add_executable(discordbot main.cc)
target_link_libraries(discordbot discordcpp)
target_compile_features(discordbot PUBLIC cxx_std_17)

if(MSVC)
else()
  target_compile_options(discordbot PRIVATE -Wall -Wextra -pedantic -Werror)
  target_compile_options(discordcpp PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
