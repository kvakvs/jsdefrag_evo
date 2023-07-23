/*
 JkDefrag  --  Defragment and optimize all harddisks.

 This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 Public License as published by the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 For the full text of the license see the "License gpl.txt" file.

 Jeroen C. Kessels, Internet Engineer
 http://www.kessels.com/
 */

#pragma once

#include <gdiplus.h>

constexpr COLORREF display_colors[9] = {
        RGB(240, 240, 240),     // 0 Empty         - Empty diskspace
        RGB(160, 160, 160),     // 1 Allocated     - Used diskspace / system files
        RGB(128, 128, 128),     // 2 Unfragmented  - Unfragmented files
        RGB(128, 0, 0),         // 3 Unmovable     - Unmovable files
        RGB(200, 60, 60),       // 4 Fragmented    - Fragmented files
        RGB(255, 160, 0),       // 5 Busy          - Busy color
        RGB(220, 0, 180),       // 6 Mft           - MFT reserved zones
        RGB(80, 80, 200),       // 7 SpaceHog      - Large files, rarely changed
};

// The colors used by the defragger
enum class DrawColor : uint8_t {
    // Empty diskspace
    Empty = 0,
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
    // Not a real color, used for counting the enum values
    MaxValue,
};
