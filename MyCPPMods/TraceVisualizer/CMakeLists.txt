cmake_minimum_required(VERSION 3.22)

set(TARGET TraceVisualizer)
project(${TARGET})

# Every .cpp under src/ is part of the mod, so a glob is honest here rather
# than lazy. CONFIGURE_DEPENDS keeps it in step when files are added.
file(GLOB_RECURSE TRACEVIZ_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(${TARGET} SHARED ${TRACEVIZ_SOURCES})

target_compile_features(${TARGET} PRIVATE cxx_std_20)

target_include_directories(${TARGET}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    # include/ holds the public consumer header: this mod compiles it, and it
    # is also the file other mod authors copy into their own project.
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

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
