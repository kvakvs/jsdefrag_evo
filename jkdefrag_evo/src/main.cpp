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

#include "precompiled_header.h"
#include "defrag.h"

void set_locale();

int __stdcall WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show) {
    set_locale();

    Defrag *defrag = Defrag::get_instance();
    WPARAM ret_value = 0;

    if (defrag != nullptr) {
        ret_value = defrag->start_program(instance, prev_instance, cmd_line, cmd_show);

        Defrag::release_instance();
    }

    return (int) ret_value;
}

void set_locale() {
    std::locale::global(std::locale("en_US.UTF-8"));
//    std::setlocale(LC_ALL, "en_US.UTF-8");
//    auto ploc = std::localeconv();
//    strcpy_s(ploc->decimal_point, 16, ".");
//    strcpy_s(ploc->thousands_sep, 16, "'");
}
