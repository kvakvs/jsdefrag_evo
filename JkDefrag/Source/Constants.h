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
