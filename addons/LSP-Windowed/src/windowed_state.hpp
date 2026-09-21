#pragma once
#include <cstdint>
#include <mutex>
#include <windows.h>

// Centralized state for the windowed mode addon
struct WindowedSettings {
    bool enabled = true;     // master switch: off = no virtual monitor, no window moves (works live, no restart)
    bool splitMode = false;
    int splitType = 0;       // 0:Left 1:Right 2:Top 3:Bottom
    bool positionMode = false;
    int positionSide = 1;    // 0:Left 1:Right 2:Top 3:Bottom
};

struct WindowedState {
    std::mutex mutex;
    WindowedSettings settings;

    RECT targetRect = {0, 0, 1920, 1080};
    RECT lsRect = {0, 0, 1920, 1080};
    HWND hTargetWindow = nullptr;
    HWND hFoundOverlay = nullptr;

    bool running = true;
};

// Singleton accessor
WindowedState& GetState();
