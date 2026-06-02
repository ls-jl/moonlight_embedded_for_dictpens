cmake_minimum_required(VERSION 3.5)

if(DEFINED CROSS_TOOLCHAIN_PREFIX AND NOT "${CROSS_TOOLCHAIN_PREFIX}" STREQUAL "")
    set(CMAKE_C_COMPILER "${CROSS_TOOLCHAIN_PREFIX}gcc")
    set(CMAKE_CXX_COMPILER "${CROSS_TOOLCHAIN_PREFIX}g++")
endif()

project(jsapi_rgbframe C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(CMAKE_SKIP_BUILD_RPATH true)

set(IOT_UI_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/iot-miniapp-sdk")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${IOT_UI_SDK_PATH}/include")

file(GLOB_RECURSE LIBSOURCES "${IOT_UI_SDK_PATH}/src/*.cpp")
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(jsapi_rgbframe SHARED ${LIBSOURCES} ${SOURCES})
target_include_directories(jsapi_rgbframe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/jsapi_rgbframe")
target_compile_options(jsapi_rgbframe PRIVATE -Wall -Werror=return-type -Os -g)
target_link_libraries(jsapi_rgbframe PRIVATE pthread)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(jsapi_rgbframe PRIVATE -Wl,-undefined,dynamic_lookup)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(jsapi_rgbframe PRIVATE -Wl,-unresolved-symbols=ignore-all)
endif()

set(CMAKE_VERBOSE_MAKEFILE ON)
