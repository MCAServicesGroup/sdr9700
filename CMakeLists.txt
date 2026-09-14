cmake_minimum_required(VERSION 3.25)
project(SDR9700 VERSION 26.9.3 LANGUAGES C CXX)

include(CTest)

set(SDR9700_DISPLAY_VERSION "26.9.3-beta.2")
option(SDR9700_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer instrumentation" OFF)
option(SDR9700_THREAD_SANITIZE "Enable ThreadSanitizer instrumentation" OFF)
option(SDR9700_ENABLE_HIDAPI "Build optional Icom RC-28 support when hidapi is available" ON)

if(APPLE)
    enable_language(OBJCXX)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "macOS target architecture" FORCE)
    endif()
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "SDR9700 macOS builds must use CMAKE_OSX_ARCHITECTURES=arm64.")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(SDR9700_SANITIZE AND SDR9700_THREAD_SANITIZE)
    message(FATAL_ERROR "SDR9700_SANITIZE and SDR9700_THREAD_SANITIZE cannot be enabled together.")
endif()

if(SDR9700_SANITIZE OR SDR9700_THREAD_SANITIZE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        message(FATAL_ERROR "SDR9700 sanitizer builds require GCC or Clang.")
    endif()
endif()

if(SDR9700_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(SDR9700_THREAD_SANITIZE)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(SDR9700_ALLOWED_BUILD_TYPES Release Debug)
if(NOT CMAKE_BUILD_TYPE IN_LIST SDR9700_ALLOWED_BUILD_TYPES)
    message(FATAL_ERROR "Unsupported CMAKE_BUILD_TYPE '${CMAKE_BUILD_TYPE}'. Use Release or Debug.")
endif()

set(SDR9700_DEBUG_BUILD 0)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SDR9700_DEBUG_BUILD 1)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Multimedia
    MultimediaWidgets
    Sql
    Svg
)
if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(src)
