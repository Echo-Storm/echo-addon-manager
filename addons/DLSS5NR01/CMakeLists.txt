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
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4)
FetchContent_Populate(imgui)
FetchContent_Populate(minhook)

add_library(minhook_static STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook_static PUBLIC ${minhook_SOURCE_DIR}/include)

# ---- forwarder: the only module allowed to call the DLSSNR snippet directly ----
add_library(lspnr_forwarder SHARED src/forwarder/lspnr_forwarder.cpp)
target_include_directories(lspnr_forwarder PRIVATE src)
target_link_libraries(lspnr_forwarder PRIVATE d3d12)
set_target_properties(lspnr_forwarder PROPERTIES OUTPUT_NAME "nvngx.dll_lspnr" PREFIX "" SUFFIX ".dll")

# ---- standalone harness (proof / measurement) ----
add_executable(lspnr_harness src/harness/lspnr_harness.cpp)
target_include_directories(lspnr_harness PRIVATE src ${NGX_DIR}/include)
target_link_libraries(lspnr_harness PRIVATE ${NGX_LIB} d3d12 dxgi user32 gdi32 advapi32 shlwapi ole32 windowscodecs)
add_dependencies(lspnr_harness lspnr_forwarder)

# ---- the addon ----
add_library(DLSS5NR01 SHARED
    src/addon/addon.cpp src/addon/bridge.cpp src/addon/frame_tap.cpp src/addon/dispatch_hook.cpp
    src/addon/present_hook.cpp src/addon/compose11.cpp
    src/engine/nr_engine.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(DLSS5NR01 PRIVATE src ${NGX_DIR}/include ${CMAKE_SOURCE_DIR}/../../manager/sdk/include ${imgui_SOURCE_DIR})
target_link_libraries(DLSS5NR01 PRIVATE minhook_static ${NGX_LIB} d3d12 d3d11 dxgi d3dcompiler user32 gdi32 advapi32 shlwapi ole32)
set_target_properties(DLSS5NR01 PROPERTIES PREFIX "" OUTPUT_NAME "DLSS5NR01")
# Keep a PDB and a linker map next to the Release DLL so the offsets in a crash backtrace can be resolved.
if(MSVC)
    target_compile_options(DLSS5NR01 PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(DLSS5NR01 PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF /MAP>)
endif()
add_dependencies(DLSS5NR01 lspnr_forwarder)

# ---- offline test host: loads the built addon with a fake IHost, headless ImGui and a synthetic LSFG pattern ----
add_executable(lspnr_hosttest tools/addon_host_test.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(lspnr_hosttest PRIVATE ${CMAKE_SOURCE_DIR}/../../manager/sdk/include ${CMAKE_SOURCE_DIR}/tools ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(lspnr_hosttest PRIVATE d3d11 dxgi d3dcompiler)
add_dependencies(lspnr_hosttest DLSS5NR01)
