cmake_minimum_required(VERSION 3.14)
project(EchoAddonManager VERSION 0.1.0)

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

# Source files
set(CORE_SOURCES
    src/core/main.cpp
    src/core/shader_hook.cpp
    src/core/d3d11_hook.cpp
)

set(ADDON_SOURCES
    src/addon/addon_manager.cpp
    src/addon/addon_security.cpp
    src/addon/addon_dependency.cpp
    src/addon/addon_install.cpp
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
    src/gui/tabs/tab_settings.cpp
    src/gui/tabs/tab_logs.cpp
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
)

target_link_libraries(Lossless
    d3d11
    dxgi
    d3dcompiler
    bcrypt
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
    src/gui/tabs/tab_about.cpp
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
)
target_link_libraries(lsproxy_uipreview d3d11 dxgi d3dcompiler)

# Offline test of installing addons from a folder / zip / dll: cmake --build . --config Release --target lsproxy_installtest
add_executable(lsproxy_installtest EXCLUDE_FROM_ALL tools/install_test.cpp src/addon/addon_install.cpp src/host/metrics.cpp src/host/gpu_stats.cpp src/config/settings_backup.cpp src/diag/diagnostics.cpp)
target_include_directories(lsproxy_installtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# SDK install target (copies headers for addon developers)
install(DIRECTORY sdk/include/lsproxy DESTINATION include)
