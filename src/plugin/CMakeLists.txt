cmake_minimum_required(VERSION 3.25)

project(TargetLines
    VERSION 1.1.0
    LANGUAGES CXX
)

add_library(TargetLines SHARED
    TargetLines.cpp
    WindowerPlugin.h
    exports.def
)

set_target_properties(TargetLines PROPERTIES PREFIX "")

target_compile_features(TargetLines PRIVATE cxx_std_17)
target_link_libraries(TargetLines PRIVATE d3d8)

if(MSVC)
    target_compile_options(TargetLines PRIVATE /W4 /EHsc)
else()
    target_compile_options(TargetLines PRIVATE -Wall -Wextra -Wpedantic -m32)
    target_link_options(TargetLines PRIVATE -m32 -static -static-libgcc -static-libstdc++)
endif()
