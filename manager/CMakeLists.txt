cmake_minimum_required(VERSION 3.14)
project(EchoAddonManager VERSION 0.2.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define UNICODE for Windows GUI API
add_definitions(-DUNICODE -D_UNICODE)

# Fetch ImGui
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    # Pinned: addons share this ImGuiContext across DLL boundaries, so the struct layout must match
    # the ImGui commit they were built against. A floating branch silently breaks that.
    GIT_TAG        367b2c24f399988ddafc0bb4628da0106bcc09be
)
FetchContent_MakeAvailable(imgui)

# MinHook (BSD-2-Clause): the Windowed feature hooks Windows' display enumeration and DXGI with it. Pinned, like Dear ImGui.
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
)
FetchContent_MakeAvailable(minhook)

# Source files
set(CORE_SOURCES
    src/core/main.cpp
    src/core/shader_hook.cpp
    src/core/d3d11_hook.cpp
)

set(ADDON_SOURCES
    src/addon/addon_manager.cpp
    src/addon/addon_manifest.cpp
    src/addon/addon_discovery.cpp
    src/addon/addon_runtime.cpp
    src/addon/addon_security.cpp
    src/addon/addon_dependency.cpp
    src/addon/addon_install.cpp
)

set(FEATURE_SOURCES
    src/features/features.cpp
    src/features/reshade_passthrough.cpp
    src/features/reshade_windows.cpp
    src/features/reshade_input.cpp
    src/features/windowed_mode.cpp
    src/features/windowed_dxgi.cpp
)

set(EVENT_SOURCES
    src/event/event_system.cpp
)

set(HOST_SOURCES
    src/host/host_impl.cpp
    src/host/metrics.cpp
    src/host/gpu_stats.cpp
)

set(CONFIG_SOURCES
    src/config/config_manager.cpp
    src/config/settings_backup.cpp
    src/diag/diagnostics.cpp
)

set(LOG_SOURCES
    src/log/logger.cpp
)

set(GUI_SOURCES
    src/gui/gui_manager.cpp
    src/gui/gui_style.cpp
    src/gui/icon_loader.cpp
    src/gui/tabs/tab_addons.cpp
    src/gui/tabs/tab_features.cpp
    src/gui/tabs/tab_settings.cpp
    src/gui/tabs/tab_logs.cpp
    src/gui/tabs/tab_performance.cpp
    src/gui/tabs/tab_about.cpp
    src/gui/widgets/toggle_switch.cpp
    src/gui/widgets/toast.cpp
    src/gui/widgets/search_bar.cpp
    src/gui/widgets/addon_card.cpp
    src/gui/widgets/status_bar.cpp
    src/gui/widgets/empty_state.cpp
    src/gui/widgets/file_dialog.cpp
    src/gui/window/chrome.cpp
    src/gui/window/dpi.cpp
    src/gui/window/hotkey.cpp
    src/gui/window/main_frame.cpp
    src/gui/window/placement.cpp
    src/gui/window/status_text.cpp
    src/gui/window/tray.cpp
    src/gui/window/window_device.cpp
)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

# Main DLL target
add_library(Lossless SHARED
    ${CORE_SOURCES}
    ${ADDON_SOURCES}
    ${FEATURE_SOURCES}
    ${EVENT_SOURCES}
    ${HOST_SOURCES}
    ${CONFIG_SOURCES}
    ${LOG_SOURCES}
    ${GUI_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(Lossless PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${minhook_SOURCE_DIR}/include
)

target_link_libraries(Lossless
    d3d11
    dxgi
    d3dcompiler
    bcrypt
    minhook
)

# Keep a PDB and a linker map next to the Release DLL so crash backtraces can be resolved.
if(MSVC)
    target_compile_options(Lossless PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(Lossless PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF /MAP>)
endif()

# Offscreen UI preview (no window): renders the theme, toggle, cards and sliders to BMP files. Not built by default;
# build it with: cmake --build . --config Release --target lsproxy_uipreview
add_executable(lsproxy_uipreview EXCLUDE_FROM_ALL
    tools/ui_preview.cpp
    src/gui/gui_style.cpp
    src/gui/widgets/toggle_switch.cpp
    src/gui/widgets/toast.cpp
    src/gui/widgets/addon_card.cpp
    src/gui/widgets/status_bar.cpp
    src/gui/widgets/empty_state.cpp
    src/gui/window/status_text.cpp
    src/gui/tabs/tab_about.cpp
    src/gui/tabs/tab_features.cpp
    src/features/features.cpp
    src/features/reshade_passthrough.cpp
    src/features/reshade_windows.cpp
    src/features/reshade_input.cpp
    src/features/windowed_mode.cpp
    src/features/windowed_dxgi.cpp
    src/gui/tabs/tab_performance.cpp
    src/gui/tabs/tab_settings.cpp
    src/gui/tabs/tab_logs.cpp
    src/gui/widgets/file_dialog.cpp
    src/host/metrics.cpp
    src/host/gpu_stats.cpp
    src/config/config_manager.cpp
    src/config/settings_backup.cpp
    src/diag/diagnostics.cpp
    src/log/logger.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(lsproxy_uipreview PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
    ${minhook_SOURCE_DIR}/include
)
target_link_libraries(lsproxy_uipreview d3d11 dxgi d3dcompiler minhook)

# Offline test of installing addons from a folder / zip / dll: cmake --build . --config Release --target lsproxy_installtest
add_executable(lsproxy_installtest EXCLUDE_FROM_ALL tools/install_test.cpp src/addon/addon_install.cpp src/host/metrics.cpp src/host/gpu_stats.cpp src/config/settings_backup.cpp src/diag/diagnostics.cpp)
target_include_directories(lsproxy_installtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# SDK install target (copies headers for addon developers)
# Offline test of the addon handling (scan, load, init, toggle, remove, install, security), run against a throw-away folder with a small
# test addon: cmake --build . --config Release --target lsproxy_coretest
add_library(lsproxy_testaddon SHARED EXCLUDE_FROM_ALL tools/test_addon.cpp)
target_include_directories(lsproxy_testaddon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include ${imgui_SOURCE_DIR})
add_executable(lsproxy_coretest EXCLUDE_FROM_ALL tools/core_test.cpp
    ${ADDON_SOURCES} ${FEATURE_SOURCES} ${EVENT_SOURCES} ${HOST_SOURCES} ${CONFIG_SOURCES} ${LOG_SOURCES}
    src/gui/icon_loader.cpp src/core/d3d11_hook.cpp src/core/shader_hook.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(lsproxy_coretest PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include ${CMAKE_CURRENT_SOURCE_DIR}/third_party ${minhook_SOURCE_DIR}/include)
target_link_libraries(lsproxy_coretest d3d11 dxgi d3dcompiler bcrypt minhook)
add_dependencies(lsproxy_coretest lsproxy_testaddon)
add_custom_command(TARGET lsproxy_coretest POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lsproxy_testaddon> $<TARGET_FILE_DIR:lsproxy_coretest>)

# Offline test of the built-in features (ReShade passthrough on a hidden window, the Windowed virtual display through DXGI). Run it twice:
# once plain, once with the argument "off" (Windowed switched off): cmake --build . --config Release --target lsproxy_featurestest
add_executable(lsproxy_featurestest EXCLUDE_FROM_ALL tools/features_test.cpp
    ${FEATURE_SOURCES} src/config/config_manager.cpp src/log/logger.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(lsproxy_featurestest PRIVATE ${imgui_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include ${CMAKE_CURRENT_SOURCE_DIR}/third_party ${minhook_SOURCE_DIR}/include)
target_link_libraries(lsproxy_featurestest d3d11 dxgi user32 minhook)

# Smoke test of the manager window (real GUI thread, hidden: no window appears). Run it twice: plain, and with the argument "place":
# cmake --build . --config Release --target lsproxy_guitest
add_executable(lsproxy_guitest EXCLUDE_FROM_ALL tools/gui_test.cpp
    ${ADDON_SOURCES} ${FEATURE_SOURCES} ${EVENT_SOURCES} ${HOST_SOURCES} ${CONFIG_SOURCES} ${LOG_SOURCES}
    src/core/d3d11_hook.cpp src/core/shader_hook.cpp
    ${GUI_SOURCES} ${IMGUI_SOURCES})
target_include_directories(lsproxy_guitest PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include ${CMAKE_CURRENT_SOURCE_DIR}/third_party ${minhook_SOURCE_DIR}/include)
target_link_libraries(lsproxy_guitest d3d11 dxgi d3dcompiler bcrypt minhook)

install(DIRECTORY sdk/include/lsproxy DESTINATION include)
