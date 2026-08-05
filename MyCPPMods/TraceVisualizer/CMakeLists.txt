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

    # RE-UE4SS/UE4SS/CMakeLists.txt links several first-party static libraries
    # (Unreal, DynamicOutput, and likely others) into UE4SS as PUBLIC
    # dependencies:
    #     target_link_libraries(UE4SS PUBLIC Unreal SinglePassSigScanner
    #                                        DynamicOutput ...)
    # UE4SS is itself a SHARED library, and its dllexport'd symbols include
    # everything those libraries define (FField/FProperty/UObject/
    # UObjectGlobals, DefaultTargets::get_default_devices_ref, ...) since their
    # object code is compiled directly into UE4SS.dll. Because the
    # dependencies are PUBLIC, CMake also puts each of their .lib files on the
    # link line of anything that links UE4SS -- including this target -- so
    # the linker sees two definitions of the same symbols: once via UE4SS.lib
    # (correct: resolved dynamically against the already-loaded UE4SS.dll,
    # which is how a mod DLL is supposed to bind to it) and once from each
    # library's own object files linked in a second time directly (LNK2005
    # "already defined in UE4SS.lib(UE4SS.dll)").
    #
    # This is not fixable from here by changing PRIVATE/PUBLIC on our own
    # target_link_libraries(UE4SS) call above -- that keyword controls
    # whether OUR dependents inherit UE4SS's requirements, not whether WE do;
    # a direct consumer always inherits a linked target's PUBLIC usage
    # requirements regardless of which keyword it links that target with.
    # And RE-UE4SS's own CMakeLists.txt is a build dependency this project
    # does not edit.
    #
    # /NODEFAULTLIB:Unreal(.lib) was tried first and had no measurable effect
    # -- confirmed against a real build, not assumed -- most likely because
    # CMake places these as explicit link inputs rather than ones pulled in
    # through the linker's implicit default-library search, which is the only
    # mechanism /NODEFAULTLIB suppresses.
    #
    # A #pragma message diagnostic (since removed) confirmed this mod's own
    # compiled code correctly sees these symbols as dllimport, i.e. the
    # duplication is not coming from anything this mod's source does -- it is
    # purely the same already-compiled Unreal.lib/DynamicOutput.lib object
    # code being linked in from two places at once. Both copies are the exact
    # same bytes (Unreal and DynamicOutput are each built as one CMake target
    # producing one .lib; UE4SS.dll's build links that file into itself, and
    # this target's build links the identical file in again directly) -- this
    # is not a genuine ODR conflict between two different implementations,
    # just the same object code counted twice. /FORCE:MULTIPLE, which is
    # usually risky because it can silently pick between two DIFFERING
    # definitions, is safe specifically in this case because there is no
    # difference to pick between.
    target_link_options(${TARGET} PRIVATE "/FORCE:MULTIPLE")
endif()
