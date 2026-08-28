cmake_minimum_required(VERSION 3.25)

project(TargetLinesNative
    VERSION 2.0.0
    LANGUAGES CXX
)

add_library(_TargetLines SHARED
    TargetLinesModule.cpp
    TargetLinesRenderer.cpp
    TargetLinesRenderer.h
    SceneHook/SceneHook.h
    exports.def
)

set_target_properties(_TargetLines PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../addons/TargetLines/libs"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../addons/TargetLines/libs"
)

target_compile_features(_TargetLines PRIVATE cxx_std_17)
target_link_libraries(_TargetLines PRIVATE d3d8)

if(MSVC)
    target_compile_options(_TargetLines PRIVATE /W4 /EHsc)
else()
    target_compile_options(_TargetLines PRIVATE -Wall -Wextra -Wpedantic -m32)
    target_link_options(_TargetLines PRIVATE -m32 -static -static-libgcc -static-libstdc++)
endif()
