set(IMGUI_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/deps/src/imgui)
file(GLOB IMGUI_SOURCES ${IMGUI_INCLUDE_DIR}/*.cpp)
file(GLOB IMGUI_HEADERS ${IMGUI_INCLUDE_DIR}/*.h)

add_library(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_SOURCES})

add_definitions(-DIMGUI_IMPL_OPENGL_LOADER_GLAD)
add_definitions(-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS)

include_directories(
        ${IMGUI_INCLUDE_DIR})

target_link_libraries(imgui)

set(IMGUI_LIBRARIES imgui)