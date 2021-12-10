cmake_minimum_required(VERSION 3.12)

project(FilterEnvmap)

include(../../CMake/CommonMacros.txt)

#include(../CMake/ThirdPartyLibraries.cmake)
include_directories(../shared)

SETUP_APP(FilterEnvmap "Rendering For Fun")

target_link_libraries(FilterEnvmap PRIVATE SharedUtils)