cmake_minimum_required(VERSION 3.25)
project(sdr9700 VERSION 26.9.3 LANGUAGES C CXX)

include(CTest)

set(sdr9700_DISPLAY_VERSION "26.9.3-beta.3")
option(sdr9700_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer instrumentation" OFF)
option(sdr9700_THREAD_SANITIZE "Enable ThreadSanitizer instrumentation" OFF)
option(sdr9700_ENABLE_HIDAPI "Build optional Icom RC-28 support when hidapi is available" ON)
option(sdr9700_ENABLE_GPU_PANADAPTER "Render the panadapter through Qt RHI" ${APPLE})
set(sdr9700_QT_PRIVATE_INCLUDE_ROOT "" CACHE PATH
    "Optional Qt include root containing versioned QtGui private headers")

if(APPLE)
    enable_language(OBJCXX)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "macOS target architecture" FORCE)
    endif()
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "sdr9700 macOS builds must use CMAKE_OSX_ARCHITECTURES=arm64.")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(sdr9700_SANITIZE AND sdr9700_THREAD_SANITIZE)
    message(FATAL_ERROR "sdr9700_SANITIZE and sdr9700_THREAD_SANITIZE cannot be enabled together.")
endif()

if(sdr9700_SANITIZE OR sdr9700_THREAD_SANITIZE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        message(FATAL_ERROR "sdr9700 sanitizer builds require GCC or Clang.")
    endif()
endif()

if(sdr9700_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(sdr9700_THREAD_SANITIZE)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(sdr9700_ALLOWED_BUILD_TYPES Release Debug)
if(NOT CMAKE_BUILD_TYPE IN_LIST sdr9700_ALLOWED_BUILD_TYPES)
    message(FATAL_ERROR "Unsupported CMAKE_BUILD_TYPE '${CMAKE_BUILD_TYPE}'. Use Release or Debug.")
endif()

set(sdr9700_DEBUG_BUILD 0)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(sdr9700_DEBUG_BUILD 1)
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

if(sdr9700_ENABLE_GPU_PANADAPTER)
    if(Qt6_VERSION VERSION_LESS 6.7)
        message(FATAL_ERROR "The GPU panadapter requires Qt 6.7 or newer (found Qt ${Qt6_VERSION}).")
    endif()

    find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
    if(NOT sdr9700_QT_PRIVATE_INCLUDE_ROOT)
        find_package(Qt6GuiPrivate QUIET)
    endif()

    set(sdr9700_QT_GUI_PRIVATE_TARGET_USABLE OFF)
    if(TARGET Qt6::GuiPrivate AND NOT sdr9700_QT_PRIVATE_INCLUDE_ROOT)
        set(sdr9700_QT_GUI_PRIVATE_TARGET_USABLE ON)
        get_target_property(sdr9700_QT_GUI_PRIVATE_TARGET_INCLUDES Qt6::GuiPrivate INTERFACE_INCLUDE_DIRECTORIES)
        foreach(sdr9700_QT_GUI_PRIVATE_TARGET_INCLUDE IN LISTS sdr9700_QT_GUI_PRIVATE_TARGET_INCLUDES)
            if(IS_ABSOLUTE "${sdr9700_QT_GUI_PRIVATE_TARGET_INCLUDE}" AND
               NOT EXISTS "${sdr9700_QT_GUI_PRIVATE_TARGET_INCLUDE}")
                set(sdr9700_QT_GUI_PRIVATE_TARGET_USABLE OFF)
                break()
            endif()
        endforeach()
    endif()

    if(NOT sdr9700_QT_GUI_PRIVATE_TARGET_USABLE)
        set(sdr9700_QT_GUI_PRIVATE_INCLUDE_DIRS "")
        if(sdr9700_QT_PRIVATE_INCLUDE_ROOT)
            list(APPEND sdr9700_QT_GUI_PRIVATE_INCLUDE_DIRS
                "${sdr9700_QT_PRIVATE_INCLUDE_ROOT}/QtGui/${Qt6_VERSION}"
                "${sdr9700_QT_PRIVATE_INCLUDE_ROOT}/QtGui/${Qt6_VERSION}/QtGui"
            )
        else()
            get_target_property(sdr9700_QT_GUI_INCLUDE_DIRS Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
            foreach(sdr9700_QT_GUI_INCLUDE_DIR IN LISTS sdr9700_QT_GUI_INCLUDE_DIRS)
                if(EXISTS "${sdr9700_QT_GUI_INCLUDE_DIR}/${Qt6_VERSION}/QtGui/rhi/qrhi.h")
                    list(APPEND sdr9700_QT_GUI_PRIVATE_INCLUDE_DIRS
                        "${sdr9700_QT_GUI_INCLUDE_DIR}/${Qt6_VERSION}"
                        "${sdr9700_QT_GUI_INCLUDE_DIR}/${Qt6_VERSION}/QtGui"
                    )
                    break()
                endif()
            endforeach()
        endif()

        set(sdr9700_QT_GUI_PRIVATE_HEADERS_FOUND OFF)
        foreach(sdr9700_QT_PRIVATE_INCLUDE_DIR IN LISTS sdr9700_QT_GUI_PRIVATE_INCLUDE_DIRS)
            if(EXISTS "${sdr9700_QT_PRIVATE_INCLUDE_DIR}/QtGui/rhi/qrhi.h")
                set(sdr9700_QT_GUI_PRIVATE_HEADERS_FOUND ON)
            endif()
        endforeach()
        if(NOT sdr9700_QT_GUI_PRIVATE_HEADERS_FOUND)
            message(FATAL_ERROR
                "The GPU panadapter requires QtGui's version-matched RHI headers. Install the Qt private "
                "development headers for this Qt version or provide sdr9700_QT_PRIVATE_INCLUDE_ROOT.")
        endif()
    endif()

    if(APPLE)
        message(STATUS "sdr9700 GPU panadapter enabled with the Metal backend")
    else()
        message(STATUS "sdr9700 GPU panadapter enabled for build verification")
    endif()
endif()

add_subdirectory(src)
