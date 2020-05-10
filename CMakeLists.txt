cmake_minimum_required(VERSION 3.0)
project(discord)

find_package(Threads REQUIRED)

include(CTest)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_subdirectory(${CMAKE_SOURCE_DIR}/src)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()
