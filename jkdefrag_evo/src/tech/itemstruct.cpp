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
#include "itemstruct.h"

/// short_path can be null
void ItemStruct::set_names(const wchar_t *long_path, const wchar_t *long_filename,
                           const wchar_t *short_path, const wchar_t *short_filename) {
    this->long_path_ = long_path;
    this->long_filename_ = long_filename;

    if (!short_path || this->long_path_ == short_path) {
        this->short_path_ = std::nullopt;
    } else {
        this->short_path_ = short_path;
    }

    if (this->long_filename_ == short_filename) {
        this->short_filename_ = std::nullopt;
    } else {
        this->short_filename_ = short_filename;
    }
}

ItemStruct::~ItemStruct() {
    while (fragments_ != nullptr) {
        FragmentListStruct *fragment = fragments_->next_;
        delete fragments_;
        fragments_ = fragment;
    }
}

