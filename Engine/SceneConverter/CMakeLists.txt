cmake_minimum_required(VERSION 3.12)

project(Rendering-For-Fun)

include(../../CMake/CommonMacros.txt)

include_directories(../../deps/src/vulkan/include)
include_directories(../../shared)

SETUP_APP(SceneConverter "Rendering For Fun")

target_link_libraries(SceneConverter PRIVATE SharedUtils meshoptimizer)

if(UNIX)
    target_link_libraries(SceneConverter PRIVATE tbb)
endif()

if(BUILD_WITH_EASY_PROFILER)
    target_link_libraries(SceneConverter PRIVATE easy_profiler)
endif()

if(BUILD_WITH_OPTICK)
    target_link_libraries(SceneConverter PRIVATE OptickCore)
endif()