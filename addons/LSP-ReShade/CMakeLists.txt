cmake_minimum_required(VERSION 3.15)
project(LSP_ReShade VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    # Pinned to the commit the manager is built with: addons share its ImGuiContext, so the struct layout must match.
    GIT_TAG        367b2c24f399988ddafc0bb4628da0106bcc09be
)
FetchContent_MakeAvailable(imgui)

set(SOURCES
    src/main.cpp
    src/input_sim.cpp
    src/window_manager.cpp
)

add_library(LSP_ReShade SHARED ${SOURCES})

target_include_directories(LSP_ReShade PRIVATE
    ${imgui_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../manager/sdk/include
)

target_sources(LSP_ReShade PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_link_libraries(LSP_ReShade user32)

# Offline lifecycle test (not part of the default build): cmake --build . --config Release --target reshade_lifecycle_test
add_executable(reshade_lifecycle_test EXCLUDE_FROM_ALL tools/lifecycle_test.cpp)
target_include_directories(reshade_lifecycle_test PRIVATE
    ${imgui_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../manager/sdk/include
)
add_dependencies(reshade_lifecycle_test LSP_ReShade)
