cmake_minimum_required(VERSION 3.12)

project(Rendering-For-Fun)

include(../../CMake/CommonMacros.txt)

include(../../CMake/ThirdPartyLibraries.cmake)
include_directories(../../deps/src/imgui)
include_directories(../../deps/src/vulkan/include)
include_directories(../../shared)

SETUP_APP(Vulkan_Engine "Rendering For Fun")

target_link_libraries(Vulkan_Engine PRIVATE SharedUtils imgui)

if(BUILD_WITH_EASY_PROFILER)
    target_link_libraries(Vulkan_Engine PRIVATE easy_profiler)
endif()

if(BUILD_WITH_OPTICK)
    target_link_libraries(Vulkan_Engine PRIVATE OptickCore)
endif()