cmake_minimum_required(VERSION 3.12)

project(Rendering-For-Fun)

include(../../CMake/CommonMacros.txt)

#include(../CMake/ThirdPartyLibraries.cmake)
include_directories(../../deps/src/vulkan/include)
include_directories(../shared)

SETUP_APP(MeshConvert "Rendering For Fun")

target_link_libraries(MeshConvert PRIVATE assimp meshoptimizer SharedUtils)