cmake_minimum_required(VERSION 3.20)
project(DLSS5NR01 C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)

set(NGX_DIR ${CMAKE_SOURCE_DIR}/external/ngx)
set(NGX_LIB ${NGX_DIR}/lib/nvsdk_ngx_s.lib)
if(NOT EXISTS ${NGX_LIB} OR NOT EXISTS ${NGX_DIR}/include/nvsdk_ngx.h)
    message(FATAL_ERROR "NVIDIA DLSS SDK not found. Put its headers in external/ngx/include and nvsdk_ngx_s.lib in external/ngx/lib (see external/ngx/README.md). It is NVIDIA's, under NVIDIA's licence, so it is not part of this repository: tools/fetch_ngx_sdk.ps1 fetches it.")
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

# ---- the addons: DLSS 5 Neural Rendering (DLSS5NR01) and DLSS 4 DLAA (DLSS4DLAA), from the same sources (src/addon/product.h) ----
set(NR_ADDON_SOURCES
    src/addon/addon.cpp src/addon/runtime.cpp src/addon/panel.cpp src/addon/tasks.cpp src/addon/settings.cpp src/addon/log.cpp
    src/addon/screenshot.cpp src/addon/screenshot_pixels.cpp src/addon/hud_editor.cpp src/addon/auto_quality.cpp
    src/addon/bridge.cpp src/addon/frame_tap.cpp
    src/addon/present_hook.cpp src/addon/compose11.cpp src/addon/requirements.cpp
    src/engine/nr_engine.cpp
    src/engine/dlaa_model.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
function(nr_addon target)
    add_library(${target} SHARED ${NR_ADDON_SOURCES})
    target_include_directories(${target} PRIVATE src ${NGX_DIR}/include ${CMAKE_SOURCE_DIR}/../../manager/sdk/include ${imgui_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE ${NGX_LIB} d3d12 d3d11 dxgi d3dcompiler user32 gdi32 advapi32 shlwapi ole32 shell32 comdlg32 windowscodecs)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME "${target}")
    # Keep a PDB and a linker map next to the Release DLL so the offsets in a crash backtrace can be resolved.
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/Zi>)
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF /MAP>)
    endif()
endfunction()

nr_addon(DLSS5NR01)
add_dependencies(DLSS5NR01 nr_forwarder)

nr_addon(DLSS4DLAA)
target_compile_definitions(DLSS4DLAA PRIVATE NR_PRODUCT_DLAA)
# NVIDIA's DLSS runtime, in a dlss folder next to the built addon (as in the release), where the DLAA addon points NGX
if(EXISTS ${NGX_DIR}/bin/nvngx_dlss.dll)
    add_custom_command(TARGET DLSS4DLAA POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:DLSS4DLAA>/dlss
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NGX_DIR}/bin/nvngx_dlss.dll $<TARGET_FILE_DIR:DLSS4DLAA>/dlss)
endif()

# ---- offline test host: loads the built addon with a fake IHost, headless ImGui and a synthetic LSFG pattern ----
add_executable(nr_hosttest tools/addon_host_test.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(nr_hosttest PRIVATE ${CMAKE_SOURCE_DIR}/../../manager/sdk/include ${CMAKE_SOURCE_DIR}/tools ${CMAKE_SOURCE_DIR}/../../manager/tools ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(nr_hosttest PRIVATE d3d11 dxgi d3dcompiler)
add_dependencies(nr_hosttest DLSS5NR01 DLSS4DLAA)

# ---- offline test of the frame tap (which pass is the capture, the motion hand-over, presents); a D3D11 device, no model, no SDK at run time ----
add_executable(nr_taptest tools/tap_test.cpp src/addon/frame_tap.cpp)
target_include_directories(nr_taptest PRIVATE src)
target_link_libraries(nr_taptest PRIVATE d3d11)

# ---- offline test of the settings and looks (the table, looks as text and back, the settings file through a stand-in host)
add_executable(nr_settingstest tools/settings_test.cpp src/addon/settings.cpp src/addon/screenshot_pixels.cpp)
target_include_directories(nr_settingstest PRIVATE src ${CMAKE_SOURCE_DIR}/../../manager/sdk/include)

# ---- offline test of auto quality (a simulated model)
add_executable(nr_autotest tools/auto_test.cpp src/addon/auto_quality.cpp)
target_include_directories(nr_autotest PRIVATE src)

# ---- offline test of the requirements checker (no model, no NVIDIA SDK needed at run time) ----
add_executable(nr_reqtest tools/requirements_test.cpp src/addon/requirements.cpp)
target_include_directories(nr_reqtest PRIVATE src)
target_link_libraries(nr_reqtest PRIVATE dxgi)
