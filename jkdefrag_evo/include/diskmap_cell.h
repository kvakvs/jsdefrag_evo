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

//
// Created by Dmytro on 20-Jul-23.
//

#pragma once

// Represents state of a disk cluster, used for coloring the diskmap. Dirty requires a redraw.
using DiskMapCell = struct {
    bool dirty: 1;
    bool empty: 1;
    bool allocated: 1;
    bool unfragmented: 1;
    bool unmovable: 1;
    bool fragmented: 1;
    bool busy: 1;
    bool mft: 1;
    bool spacehog: 1;
};
