cmake_minimum_required(VERSION 3.15)
project(LSP_Windowed VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# MinHook
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(minhook)

# ImGui (needed for shared context with host)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    # Pinned to the commit the manager is built with: addons share its ImGuiContext, so the struct layout must match.
    GIT_TAG        367b2c24f399988ddafc0bb4628da0106bcc09be
)
FetchContent_MakeAvailable(imgui)

# Source files
set(SOURCES
    src/main.cpp
    src/dxgi_proxy.cpp
    src/windowed_state.cpp
)

# Create the DLL
add_library(LSP_Windowed SHARED ${SOURCES})

target_include_directories(LSP_Windowed PRIVATE
    ${MINHOOK_DIR}/include
    ${imgui_SOURCE_DIR}
    # Echo Addon Manager SDK headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../manager/sdk/include
)

# ImGui core sources (no backends - host provides rendering)
target_sources(LSP_Windowed PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_link_libraries(LSP_Windowed
    d3d11.lib
    dxgi.lib
    minhook
)

set_target_properties(LSP_Windowed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LSP_Windowed"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LSP_Windowed"
)

# Offline test (not part of the default build): cmake --build . --config Release --target windowed_proxy_test
add_executable(windowed_proxy_test EXCLUDE_FROM_ALL tools/proxy_test.cpp)
target_include_directories(windowed_proxy_test PRIVATE
    ${imgui_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../manager/sdk/include
)
target_link_libraries(windowed_proxy_test dxgi.lib user32.lib)
add_dependencies(windowed_proxy_test LSP_Windowed)
