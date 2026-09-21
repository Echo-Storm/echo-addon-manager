# A standalone build of the sample addon (nothing else from this repository is needed except the SDK headers):
#   cmake -S examples/SampleAddon -B examples/SampleAddon/build -G "Visual Studio 17 2022" -A x64
#   cmake --build examples/SampleAddon/build --config Release
# Then copy addon.json and Release\SampleAddon.dll into <Lossless Scaling>\addons\SampleAddon\ and start Lossless Scaling.
cmake_minimum_required(VERSION 3.20)
project(SampleAddon CXX)
set(CMAKE_CXX_STANDARD 17)
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)

# Dear ImGui must be the same version the manager was built with: the manager and every addon share one ImGuiContext, so the layout of its structs has to match.
# This is the commit pinned in manager/CMakeLists.txt.
include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG 367b2c24f399988ddafc0bb4628da0106bcc09be)
FetchContent_Populate(imgui)

add_library(SampleAddon SHARED
    SampleAddon.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(SampleAddon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../manager/sdk/include ${imgui_SOURCE_DIR})
set_target_properties(SampleAddon PROPERTIES PREFIX "" OUTPUT_NAME "SampleAddon")
