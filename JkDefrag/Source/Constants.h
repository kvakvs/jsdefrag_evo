#pragma once

enum class DebugLevel {
    // Fatal errors.
    Fatal = 0,
    // Warning messages
    Warning = 1,
    // General progress messages.
    Progress = 2,
    // Detailed progress messages
    DetailedProgress = 3,
    // Detailed file information.
    DetailedFileInfo = 4,
    // Detailed gap-filling messages.
    DetailedGapFilling = 5,
    // Detailed gap-finding messages.
    DetailedGapFinding = 6
};

#define APP_NAME "JkDefragEvo"
#define APP_NAME_W L"JkDefragEvo"
// Used by the GUI to lock itself
#define DISPLAY_MUTEX APP_NAME "DisplayMutex"

constexpr COLORREF display_colors[9] = {
        RGB(150, 150, 150),     // 0 Empty         - Empty diskspace
        RGB(200, 200, 200),     // 1 Allocated     - Used diskspace / system files
        RGB(0, 150, 0),         // 2 Unfragmented  - Unfragmented files
        RGB(128, 0, 0),         // 3 Unmovable     - Unmovable files
        RGB(200, 100, 60),      // 4 Fragmented    - Fragmented files
        RGB(0, 0, 255),         // 5 Busy          - Busy color
        RGB(255, 0, 255),       // 6 Mft           - MFT reserved zones
        RGB(0, 150, 150),       // 7 SpaceHog      - Large files, rarely changed
        // RGB(255, 255, 255)      // 8 background
};

// The colors used by the defragger
enum class DrawColor : uint8_t {
    // Empty diskspace
    Empty,
    // Used diskspace / system files
    Allocated,
    // Unfragmented files
    Unfragmented,
    // Unmovable files, or files we decided to not move
    Unmovable,
    // Fragmented files
    Fragmented,
    // Busy color
    Busy,
    // MFT reserved zones
    Mft,
    // Spacehogs
    SpaceHog,
    // Background color
    // Background
};
