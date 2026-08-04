cmake_minimum_required(VERSION 3.22)
project(MyMod VERSION 1.0.0 LANGUAGES CXX)

# Require C++20 for modern UE4SS API features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure Target Unreal Engine Version & System Defines
add_compile_definitions(
    # Set this to your exact UE version macro (e.g., UE_VERSION_5_1, UE_VERSION_5_3)
    UE_VERSION_5_1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

# Collect all project source and header files
file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

# Target Include Directories
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    # Points to the C++ API headers included in the UE4SS Mod Template dependency folder
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/UE4SS/include"
)

# MSVC Compiler Options
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4          # High warning level
        /permissive- # Strict conformance
        /utf-8       # Ensure UTF-8 execution character set
    )
endif()