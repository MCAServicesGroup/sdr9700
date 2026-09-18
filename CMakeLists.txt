cmake_minimum_required(VERSION 3.25)
project(SDR9700 VERSION 26.9.4 LANGUAGES C CXX)

include(CTest)

set(SDR9700_DISPLAY_VERSION "26.9.4-beta.1")
option(SDR9700_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer instrumentation" OFF)
option(SDR9700_THREAD_SANITIZE "Enable ThreadSanitizer instrumentation" OFF)
option(SDR9700_ENABLE_HIDAPI "Build optional Icom RC-28 support when hidapi is available" ON)
option(SDR9700_ENABLE_GPU_PANADAPTER "Render the panadapter through Qt RHI" ${APPLE})
option(SDR9700_BUILD_BENCHMARKS "Build opt-in performance benchmarks" OFF)
set(SDR9700_QT_PRIVATE_INCLUDE_ROOT "" CACHE PATH
    "Optional Qt include root containing versioned QtGui private headers")

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

if(SDR9700_ENABLE_GPU_PANADAPTER)
    if(Qt6_VERSION VERSION_LESS 6.7)
        message(FATAL_ERROR "The GPU panadapter requires Qt 6.7 or newer (found Qt ${Qt6_VERSION}).")
    endif()

    find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
    if(NOT SDR9700_QT_PRIVATE_INCLUDE_ROOT)
        find_package(Qt6GuiPrivate QUIET)
    endif()

    set(SDR9700_QT_GUI_PRIVATE_TARGET_USABLE OFF)
    if(TARGET Qt6::GuiPrivate AND NOT SDR9700_QT_PRIVATE_INCLUDE_ROOT)
        set(SDR9700_QT_GUI_PRIVATE_TARGET_USABLE ON)
        get_target_property(SDR9700_QT_GUI_PRIVATE_TARGET_INCLUDES Qt6::GuiPrivate INTERFACE_INCLUDE_DIRECTORIES)
        foreach(SDR9700_QT_GUI_PRIVATE_TARGET_INCLUDE IN LISTS SDR9700_QT_GUI_PRIVATE_TARGET_INCLUDES)
            if(IS_ABSOLUTE "${SDR9700_QT_GUI_PRIVATE_TARGET_INCLUDE}" AND
               NOT EXISTS "${SDR9700_QT_GUI_PRIVATE_TARGET_INCLUDE}")
                set(SDR9700_QT_GUI_PRIVATE_TARGET_USABLE OFF)
                break()
            endif()
        endforeach()
    endif()

    if(NOT SDR9700_QT_GUI_PRIVATE_TARGET_USABLE)
        set(SDR9700_QT_GUI_PRIVATE_INCLUDE_DIRS "")
        if(SDR9700_QT_PRIVATE_INCLUDE_ROOT)
            list(APPEND SDR9700_QT_GUI_PRIVATE_INCLUDE_DIRS
                "${SDR9700_QT_PRIVATE_INCLUDE_ROOT}/QtGui/${Qt6_VERSION}"
                "${SDR9700_QT_PRIVATE_INCLUDE_ROOT}/QtGui/${Qt6_VERSION}/QtGui"
            )
        else()
            get_target_property(SDR9700_QT_GUI_INCLUDE_DIRS Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
            foreach(SDR9700_QT_GUI_INCLUDE_DIR IN LISTS SDR9700_QT_GUI_INCLUDE_DIRS)
                if(EXISTS "${SDR9700_QT_GUI_INCLUDE_DIR}/${Qt6_VERSION}/QtGui/rhi/qrhi.h")
                    list(APPEND SDR9700_QT_GUI_PRIVATE_INCLUDE_DIRS
                        "${SDR9700_QT_GUI_INCLUDE_DIR}/${Qt6_VERSION}"
                        "${SDR9700_QT_GUI_INCLUDE_DIR}/${Qt6_VERSION}/QtGui"
                    )
                    break()
                endif()
            endforeach()
        endif()

        set(SDR9700_QT_GUI_PRIVATE_HEADERS_FOUND OFF)
        foreach(SDR9700_QT_PRIVATE_INCLUDE_DIR IN LISTS SDR9700_QT_GUI_PRIVATE_INCLUDE_DIRS)
            if(EXISTS "${SDR9700_QT_PRIVATE_INCLUDE_DIR}/QtGui/rhi/qrhi.h")
                set(SDR9700_QT_GUI_PRIVATE_HEADERS_FOUND ON)
            endif()
        endforeach()
        if(NOT SDR9700_QT_GUI_PRIVATE_HEADERS_FOUND)
            message(FATAL_ERROR
                "The GPU panadapter requires QtGui's version-matched RHI headers. Install the Qt private "
                "development headers for this Qt version or provide SDR9700_QT_PRIVATE_INCLUDE_ROOT.")
        endif()
    endif()

    if(APPLE)
        message(STATUS "SDR9700 GPU panadapter enabled with the Metal backend")
    else()
        message(STATUS "SDR9700 GPU panadapter enabled for build verification")
    endif()
endif()

add_subdirectory(src)
