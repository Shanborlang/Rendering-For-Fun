cmake_minimum_required(VERSION 3.12)

project(Rendering-For-Fun)

include(../../CMake/CommonMacros.txt)

#include(../CMake/ThirdPartyLibraries.cmake)
include_directories(../shared)

SETUP_APP(OpenGL_Engine "Rendering For Fun")

target_link_libraries(OpenGL_Engine PRIVATE assimp meshoptimizer SharedUtils)