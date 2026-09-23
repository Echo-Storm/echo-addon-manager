cmake_minimum_required(VERSION 3.20)
project(DLSS5NR01 C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)

set(NGX_DIR ${CMAKE_SOURCE_DIR}/external/ngx)
set(NGX_LIB ${NGX_DIR}/lib/nvsdk_ngx_s.lib)
if(NOT EXISTS ${NGX_LIB} OR NOT EXISTS ${NGX_DIR}/include/nvsdk_ngx.h)
    message(FATAL_ERROR "NVIDIA DLSS SDK not found. Put its headers in external/ngx/include and nvsdk_ngx_s.lib in external/ngx/lib (see external/ngx/README.md). It is not redistributable, so it is not part of this repository.")
endif()

# ---- third party (fetched at configure time) ----
include(FetchContent)
# ImGui docking branch. Must match the layout compiled into the installed Echo Addon Manager (Lossless.dll).
# Re-pinned 2026-09-10 to the commit our own from-source manager build resolved docking HEAD to.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        367b2c24f399988ddafc0bb4628da0106bcc09be)
FetchContent_MakeAvailable(imgui)   # Dear ImGui has no CMake project of its own: this only fetches it

# ---- forwarder: the only module allowed to call the DLSSNR snippet directly ----
add_library(nr_forwarder SHARED src/forwarder/nr_forwarder.cpp)
target_include_directories(nr_forwarder PRIVATE src)
target_link_libraries(nr_forwarder PRIVATE d3d12)
set_target_properties(nr_forwarder PROPERTIES OUTPUT_NAME "nvngx.dll_dlss5nr01" PREFIX "" SUFFIX ".dll")

# ---- standalone harness (proof / measurement) ----
add_executable(nr_harness src/harness/nr_harness.cpp)
target_include_directories(nr_harness PRIVATE src ${NGX_DIR}/include)
target_link_libraries(nr_harness PRIVATE ${NGX_LIB} d3d12 dxgi user32 gdi32 advapi32 shlwapi ole32 windowscodecs)
add_dependencies(nr_harness nr_forwarder)

# ---- the compatibility self-test: does this model file work on this graphics card? Started by the addon in its own process ----
add_executable(nr_selftest src/selftest/nr_selftest.cpp)
target_include_directories(nr_selftest PRIVATE src ${NGX_DIR}/include)
target_link_libraries(nr_selftest PRIVATE ${NGX_LIB} d3d12 dxgi user32 advapi32 shlwapi ole32 version)
# the addon's version, for the shareable report (--report): from addon.json, so it is never out of step
file(READ ${CMAKE_SOURCE_DIR}/addon.json NR_ADDON_JSON)
string(REGEX MATCH "\"version\": *\"([0-9.]+)\"" NR_ADDON_JSON_MATCH "${NR_ADDON_JSON}")
target_compile_definitions(nr_selftest PRIVATE NR_ADDON_VERSION_TEXT="${CMAKE_MATCH_1}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/addon.json)
add_dependencies(nr_selftest nr_forwarder)

# ---- the addon ----
add_library(DLSS5NR01 SHARED
    src/addon/addon.cpp src/addon/bridge.cpp src/addon/frame_tap.cpp
    src/addon/present_hook.cpp src/addon/compose11.cpp src/addon/requirements.cpp
    src/engine/nr_engine.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(DLSS5NR01 PRIVATE src ${NGX_DIR}/include ${CMAKE_SOURCE_DIR}/../../manager/sdk/include ${imgui_SOURCE_DIR})
target_link_libraries(DLSS5NR01 PRIVATE ${NGX_LIB} d3d12 d3d11 dxgi d3dcompiler user32 gdi32 advapi32 shlwapi ole32 shell32 comdlg32)
set_target_properties(DLSS5NR01 PROPERTIES PREFIX "" OUTPUT_NAME "DLSS5NR01")
# Keep a PDB and a linker map next to the Release DLL so the offsets in a crash backtrace can be resolved.
if(MSVC)
    target_compile_options(DLSS5NR01 PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(DLSS5NR01 PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF /MAP>)
endif()
add_dependencies(DLSS5NR01 nr_forwarder)

# ---- offline test host: loads the built addon with a fake IHost, headless ImGui and a synthetic LSFG pattern ----
add_executable(nr_hosttest tools/addon_host_test.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(nr_hosttest PRIVATE ${CMAKE_SOURCE_DIR}/../../manager/sdk/include ${CMAKE_SOURCE_DIR}/tools ${CMAKE_SOURCE_DIR}/../../manager/tools ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(nr_hosttest PRIVATE d3d11 dxgi d3dcompiler)
add_dependencies(nr_hosttest DLSS5NR01)

# ---- offline test of the frame tap (which pass is the capture, the motion hand-over, presents); a D3D11 device, no model, no SDK at run time ----
add_executable(nr_taptest tools/tap_test.cpp src/addon/frame_tap.cpp)
target_include_directories(nr_taptest PRIVATE src)
target_link_libraries(nr_taptest PRIVATE d3d11)

# ---- offline test of the requirements checker (no model, no NVIDIA SDK needed at run time) ----
add_executable(nr_reqtest tools/requirements_test.cpp src/addon/requirements.cpp)
target_include_directories(nr_reqtest PRIVATE src)
target_link_libraries(nr_reqtest PRIVATE dxgi)
