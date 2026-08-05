cmake_minimum_required(VERSION 3.22)

set(TARGET TraceVisualizer)
project(${TARGET})

# This directory cannot be configured on its own: the UE4SS target is created by
# add_subdirectory(RE-UE4SS) in the repository root.
#
# Without this check the mistake is painful to diagnose. CMake treats a bare
# name like "UE4SS" in target_link_libraries as a plain library filename rather
# than a missing target, so configuring this folder alone SUCCEEDS, contributes
# no UE4SS include directories, and then fails at compile time with hundreds of
# cascading "cannot open source file" errors.
if(NOT TARGET UE4SS)
    message(FATAL_ERROR
        "TraceVisualizer must be configured from the repository ROOT, not from this directory.\n"
        "The UE4SS target does not exist here, so no UE4SS headers would be on the include path.\n"
        "\n"
        "From the repository root:\n"
        "    git submodule update --init --recursive\n"
        "    generate_project_files.bat\n"
        "    build_mods_shipping.bat\n"
        "\n"
        "Or equivalently:\n"
        "    cmake -B build -G \"Visual Studio 17 2022\" .\n"
        "    cmake --build build --config Game__Shipping__Win64\n")
endif()

# Every .cpp under src/ is part of the mod, so a glob is honest here rather
# than lazy. CONFIGURE_DEPENDS keeps it in step when files are added.
file(GLOB_RECURSE TRACEVIZ_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(${TARGET} SHARED ${TRACEVIZ_SOURCES})

# UE4SS itself requires C++23 and exports that as a PUBLIC compile feature, so
# this only makes the requirement explicit rather than inheriting it silently.
target_compile_features(${TARGET} PRIVATE cxx_std_23)

target_include_directories(${TARGET}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    # include/ holds the public consumer header: this mod compiles it, and it
    # is also the file other mod authors copy into their own project.
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# NOMINMAX is not optional. Without it, <windows.h>'s min/max macros clobber
# any FMath::Max(...)/FMath::Min(...) call textually, which is exactly what
# happens inside UEPseudo's own TArray::RemoveAt/RemoveAtSwap. The macro
# expansion eats the Max/Min token and leaves e.g. "FMath::(...)", producing a
# cascade of syntax errors ("illegal token on right side of '::'", then
# "undeclared identifier" for whatever local the mangled statement was
# supposed to declare) deep inside an Unreal header that this mod never
# touches directly -- so if this definition is ever removed again, the
# failure will look exactly as confusing as it did the first time.
# WIN32_LEAN_AND_MEAN avoids pulling in the parts of <windows.h> nothing here
# needs.
target_compile_definitions(${TARGET} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

# UE4SS brings the Unreal reflection headers, the mod base class and ImGui.
#
# There is deliberately no UE_VERSION_* define here. This mod resolves every
# engine call through runtime reflection rather than against a compiled-in
# struct layout, so it is not built against a specific engine version. If you
# later add code that does need the engine version, that define belongs in the
# RE-UE4SS build configuration, not here.
target_link_libraries(${TARGET} PRIVATE UE4SS)

if(MSVC)
    target_compile_options(${TARGET} PRIVATE
        /W4          # High warning level
        /permissive- # Strict conformance
        /utf-8       # Ensure UTF-8 execution character set
    )
endif()
