cmake_minimum_required(VERSION 3.22)
project(DirectPipe VERSION 3.10.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: treat source/execution as UTF-8 to avoid C4819 warnings
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Platform info
if(NOT WIN32)
    message(STATUS "Non-Windows build: some features (ASIO, WASAPI Exclusive) are Windows-only.")
endif()

# Options
option(DIRECTPIPE_BUILD_TESTS "Build unit tests" ON)
option(DIRECTPIPE_BUILD_HOST "Build JUCE host application" ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ─── JUCE (via FetchContent) ───
include(FetchContent)

option(DIRECTPIPE_BUILD_RECEIVER "Build Receiver plugin" ON)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
    GIT_SHALLOW TRUE
)

# Fetch JUCE if building host or receiver
if(DIRECTPIPE_BUILD_HOST OR DIRECTPIPE_BUILD_RECEIVER)
    FetchContent_MakeAvailable(JUCE)

    # VST2 SDK (interface headers for VST2 plugin hosting + Receiver VST2 format)
    set(VST2_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/VST2_SDK")
    if(EXISTS "${VST2_SDK_PATH}/pluginterfaces/vst2.x/aeffect.h")
        juce_set_vst2_sdk_path("${VST2_SDK_PATH}")
        set(DIRECTPIPE_HAS_VST2 ON)
        message(STATUS "VST2 SDK found at ${VST2_SDK_PATH}")
    else()
        set(DIRECTPIPE_HAS_VST2 OFF)
        message(STATUS "VST2 SDK not found — VST2 format disabled")
    endif()
endif()

# ─── Google Test (for testing) ───
if(DIRECTPIPE_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# ─── Sub-projects ───

# Core library
add_subdirectory(core)

# JUCE Host application
if(DIRECTPIPE_BUILD_HOST)
    add_subdirectory(host)
endif()

# Receiver plugin (VST2/VST3/AU)
if(DIRECTPIPE_BUILD_RECEIVER)
    add_subdirectory(plugins/receiver)
endif()

# Tests
if(DIRECTPIPE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
