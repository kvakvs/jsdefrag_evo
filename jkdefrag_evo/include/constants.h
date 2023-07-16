#pragma once

#undef max
// Used in the file ItemStruct tree for node locations
constexpr uint64_t VIRTUALFRAGMENT = std::numeric_limits<uint64_t>::max();

enum class DefragPhase {
    Analyze,
    Defragment,
    ForcedFill,
    ZoneSort,
    ZoneFastOpt,
    MoveUp,
    Fixup,
    Done,
};

enum class OptimizeMode {
    // Analyze only, do not defragment and do not optimize.
    AnalyzeOnly = 0,
    // Analyze and fixup, do not optimize.
    AnalyzeFixup = 1,
    // Analyze, fixup, and fast optimization(default).
    AnalyzeFixupFastopt = 2,
    // Deprecated.Analyze, fixup, and full optimization.
    DeprecatedAnalyzeFixupFull = 3,
    // Analyze and force together.
    AnalyzeGroup = 4,
    // Analyze and move to end of disk.
    AnalyzeMoveToEnd = 5,
    // Analyze and sort files by name.
    AnalyzeSortByName = 6,
    // Analyze and sort files by size(smallest first).
    AnalyzeSortBySize = 7,
    // Analyze and sort files by last access(newest first).
    AnalyzeSortByAccess = 8,
    // Analyze and sort files by last change(oldest first).
    AnalyzeSortByChanged = 9,
    // Analyze and sort files by creation time(oldest first).
    AnalyzeSortByCreated = 10,
    // 11 (unused?) - move to beginning of disk
    Max
};

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

enum class MoveStrategy {
    Whole,
    InFragments,
};

enum class MoveDirection {
    Up,
    Down,
};

// Format string to use for numbers formatting with thousands separators
#define NUM_FMT L"{:L}"
// Format floating point with 0 decimals
#define FLT0_FMT L"{:.0f}"
// Format floating point with 4 decimals
#define FLT4_FMT L"{:.4f}"
// Format floating point with 2 decimals
#define FLT2_FMT L"{:.2f}"

#define GAP_FOUND_FMT           L"Gap found: LCN=" NUM_FMT ", Size=" NUM_FMT
#define MOVING_1_CLUSTER_FMT    L"Moving 1 cluster from " NUM_FMT " to " NUM_FMT
#define MOVING_CLUSTERS_FMT     L"Moving " NUM_FMT " clusters from " NUM_FMT " to " NUM_FMT
#define EXTENT_FMT              L"Extent: Lcn=" NUM_FMT ", Vcn=" NUM_FMT ", NextVcn=" NUM_FMT
#define VEXTENT_FMT             L"Extent (virtual): Vcn=" NUM_FMT ", NextVcn=" NUM_FMT
#define SKIPPING_GAP_FMT        L"Skipping gap, cannot fill: " NUM_FMT " [" NUM_FMT " clusters]"
#define MFT_EXCL_FMT            L"MftExcludes[{}].Start=" NUM_FMT ", MftExcludes[{}].End=" NUM_FMT

#define SUMMARY_HEADER          L"Fragments Bytes            Clusters     Name"
#define SUMMARY_DASH_LINE       L"--------- ---------------- ------------ -----"
#define SUMMARY_FMT             L"{:9L} {:16L} {:12L}"

#define EXECUTABLE_NAME         "jkdefrag.exe"
#define CMD_EXECUTABLE_NAME     "jkdefragcmd.exe"
#define SCREENSAVER_NAME        "jkdefragscreensaver.exe"
