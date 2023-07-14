/*

The JkDefrag library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

For the full text of the license see the "License lgpl.txt" file.

Jeroen C. Kessels
Internet Engineer
http://www.kessels.com/

*/

#include "std_afx.h"

#include "defrag_data_struct.h"

DefragLib::DefragLib() = default;
DefragLib::~DefragLib() = default;

DefragLib* DefragLib::get_instance() {
    if (instance_ == nullptr) {
        instance_.reset(new DefragLib());
    }

    return instance_.get();
}

/*

All the text strings used by the defragger library.
Note: The RunJkDefrag() function call has a parameter where you can specify
a different array. Do not change this default array, simply create a new
array in your program and specify it as a parameter.

*/
WCHAR* DefaultDebugMsg[] =
{
    /*  0 */ L"",
    /*  1 */ L"",
    /*  2 */ L"",
    /*  3 */ L"",
    /*  4 */ L"",
    /*  5 */ L"",
    /*  6 */ L"",
    /*  7 */ L"",
    /*  8 */ L"",
    /*  9 */ L"",
    /* 10 */ L"Getting cluster bitmap: %s",
    /* 11 */ L"Extent: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u",
    /* 12 */ L"ERROR: could not get volume bitmap: %s",
    /* 13 */ L"Gap found: LCN=%I64d, Size=%I64d",
    /* 14 */ L"Processing '%s'",
    /* 15 */ L"Could not open '%s': %s",
    /* 16 */ L"%I64d clusters at %I64d, %I64d bytes",
    /* 17 */ L"Special file attribute: Compressed",
    /* 18 */ L"Special file attribute: Encrypted",
    /* 19 */ L"Special file attribute: Offline",
    /* 20 */ L"Special file attribute: Read-only",
    /* 21 */ L"Special file attribute: Sparse-file",
    /* 22 */ L"Special file attribute: Temporary",
    /* 23 */ L"Analyzing: %s",
    /* 24 */ L"",
    /* 25 */ L"Cannot move file away because no gap is big enough: %I64d[%I64d]",
    /* 26 */ L"Don't know which file is at the end of the gap: %I64d[%I64d]",
    /* 27 */ L"Enlarging gap %I64d[%I64d] by moving %I64d[%I64d]",
    /* 28 */ L"Skipping gap, cannot fill: %I64d[%I64d]",
    /* 29 */ L"Opening volume '%s' at mountpoint '%s'",
    /* 30 */ L"",
    /* 31 */ L"Volume '%s' at mountpoint '%s' is not mounted.",
    /* 32 */ L"Cannot defragment volume '%s' at mountpoint '%s'",
    /* 33 */ L"MftStartLcn=%I64d, MftZoneStart=%I64d, MftZoneEnd=%I64d, Mft2StartLcn=%I64d, MftValidDataLength=%I64d",
    /* 34 */ L"MftExcludes[%u].Start=%I64d, MftExcludes[%u].End=%I64d",
    /* 35 */ L"",
    /* 36 */ L"Ignoring volume '%s' because it is read-only.",
    /* 37 */ L"Analyzing volume '%s'",
    /* 38 */ L"Finished.",
    /* 39 */ L"Could not get list of volumes: %s",
    /* 40 */ L"Cannot find volume name for mountpoint '%s': %s",
    /* 41 */ L"Cannot enlarge gap at %I64d[%I64d] because of unmovable data.",
    /* 42 */ L"Windows could not move the file, trying alternative method.",
    /* 43 */ L"Cannot process clustermap of '%s': %s",
    /* 44 */ L"Disk is full, cannot defragment.",
    /* 45 */ L"Alternative method failed, leaving file where it is.",
    /* 46 */ L"Extent (virtual): Vcn=%I64u, NextVcn=%I64u",
    /* 47 */ L"Ignoring volume '%s' because of exclude mask '%s'.",
    /* 48 */ L"Vacating %I64u clusters starting at LCN=%I64u",
    /* 49 */ L"Vacated %I64u clusters (until %I64u) from LCN=%I64u",
    /* 50 */ L"Finished vacating %I64u clusters, until LCN=%I64u",
    /* 51 */ L"",
    /* 52 */ L"",
    /* 53 */ L"I am fragmented.",
    /* 54 */ L"I am in MFT reserved space.",
    /* 55 */ L"I am a regular file in zone 1.",
    /* 56 */ L"I am a spacehog in zone 1 or 2.",
    /* 57 */ L"Ignoring volume '%s' because it is not a harddisk."
};

/* Search case-insensitive for a substring. */
char* DefragLib::stristr(char* haystack, const char* needle) {
    if (haystack == nullptr || needle == nullptr) return nullptr;

    char* p1 = haystack;
    const size_t i = strlen(needle);

    while (*p1 != '\0') {
        if (_strnicmp(p1, needle, i) == 0) return p1;

        p1++;
    }

    return nullptr;
}

/* Search case-insensitive for a substring. */
WCHAR* DefragLib::stristr_w(WCHAR* haystack, const WCHAR* needle) {
    if (haystack == nullptr || needle == nullptr) return nullptr;

    WCHAR* p1 = haystack;
    const size_t i = wcslen(needle);

    while (*p1 != 0) {
        if (_wcsnicmp(p1, needle, i) == 0) return p1;

        p1++;
    }

    return nullptr;
}

/* Return a string with the error message for GetLastError(). */
void DefragLib::system_error_str(const uint32_t error_code, WCHAR* out, const size_t width) const {
    WCHAR s1[BUFSIZ];

    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                   nullptr, error_code, 0, s1, BUFSIZ, nullptr);

    /* Strip trailing whitespace. */
    WCHAR* p1 = wcschr(s1, '\0');

    while (p1 != s1) {
        p1--;

        if (*p1 != ' ' && *p1 != '\t' && *p1 != '\n' && *p1 != '\r') break;

        *p1 = '\0';
    }

    /* Add error number. */
    swprintf_s(out, width, L"[%lu] %s", error_code, s1);
}

/* Translate character to lowercase. */
WCHAR DefragLib::lower_case(const WCHAR c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';

    return c;
}

/* Dump a block of data to standard output, for debugging purposes. */
void DefragLib::show_hex([[maybe_unused]] DefragDataStruct* data, const BYTE* buffer,
                         const uint64_t count) const {
    DefragGui* gui = DefragGui::get_instance();

    int j;

    for (int i = 0; i < count; i = i + 16) {
        WCHAR s2[BUFSIZ];
        WCHAR s1[BUFSIZ];
        swprintf_s(s1,BUFSIZ, L"%4u %4X   ", i, i);

        for (j = 0; j < 16; j++) {
            if (j == 8) wcscat_s(s1,BUFSIZ, L" ");

            if (j + i >= count) {
                wcscat_s(s1,BUFSIZ, L"   ");
            }
            else {
                swprintf_s(s2,BUFSIZ, L"%02X ", buffer[i + j]);
                wcscat_s(s1,BUFSIZ, s2);
            }
        }

        wcscat_s(s1,BUFSIZ, L" ");

        for (j = 0; j < 16; j++) {
            if (j + i >= count) {
                wcscat_s(s1,BUFSIZ, L" ");
            }
            else {
                if (buffer[i + j] < 32) {
                    wcscat_s(s1,BUFSIZ, L".");
                }
                else {
                    swprintf_s(s2,BUFSIZ, L"%c", buffer[i + j]);
                    wcscat_s(s1,BUFSIZ, s2);
                }
            }
        }

        gui->show_debug(DebugLevel::Progress, nullptr, L"%s", s1);
    }
}

/*

Compare a string with a mask, case-insensitive. If it matches then return
true, otherwise false. The mask may contain wildcard characters '?' (any
character) '*' (any characters).

*/
bool DefragLib::match_mask(WCHAR* string, WCHAR* mask) {
    if (string == nullptr) return false; /* Just to speed up things. */
    if (mask == nullptr) return false;
    if (wcscmp(mask, L"*") == 0) return true;

    WCHAR* m = mask;
    WCHAR* s = string;

    while (*m != '\0' && *s != '\0') {
        if (lower_case(*m) != lower_case(*s) && *m != '?') {
            if (*m != '*') return false;

            m++;

            if (*m == '\0') return true;

            while (*s != '\0') {
                if (match_mask(s, m) == true) return true;
                s++;
            }

            return false;
        }

        m++;
        s++;
    }

    while (*m == '*') m++;

    if (*s == '\0' && *m == '\0') return true;

    return false;
}

/*

Add a string to a string array. If the array is empty then initialize, the
last item in the array is nullptr. If the array is not empty then append the
new string, realloc() the array.

*/
WCHAR** DefragLib::add_array_string(WCHAR** array, const WCHAR* new_string) {
    WCHAR** new_array;

    /* Sanity check. */
    if (new_string == nullptr) return array;

    if (array == nullptr) {
        new_array = (WCHAR**)malloc(2 * sizeof(WCHAR*));

        if (new_array == nullptr) return nullptr;

        new_array[0] = _wcsdup(new_string);

        if (new_array[0] == nullptr) return nullptr;

        new_array[1] = nullptr;

        return new_array;
    }

    int i = 0;

    while (array[i] != nullptr) i++;

    new_array = (WCHAR**)realloc(array, (i + 2) * sizeof(WCHAR*));

    if (new_array == nullptr) return nullptr;

    new_array[i] = _wcsdup(new_string);

    if (new_array[i] == nullptr) return nullptr;

    new_array[i + 1] = nullptr;

    return new_array;
}

/* Subfunction of GetShortPath(). */
void DefragLib::append_to_short_path(const ItemStruct* item, WCHAR* path, const size_t length) {
    if (item->parent_directory_ != nullptr) append_to_short_path(item->parent_directory_, path, length);

    wcscat_s(path, length, L"\\");

    if (item->short_filename_ != nullptr) {
        wcscat_s(path, length, item->short_filename_);
    }
    else if (item->long_filename_ != nullptr) {
        wcscat_s(path, length, item->long_filename_);
    }
}

/*

Return a string with the full path of an item, constructed from the short names.
Return nullptr if error. The caller must free() the new string.

*/
WCHAR* DefragLib::get_short_path(const DefragDataStruct* data, const ItemStruct* item) {
    /* Sanity check. */
    if (item == nullptr) return nullptr;

    /* Count the size of all the ShortFilename's. */
    size_t length = wcslen(data->disk_.mount_point_) + 1;

    for (auto temp_item = item; temp_item != nullptr; temp_item = temp_item->parent_directory_) {
        if (temp_item->short_filename_ != nullptr) {
            length = length + wcslen(temp_item->short_filename_) + 1;
        }
        else if (temp_item->long_filename_ != nullptr) {
            length = length + wcslen(temp_item->long_filename_) + 1;
        }
        else {
            length = length + 1;
        }
    }

    /* Allocate new string. */
    const auto path = (WCHAR*)malloc(sizeof(WCHAR) * length);

    if (path == nullptr) return nullptr;

    wcscpy_s(path, length, data->disk_.mount_point_);

    /* Append all the strings. */
    append_to_short_path(item, path, length);

    return path;
}

/* Subfunction of GetLongPath(). */
void DefragLib::append_to_long_path(const ItemStruct* item, WCHAR* path, const size_t length) {
    if (item->parent_directory_ != nullptr) append_to_long_path(item->parent_directory_, path, length);

    wcscat_s(path, length, L"\\");

    if (item->long_filename_ != nullptr) {
        wcscat_s(path, length, item->long_filename_);
    }
    else if (item->short_filename_ != nullptr) {
        wcscat_s(path, length, item->short_filename_);
    }
}

/*

Return a string with the full path of an item, constructed from the long names.
Return nullptr if error. The caller must free() the new string.

*/
WCHAR* DefragLib::get_long_path(const DefragDataStruct* data, const ItemStruct* item) {
    /* Sanity check. */
    if (item == nullptr) return nullptr;

    /* Count the size of all the LongFilename's. */
    size_t length = wcslen(data->disk_.mount_point_) + 1;

    for (const ItemStruct* temp_item = item; temp_item != nullptr; temp_item = temp_item->parent_directory_) {
        if (temp_item->long_filename_ != nullptr) {
            length = length + wcslen(temp_item->long_filename_) + 1;
        }
        else if (item->short_filename_ != nullptr) {
            length = length + wcslen(temp_item->short_filename_) + 1;
        }
        else {
            length = length + 1;
        }
    }

    /* Allocate new string. */
    const auto path = (WCHAR*)malloc(sizeof(WCHAR) * length);

    if (path == nullptr) return nullptr;

    wcscpy_s(path, length, data->disk_.mount_point_);

    /* Append all the strings. */
    append_to_long_path(item, path, length);

    return path;
}

/* Slow the program down. */
void DefragLib::slow_down(DefragDataStruct* data) {
    __timeb64 t{};

    /* Sanity check. */
    if (data->speed_ <= 0 || data->speed_ >= 100) return;

    /* Calculate the time we have to sleep so that the wall time is 100% and the
    actual running time is the "-s" parameter percentage. */
    _ftime64_s(&t);

    const int64_t now = t.time * 1000 + t.millitm;

    if (now > data->last_checkpoint_) {
        data->running_time_ = data->running_time_ + now - data->last_checkpoint_;
    }

    if (now < data->start_time_) data->start_time_ = now; /* Should never happen. */

    /* Sleep. */
    if (data->running_time_ > 0) {
        int64_t delay = data->running_time_ * (int64_t)100 / (int64_t)data->speed_ - (now - data->start_time_);

        if (delay > 30000) delay = 30000;
        if (delay > 0) Sleep((uint32_t)delay);
    }

    /* Save the current wall time, so next time we can calculate the time spent in	the program. */
    _ftime64_s(&t);

    data->last_checkpoint_ = t.time * 1000 + t.millitm;
}

/* Return the location on disk (LCN, Logical Cluster Number) of an item. */
uint64_t DefragLib::get_item_lcn(const ItemStruct* item) {
    /* Sanity check. */
    if (item == nullptr) return 0;

    const FragmentListStruct* fragment = item->fragments_;

    while (fragment != nullptr && fragment->lcn_ == VIRTUALFRAGMENT) {
        fragment = fragment->next_;
    }

    if (fragment == nullptr) return 0;

    return fragment->lcn_;
}

/* Return pointer to the first item in the tree (the first file on the volume). */
ItemStruct* DefragLib::tree_smallest(ItemStruct* top) {
    if (top == nullptr) return nullptr;

    while (top->smaller_ != nullptr) top = top->smaller_;

    return top;
}

/* Return pointer to the last item in the tree (the last file on the volume). */
ItemStruct* DefragLib::tree_biggest(ItemStruct* top) {
    if (top == nullptr) return nullptr;

    while (top->bigger_ != nullptr) top = top->bigger_;

    return top;
}

/*

If Direction=0 then return a pointer to the first file on the volume,
if Direction=1 then the last file.

*/
ItemStruct* DefragLib::tree_first(ItemStruct* top, const int direction) {
    if (direction == 0) return tree_smallest(top);

    return tree_biggest(top);
}

/* Return pointer to the previous item in the tree. */
ItemStruct* DefragLib::tree_prev(ItemStruct* here) {
    ItemStruct* temp;

    if (here == nullptr) return here;

    if (here->smaller_ != nullptr) {
        here = here->smaller_;

        while (here->bigger_ != nullptr) here = here->bigger_;

        return here;
    }

    do {
        temp = here;
        here = here->parent_;
    }
    while (here != nullptr && here->smaller_ == temp);

    return here;
}

/* Return pointer to the next item in the tree. */
ItemStruct* DefragLib::tree_next(ItemStruct* here) {
    ItemStruct* temp;

    if (here == nullptr) return nullptr;

    if (here->bigger_ != nullptr) {
        here = here->bigger_;

        while (here->smaller_ != nullptr) here = here->smaller_;

        return here;
    }

    do {
        temp = here;
        here = here->parent_;
    }
    while (here != nullptr && here->bigger_ == temp);

    return here;
}

/*

If Direction=0 then return a pointer to the next file on the volume,
if Direction=1 then the previous file.

*/
ItemStruct* DefragLib::tree_next_prev(ItemStruct* here, const bool reverse) {
    if (reverse == false) return tree_next(here);

    return tree_prev(here);
}

/* Insert a record into the tree. The tree is sorted by LCN (Logical Cluster Number). */
void DefragLib::tree_insert(DefragDataStruct* data, ItemStruct* new_item) {
    ItemStruct* b;

    if (new_item == nullptr) return;

    const uint64_t new_lcn = get_item_lcn(new_item);

    /* Locate the place where the record should be inserted. */
    ItemStruct* here = data->item_tree_;
    ItemStruct* ins = nullptr;
    int found = 1;

    while (here != nullptr) {
        ins = here;
        found = 0;

        if (const uint64_t here_lcn = get_item_lcn(here); here_lcn > new_lcn) {
            found = 1;
            here = here->smaller_;
        }
        else {
            if (here_lcn < new_lcn) found = -1;

            here = here->bigger_;
        }
    }

    /* Insert the record. */
    new_item->parent_ = ins;
    new_item->smaller_ = nullptr;
    new_item->bigger_ = nullptr;

    if (ins == nullptr) {
        data->item_tree_ = new_item;
    }
    else {
        if (found > 0) {
            ins->smaller_ = new_item;
        }
        else {
            ins->bigger_ = new_item;
        }
    }

    /* If there have been less than 1000 inserts then return. */
    data->balance_count_ = data->balance_count_ + 1;

    if (data->balance_count_ < 1000) return;

    /* Balance the tree.
    It's difficult to explain what exactly happens here. For an excellent
    tutorial see:
    http://www.stanford.edu/~blp/avl/libavl.html/Balancing-a-BST.html
    */

    data->balance_count_ = 0;

    /* Convert the tree into a vine. */
    ItemStruct* a = data->item_tree_;
    ItemStruct* c = a;
    long count = 0;

    while (a != nullptr) {
        /* If A has no Bigger child then move down the tree. */
        if (a->bigger_ == nullptr) {
            count = count + 1;
            c = a;
            a = a->smaller_;

            continue;
        }

        /* Rotate left at A. */
        b = a->bigger_;

        if (data->item_tree_ == a) data->item_tree_ = b;

        a->bigger_ = b->smaller_;

        if (a->bigger_ != nullptr) a->bigger_->parent_ = a;

        b->parent_ = a->parent_;

        if (b->parent_ != nullptr) {
            if (b->parent_->smaller_ == a) {
                b->parent_->smaller_ = b;
            }
            else {
                a->parent_->bigger_ = b;
            }
        }

        b->smaller_ = a;
        a->parent_ = b;

        /* Do again. */
        a = b;
    }

    /* Calculate the number of skips. */
    long skip = 1;

    while (skip < count + 2) skip = skip << 1;

    skip = count + 1 - (skip >> 1);

    /* Compress the tree. */
    while (c != nullptr) {
        if (skip <= 0) c = c->parent_;

        a = c;

        while (a != nullptr) {
            b = a;
            a = a->parent_;

            if (a == nullptr) break;

            /* Rotate right at A. */
            if (data->item_tree_ == a) data->item_tree_ = b;

            a->smaller_ = b->bigger_;

            if (a->smaller_ != nullptr) a->smaller_->parent_ = a;

            b->parent_ = a->parent_;

            if (b->parent_ != nullptr) {
                if (b->parent_->smaller_ == a) {
                    b->parent_->smaller_ = b;
                }
                else {
                    b->parent_->bigger_ = b;
                }
            }

            a->parent_ = b;
            b->bigger_ = a;

            /* Next item. */
            a = b->parent_;

            /* If there were skips then leave if all done. */
            skip = skip - 1;
            if (skip == 0) break;
        }
    }
}

/*

Detach (unlink) a record from the tree. The record is not freed().
See: http://www.stanford.edu/~blp/avl/libavl.html/Deleting-from-a-BST.html

*/
void DefragLib::tree_detach(DefragDataStruct* data, const ItemStruct* item) {
    /* Sanity check. */
    if (data->item_tree_ == nullptr || item == nullptr) return;

    if (item->bigger_ == nullptr) {
        /* It is trivial to delete a node with no Bigger child. We replace
        the pointer leading to the node by it's Smaller child. In
        other words, we replace the deleted node by its Smaller child. */
        if (item->parent_ != nullptr) {
            if (item->parent_->smaller_ == item) {
                item->parent_->smaller_ = item->smaller_;
            }
            else {
                item->parent_->bigger_ = item->smaller_;
            }
        }
        else {
            data->item_tree_ = item->smaller_;
        }

        if (item->smaller_ != nullptr) item->smaller_->parent_ = item->parent_;
    }
    else if (item->bigger_->smaller_ == nullptr) {
        /* The Bigger child has no Smaller child. In this case, we move Bigger
        into the node's place, attaching the node's Smaller subtree as the
        new Smaller. */
        if (item->parent_ != nullptr) {
            if (item->parent_->smaller_ == item) {
                item->parent_->smaller_ = item->bigger_;
            }
            else {
                item->parent_->bigger_ = item->bigger_;
            }
        }
        else {
            data->item_tree_ = item->bigger_;
        }

        item->bigger_->parent_ = item->parent_;
        item->bigger_->smaller_ = item->smaller_;

        if (item->smaller_ != nullptr) item->smaller_->parent_ = item->bigger_;
    }
    else {
        /* Replace the node by it's inorder successor, that is, the node with
        the smallest value greater than the node. We know it exists because
        otherwise this would be case 1 or case 2, and it cannot have a Smaller
        value because that would be the node itself. The successor can
        therefore be detached and can be used to replace the node. */

        /* Find the inorder successor. */
        ItemStruct* b = item->bigger_;
        while (b->smaller_ != nullptr) b = b->smaller_;

        /* Detach the successor. */
        if (b->parent_ != nullptr) {
            if (b->parent_->bigger_ == b) {
                b->parent_->bigger_ = b->bigger_;
            }
            else {
                b->parent_->smaller_ = b->bigger_;
            }
        }

        if (b->bigger_ != nullptr) b->bigger_->parent_ = b->parent_;

        /* Replace the node with the successor. */
        if (item->parent_ != nullptr) {
            if (item->parent_->smaller_ == item) {
                item->parent_->smaller_ = b;
            }
            else {
                item->parent_->bigger_ = b;
            }
        }
        else {
            data->item_tree_ = b;
        }

        b->parent_ = item->parent_;
        b->smaller_ = item->smaller_;

        if (b->smaller_ != nullptr) b->smaller_->parent_ = b;

        b->bigger_ = item->bigger_;

        if (b->bigger_ != nullptr) b->bigger_->parent_ = b;
    }
}

/* Delete the entire ItemTree. */
void DefragLib::delete_item_tree(ItemStruct* top) {
    if (top == nullptr) return;
    if (top->smaller_ != nullptr) delete_item_tree(top->smaller_);
    if (top->bigger_ != nullptr) delete_item_tree(top->bigger_);

    if (top->short_path_ != nullptr &&
        (top->long_path_ == nullptr ||
            top->short_path_ != top->long_path_)) {
        free(top->short_path_);

        top->short_path_ = nullptr;
    }

    if (top->short_filename_ != nullptr &&
        (top->long_filename_ == nullptr ||
            top->short_filename_ != top->long_filename_)) {
        free(top->short_filename_);

        top->short_filename_ = nullptr;
    }

    if (top->long_path_ != nullptr) free(top->long_path_);
    if (top->long_filename_ != nullptr) free(top->long_filename_);

    while (top->fragments_ != nullptr) {
        FragmentListStruct* fragment = top->fragments_->next_;

        free(top->fragments_);

        top->fragments_ = fragment;
    }

    free(top);
}

/*

Return the LCN of the fragment that contains a cluster at the LCN. If the
item has no fragment that occupies the LCN then return zero.

*/
uint64_t DefragLib::find_fragment_begin(const ItemStruct* item, const uint64_t lcn) {
    /* Sanity check. */
    if (item == nullptr || lcn == 0) return 0;

    /* Walk through all the fragments of the item. If a fragment is found
    that contains the LCN then return the begin of that fragment. */
    uint64_t vcn = 0;
    for (const FragmentListStruct* fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            if (lcn >= fragment->lcn_ &&
                lcn < fragment->lcn_ + fragment->next_vcn_ - vcn) {
                return fragment->lcn_;
            }
        }

        vcn = fragment->next_vcn_;
    }

    /* Not found: return zero. */
    return 0;
}

/*

Search the list for the item that occupies the cluster at the LCN. Return a
pointer to the item. If not found then return nullptr.

*/
ItemStruct* DefragLib::find_item_at_lcn(const DefragDataStruct* data, const uint64_t lcn) {
    /* Locate the item by descending the sorted tree in memory. If found then
    return the item. */
    ItemStruct* item = data->item_tree_;

    while (item != nullptr) {
        const uint64_t item_lcn = get_item_lcn(item);

        if (item_lcn == lcn) return item;

        if (lcn < item_lcn) {
            item = item->smaller_;
        }
        else {
            item = item->bigger_;
        }
    }

    /* Walk through all the fragments of all the items in the sorted tree. If a
    fragment is found that occupies the LCN then return a pointer to the item. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (find_fragment_begin(item, lcn) != 0) return item;
    }

    /* LCN not found, return nullptr. */
    return nullptr;
}

/*

Open the item as a file or as a directory. If the item could not be
opened then show an error message and return nullptr.

*/
HANDLE DefragLib::open_item_handle(const DefragDataStruct* data, const ItemStruct* item) const {
    HANDLE file_handle;
    WCHAR error_string[BUFSIZ];
    const size_t length = wcslen(item->long_path_) + 5;
    auto path = (WCHAR*)malloc(sizeof(WCHAR) * length);

    swprintf_s(path, length, L"\\\\?\\%s", item->long_path_);

    if (item->is_dir_ == false) {
        file_handle = CreateFileW(path,FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,OPEN_EXISTING,FILE_FLAG_NO_BUFFERING, nullptr);
    }
    else {
        file_handle = CreateFileW(path,GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    }

    free(path);

    if (file_handle != INVALID_HANDLE_VALUE) return file_handle;

    /* Show error message: "Could not open '%s': %s" */
    system_error_str(GetLastError(), error_string,BUFSIZ);

    DefragGui* jkGui = DefragGui::get_instance();

    jkGui->show_debug(DebugLevel::DetailedFileInfo, nullptr, data->debug_msg_[15], item->long_path_, error_string);

    return nullptr;
}

/*

Analyze an item (file, directory) and update it's Clusters and Fragments
in memory. If there was an error then return false, otherwise return true.
Note: Very small files are stored by Windows in the MFT and have no
clusters (zero) and no fragments (nullptr).

*/
int DefragLib::get_fragments(const DefragDataStruct* data, ItemStruct* item, const HANDLE file_handle) const {
    STARTING_VCN_INPUT_BUFFER RetrieveParam;

    struct {
        uint32_t extent_count_;
        uint64_t starting_vcn_;

        struct {
            uint64_t next_vcn_;
            uint64_t lcn_;
        } extents_[1000];
    } extent_data{};

    BY_HANDLE_FILE_INFORMATION file_information;
    FragmentListStruct* last_fragment;
    uint32_t error_code;
    DWORD w;
    DefragGui* gui = DefragGui::get_instance();

    /* Initialize. If the item has an old list of fragments then delete it. */
    item->clusters_count_ = 0;

    while (item->fragments_ != nullptr) {
        last_fragment = item->fragments_->next_;

        free(item->fragments_);

        item->fragments_ = last_fragment;
    }

    /* Fetch the date/times of the file. */
    if (item->creation_time_ == 0 &&
        item->last_access_time_ == 0 &&
        item->mft_change_time_ == 0 &&
        GetFileInformationByHandle(file_handle, &file_information) != 0) {
        ULARGE_INTEGER u;
        u.LowPart = file_information.ftCreationTime.dwLowDateTime;
        u.HighPart = file_information.ftCreationTime.dwHighDateTime;

        item->creation_time_ = u.QuadPart;

        u.LowPart = file_information.ftLastAccessTime.dwLowDateTime;
        u.HighPart = file_information.ftLastAccessTime.dwHighDateTime;

        item->last_access_time_ = u.QuadPart;

        u.LowPart = file_information.ftLastWriteTime.dwLowDateTime;
        u.HighPart = file_information.ftLastWriteTime.dwHighDateTime;

        item->mft_change_time_ = u.QuadPart;
    }

    /* Show debug message: "Getting cluster bitmap: %s" */
    gui->show_debug(DebugLevel::DetailedFileInfo, nullptr, data->debug_msg_[10], item->long_path_);

    /* Ask Windows for the clustermap of the item and save it in memory.
    The buffer that is used to ask Windows for the clustermap has a
    fixed size, so we may have to loop a couple of times. */
    uint64_t vcn = 0;
    int max_loop = 1000;
    last_fragment = nullptr;

    do {
        /* I strongly suspect that the FSCTL_GET_RETRIEVAL_POINTERS system call
        can sometimes return an empty bitmap and ERROR_MORE_DATA. That's not
        very nice of Microsoft, because it causes an infinite loop. I've
        therefore added a loop counter that will limit the loop to 1000
        iterations. This means the defragger cannot handle files with more
        than 100000 fragments, though. */
        if (max_loop <= 0) {
            gui->show_debug(DebugLevel::Progress, nullptr, L"FSCTL_GET_RETRIEVAL_POINTERS error: Infinite loop");

            return false;
        }

        max_loop = max_loop - 1;

        /* Ask Windows for the (next segment of the) clustermap of this file. If error
        then leave the loop. */
        RetrieveParam.StartingVcn.QuadPart = vcn;

        error_code = DeviceIoControl(file_handle,FSCTL_GET_RETRIEVAL_POINTERS,
                                     &RetrieveParam, sizeof RetrieveParam, &extent_data, sizeof extent_data, &w,
                                     nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        }
        else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) break;

        /* Walk through the clustermap, count the total number of clusters, and
        save all fragments in memory. */
        for (uint32_t i = 0; i < extent_data.extent_count_; i++) {
            /* Show debug message. */
            if (extent_data.extents_[i].lcn_ != VIRTUALFRAGMENT) {
                /* "Extent: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u" */
                gui->show_debug(DebugLevel::DetailedFileInfo, nullptr, data->debug_msg_[11],
                                extent_data.extents_[i].lcn_,
                                vcn,
                                extent_data.extents_[i].next_vcn_);
            }
            else {
                /* "Extent (virtual): Vcn=%I64u, NextVcn=%I64u" */
                gui->show_debug(DebugLevel::DetailedFileInfo, nullptr, data->debug_msg_[46], vcn,
                                extent_data.extents_[i].next_vcn_);
            }

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */
            if (extent_data.extents_[i].lcn_ != VIRTUALFRAGMENT) {
                item->clusters_count_ = item->clusters_count_ + extent_data.extents_[i].next_vcn_ - vcn;
            }

            /* Add the fragment to the Fragments. */

            if (const auto new_fragment = (FragmentListStruct*)malloc(sizeof(FragmentListStruct)); new_fragment !=
                nullptr) {
                new_fragment->lcn_ = extent_data.extents_[i].lcn_;
                new_fragment->next_vcn_ = extent_data.extents_[i].next_vcn_;
                new_fragment->next_ = nullptr;

                if (item->fragments_ == nullptr) {
                    item->fragments_ = new_fragment;
                }
                else {
                    if (last_fragment != nullptr) last_fragment->next_ = new_fragment;
                }

                last_fragment = new_fragment;
            }

            /* The Vcn of the next fragment is the NextVcn field in this record. */
            vcn = extent_data.extents_[i].next_vcn_;
        }

        /* Loop until we have processed the entire clustermap of the file. */
    }
    while (error_code == ERROR_MORE_DATA);

    /* If there was an error while reading the clustermap then return false. */
    if (error_code != NO_ERROR && error_code != ERROR_HANDLE_EOF) {
        WCHAR error_string[BUFSIZ];
        /* Show debug message: "Cannot process clustermap of '%s': %s" */
        system_error_str(error_code, error_string,BUFSIZ);

        gui->show_debug(DebugLevel::DetailedProgress, nullptr, data->debug_msg_[43], item->long_path_, error_string);

        return false;
    }

    return true;
}

/* Return the number of fragments in the item. */
int DefragLib::get_fragment_count(const ItemStruct* item) {
    int fragments = 0;
    uint64_t vcn = 0;
    uint64_t next_lcn = 0;

    for (auto fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            if (next_lcn != 0 && fragment->lcn_ != next_lcn) fragments++;

            next_lcn = fragment->lcn_ + fragment->next_vcn_ - vcn;
        }

        vcn = fragment->next_vcn_;
    }

    if (next_lcn != 0) fragments++;

    return fragments;
}

/*

Return true if the block in the item starting at Offset with Size clusters
is fragmented, otherwise return false.
Note: this function does not ask Windows for a fresh list of fragments,
it only looks at cached information in memory.

*/
bool DefragLib::is_fragmented(const ItemStruct* item, const uint64_t offset, const uint64_t size) {
    /* Walk through all fragments. If a fragment is found where either the
    begin or the end of the fragment is inside the block then the file is
    fragmented and return true. */
    uint64_t fragment_begin = 0;
    uint64_t fragment_end = 0;
    uint64_t vcn = 0;
    uint64_t next_lcn = 0;
    const FragmentListStruct* fragment = item->fragments_;

    while (fragment != nullptr) {
        /* Virtual fragments do not occupy space on disk and do not count as fragments. */
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            /* Treat aligned fragments as a single fragment. Windows will frequently
            split files in fragments even though they are perfectly aligned on disk,
            especially system files and very large files. The defragger treats these
            files as unfragmented. */
            if (next_lcn != 0 && fragment->lcn_ != next_lcn) {
                /* If the fragment is above the block then return false, the block is
                not fragmented and we don't have to scan any further. */
                if (fragment_begin >= offset + size) return false;

                /* If the first cluster of the fragment is above the first cluster of
                the block, or the last cluster of the fragment is before the last
                cluster of the block, then the block is fragmented, return true. */
                if (fragment_begin > offset ||
                    (fragment_end - 1 >= offset &&
                        fragment_end - 1 < offset + size - 1)) {
                    return true;
                }

                fragment_begin = fragment_end;
            }

            fragment_end = fragment_end + fragment->next_vcn_ - vcn;
            next_lcn = fragment->lcn_ + fragment->next_vcn_ - vcn;
        }

        /* Next fragment. */
        vcn = fragment->next_vcn_;
        fragment = fragment->next_;
    }

    /* Handle the last fragment. */
    if (fragment_begin >= offset + size) return false;

    if (fragment_begin > offset ||
        (fragment_end - 1 >= offset &&
            fragment_end - 1 < offset + size - 1)) {
        return true;
    }

    /* Return false, the item is not fragmented inside the block. */
    return false;
}

/**
 * \brief Colorize an item (file, directory) on the screen in the proper color
 * (fragmented, unfragmented, unmovable, empty). If specified then highlight
 * part of the item. If Undraw=true then remove the item from the screen.
 * Note: the offset and size of the highlight block is in absolute clusters, not virtual clusters.
 * \param data 
 * \param item 
 * \param busy_offset Number of first cluster to be highlighted. 
 * \param busy_size Number of clusters to be highlighted.
 * \param un_draw true to undraw the file from the screen.
 */
void DefragLib::colorize_item(DefragDataStruct* data, const ItemStruct* item, const uint64_t busy_offset,
                              const uint64_t busy_size, const int un_draw) const {
    int color;

    DefragGui* gui = DefragGui::get_instance();

    /* Determine if the item is fragmented. */
    const bool is_fragmented = this->is_fragmented(item, 0, item->clusters_count_);

    /* Walk through all the fragments of the file. */
    uint64_t vcn = 0;
    uint64_t real_vcn = 0;

    const FragmentListStruct* fragment = item->fragments_;

    while (fragment != nullptr) {
        /*
        Ignore virtual fragments. They do not occupy space on disk and do not require colorization.
        */
        if (fragment->lcn_ == VIRTUALFRAGMENT) {
            vcn = fragment->next_vcn_;
            fragment = fragment->next_;

            continue;
        }

        /*
        Walk through all the segments of the file. A segment is usually
        the same as a fragment, but if a fragment spans across a boundary
        then we must determine the color of the left and right parts
        individually. So we pretend the fragment is divided into segments
        at the various possible boundaries.
        */
        uint64_t segment_begin = real_vcn;

        while (segment_begin < real_vcn + fragment->next_vcn_ - vcn) {
            uint64_t segment_end = real_vcn + fragment->next_vcn_ - vcn;

            /* Determine the color with which to draw this segment. */
            if (un_draw == false) {
                color = DefragStruct::COLORUNFRAGMENTED;

                if (item->is_hog_ == true) color = DefragStruct::COLORSPACEHOG;
                if (is_fragmented == true) color = DefragStruct::COLORFRAGMENTED;
                if (item->is_unmovable_ == true) color = DefragStruct::COLORUNMOVABLE;
                if (item->is_excluded_ == true) color = DefragStruct::COLORUNMOVABLE;

                if (vcn + segment_begin - real_vcn < busy_offset &&
                    vcn + segment_end - real_vcn > busy_offset) {
                    segment_end = real_vcn + busy_offset - vcn;
                }

                if (vcn + segment_begin - real_vcn >= busy_offset &&
                    vcn + segment_begin - real_vcn < busy_offset + busy_size) {
                    if (vcn + segment_end - real_vcn > busy_offset + busy_size) {
                        segment_end = real_vcn + busy_offset + busy_size - vcn;
                    }

                    color = DefragStruct::COLORBUSY;
                }
            }
            else {
                color = DefragStruct::COLOREMPTY;

                for (int i = 0; i < 3; i++) {
                    if (fragment->lcn_ + segment_begin - real_vcn < data->mft_excludes_[i].start_ &&
                        fragment->lcn_ + segment_end - real_vcn > data->mft_excludes_[i].start_) {
                        segment_end = real_vcn + data->mft_excludes_[i].start_ - fragment->lcn_;
                    }

                    if (fragment->lcn_ + segment_begin - real_vcn >= data->mft_excludes_[i].start_ &&
                        fragment->lcn_ + segment_begin - real_vcn < data->mft_excludes_[i].end_) {
                        if (fragment->lcn_ + segment_end - real_vcn > data->mft_excludes_[i].end_) {
                            segment_end = real_vcn + data->mft_excludes_[i].end_ - fragment->lcn_;
                        }

                        color = DefragStruct::COLORMFT;
                    }
                }
            }

            /* Colorize the segment. */
            gui->draw_cluster(data, fragment->lcn_ + segment_begin - real_vcn, fragment->lcn_ + segment_end - real_vcn,
                              color);

            /* Next segment. */
            segment_begin = segment_end;
        }

        /* Next fragment. */
        real_vcn = real_vcn + fragment->next_vcn_ - vcn;

        vcn = fragment->next_vcn_;
        fragment = fragment->next_;
    }
}

/*

Show a map on the screen of all the clusters on disk. The map shows
which clusters are free and which are in use.
The Data->RedrawScreen flag controls redrawing of the screen. It is set
to "2" (busy) when the subroutine starts. If another thread changes it to
"1" (request) while the subroutine is busy then it will immediately exit
without completing the redraw. When redrawing is completely finished the
flag is set to "0" (no). */
/*
void ShowDiskmap2(struct DefragDataStruct *Data) {
	struct ItemStruct *Item;
	STARTING_LCN_INPUT_BUFFER BitmapParam;
	struct {
		uint64_t StartingLcn;
		uint64_t BitmapSize;
		BYTE Buffer[65536];               / * Most efficient if binary multiple. * /
	} BitmapData;
	uint64_t Lcn;
	uint64_t ClusterStart;
	uint32_t ErrorCode;
	int Index;
	int IndexMax;
	BYTE Mask;
	int InUse;
	int PrevInUse;
	uint32_t w;
	int i;

	*Data->RedrawScreen = 2;                       / * Set the flag to "busy". * /

	/ * Exit if the library is not processing a disk yet. * /
	if (Data->Disk.VolumeHandle == nullptr) {
		*Data->RedrawScreen = 0;                       / * Set the flag to "no". * /
		return;
	}

	/ * Clear screen. * /
	m_jkGui->ClearScreen(nullptr);

	/ * Show the map of all the clusters in use. * /
	Lcn = 0;
	ClusterStart = 0;
	PrevInUse = 1;
	do {
		if (*Data->Running != RUNNING) break;
		if (*Data->RedrawScreen != 2) break;
		if (Data->Disk.VolumeHandle == INVALID_HANDLE_VALUE) break;

		/ * Fetch a block of cluster data. * /
		BitmapParam.StartingLcn.QuadPart = Lcn;
		ErrorCode = DeviceIoControl(Data->Disk.VolumeHandle,FSCTL_GET_VOLUME_BITMAP,
			&BitmapParam,sizeof(BitmapParam),&BitmapData,sizeof(BitmapData),&w,nullptr);
		if (ErrorCode != 0) {
			ErrorCode = NO_ERROR;
		} else {
			ErrorCode = GetLastError();
		}
		if ((ErrorCode != NO_ERROR) && (ErrorCode != ERROR_MORE_DATA)) break;

		/ * Sanity check. * /
		if (Lcn >= BitmapData.StartingLcn + BitmapData.BitmapSize) break;

		/ * Analyze the clusterdata. We resume where the previous block left off. * /
		Lcn = BitmapData.StartingLcn;
		Index = 0;
		Mask = 1;
		IndexMax = sizeof(BitmapData.Buffer);
		if (BitmapData.BitmapSize / 8 < IndexMax) IndexMax = (int)(BitmapData.BitmapSize / 8);
		while ((Index < IndexMax) && (*Data->Running == RUNNING)) {
			InUse = (BitmapData.Buffer[Index] & Mask);
			/ * If at the beginning of the disk then copy the InUse value as our
			starting value. * /
			if (Lcn == 0) PrevInUse = InUse;
			/ * At the beginning and end of an Exclude draw the cluster. * /
			if ((Lcn == Data->MftExcludes[0].Start) || (Lcn == Data->MftExcludes[0].End) ||
				(Lcn == Data->MftExcludes[1].Start) || (Lcn == Data->MftExcludes[1].End) ||
				(Lcn == Data->MftExcludes[2].Start) || (Lcn == Data->MftExcludes[2].End)) {
					if ((Lcn == Data->MftExcludes[0].End) ||
						(Lcn == Data->MftExcludes[1].End) ||
						(Lcn == Data->MftExcludes[2].End)) {
							m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::COLORUNMOVABLE);
					} else if (PrevInUse == 0) {
						m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::COLOREMPTY);
					} else {
						m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::COLORALLOCATED);
					}
					InUse = 1;
					PrevInUse = 1;
					ClusterStart = Lcn;
			}
			if ((PrevInUse == 0) && (InUse != 0)) {          / * Free * /
				m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::COLOREMPTY);
				ClusterStart = Lcn;
			}
			if ((PrevInUse != 0) && (InUse == 0)) {          / * In use * /
				m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::COLORALLOCATED);
				ClusterStart = Lcn;
			}
			PrevInUse = InUse;
			if (Mask == 128) {
				Mask = 1;
				Index = Index + 1;
			} else {
				Mask = Mask << 1;
			}
			Lcn = Lcn + 1;
		}

	} while ((ErrorCode == ERROR_MORE_DATA) &&
		(Lcn < BitmapData.StartingLcn + BitmapData.BitmapSize));

	if ((Lcn > 0) && (*Data->RedrawScreen == 2)) {
		if (PrevInUse == 0) {          / * Free * /
			m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::COLOREMPTY);
		}
		if (PrevInUse != 0) {          / * In use * /
			m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::COLORALLOCATED);
		}
	}

	/ * Show the MFT zones. * /
	for (i = 0; i < 3; i++) {
		if (*Data->RedrawScreen != 2) break;
		if (Data->MftExcludes[i].Start <= 0) continue;
		m_jkGui->DrawCluster(Data,Data->MftExcludes[i].Start,Data->MftExcludes[i].End,JKDefragStruct::COLORMFT);
	}

	/ * Colorize all the files on the screen.
	Note: the "$BadClus" file on NTFS disks maps the entire disk, so we have to
	ignore it. * /
	for (Item = TreeSmallest(Data->ItemTree); Item != nullptr; Item = TreeNext(Item)) {
		if (*Data->Running != RUNNING) break;
		if (*Data->RedrawScreen != 2) break;
		if ((Item->LongFilename != nullptr) &&
			((_wcsicmp(Item->LongFilename,L"$BadClus") == 0) ||
			(_wcsicmp(Item->LongFilename,L"$BadClus:$Bad:$DATA") == 0))) continue;
		ColorizeItem(Data,Item,0,0,false);
	}

	/ * Set the flag to "no". * /
	if (*Data->RedrawScreen == 2) *Data->RedrawScreen = 0;
}
*/

/**
 * \brief Look for a gap, a block of empty clusters on the volume.
 * \param minimum_lcn Start scanning for gaps at this location. If there is a gap at this location then return it. Zero is the begin of the disk.
 * \param maximum_lcn Stop scanning for gaps at this location. Zero is the end of the disk.
 * \param minimum_size The gap must have at least this many contiguous free clusters. Zero will match any gap, so will return the first gap at or above MinimumLcn.
 * \param must_fit if true then only return a gap that is bigger/equal than the MinimumSize. If false then return a gap bigger/equal than MinimumSize,
 *      or if no such gap is found return the largest gap on the volume (above MinimumLcn).
 * \param find_highest_gap if false then return the lowest gap that is bigger/equal than the MinimumSize. If true then return the highest gap.
 * \param begin_lcn out: LCN of begin of cluster
 * \param end_lcn out: LCN of end of cluster
 * \param ignore_mft_excludes 
 * \return true if succes, false if no gap was found or an error occurred. The routine asks Windows for the cluster bitmap every time. It would be
 *  faster to cache the bitmap in memory, but that would cause more fails because of stale information.
 */
bool DefragLib::find_gap(const DefragDataStruct* data, const uint64_t minimum_lcn, uint64_t maximum_lcn,
                         const uint64_t minimum_size,
                         const int must_fit, const bool find_highest_gap, uint64_t* begin_lcn, uint64_t* end_lcn,
                         const BOOL ignore_mft_excludes) const {
    STARTING_LCN_INPUT_BUFFER bitmap_param;
    struct {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        BYTE buffer_[65536]; /* Most efficient if binary multiple. */
    } bitmap_data{};

    uint32_t error_code;
    DWORD w;
    DefragGui* gui = DefragGui::get_instance();

    /* Sanity check. */
    if (minimum_lcn >= data->total_clusters_) return false;

    /* Main loop to walk through the entire clustermap. */
    uint64_t lcn = minimum_lcn;
    uint64_t cluster_start = 0;
    int prev_in_use = 1;
    uint64_t highest_begin_lcn = 0;
    uint64_t highest_end_lcn = 0;
    uint64_t largest_begin_lcn = 0;
    uint64_t largest_end_lcn = 0;

    do {
        /* Fetch a block of cluster data. If error then return false. */
        bitmap_param.StartingLcn.QuadPart = lcn;
        error_code = DeviceIoControl(data->disk_.volume_handle_,FSCTL_GET_VOLUME_BITMAP,
                                     &bitmap_param, sizeof bitmap_param, &bitmap_data, sizeof bitmap_data, &w, nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        }
        else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) {
            WCHAR s1[BUFSIZ];
            /* Show debug message: "ERROR: could not get volume bitmap: %s" */
            system_error_str(GetLastError(), s1,BUFSIZ);

            gui->show_debug(DebugLevel::Warning, nullptr, data->debug_msg_[12], s1);

            return false;
        }

        /* Sanity check. */
        if (lcn >= bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_) return false;
        if (maximum_lcn == 0) maximum_lcn = bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_;

        /* Analyze the clusterdata. We resume where the previous block left off. If a cluster is found that matches the criteria then return it's LCN (Logical Cluster Number). */
        lcn = bitmap_data.starting_lcn_;
        int index = 0;
        BYTE mask = 1;

        int index_max = sizeof bitmap_data.buffer_;

        if (bitmap_data.bitmap_size_ / 8 < index_max) index_max = (int)(bitmap_data.bitmap_size_ / 8);

        while (index < index_max && lcn < maximum_lcn) {
            if (lcn >= minimum_lcn) {
                int in_use = bitmap_data.buffer_[index] & mask;

                if ((lcn >= data->mft_excludes_[0].start_ && lcn < data->mft_excludes_[0].end_) ||
                    (lcn >= data->mft_excludes_[1].start_ && lcn < data->mft_excludes_[1].end_) ||
                    (lcn >= data->mft_excludes_[2].start_ && lcn < data->mft_excludes_[2].end_)) {
                    if (ignore_mft_excludes == FALSE) in_use = 1;
                }

                if (prev_in_use == 0 && in_use != 0) {
                    /* Show debug message: "Gap found: LCN=%I64d, Size=%I64d" */
                    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, data->debug_msg_[13], cluster_start,
                                    lcn - cluster_start);

                    /* If the gap is bigger/equal than the mimimum size then return it,
                    or remember it, depending on the FindHighestGap parameter. */
                    if (cluster_start >= minimum_lcn &&
                        lcn - cluster_start >= minimum_size) {
                        if (find_highest_gap == false) {
                            if (begin_lcn != nullptr) *begin_lcn = cluster_start;

                            if (end_lcn != nullptr) *end_lcn = lcn;

                            return true;
                        }

                        highest_begin_lcn = cluster_start;
                        highest_end_lcn = lcn;
                    }

                    /* Remember the largest gap on the volume. */
                    if (largest_begin_lcn == 0 ||
                        largest_end_lcn - largest_begin_lcn < lcn - cluster_start) {
                        largest_begin_lcn = cluster_start;
                        largest_end_lcn = lcn;
                    }
                }

                if (prev_in_use != 0 && in_use == 0) cluster_start = lcn;

                prev_in_use = in_use;
            }

            if (mask == 128) {
                mask = 1;
                index = index + 1;
            }
            else {
                mask = mask << 1;
            }

            lcn = lcn + 1;
        }
    }
    while (error_code == ERROR_MORE_DATA &&
        lcn < bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_ &&
        lcn < maximum_lcn);

    /* Process the last gap. */
    if (prev_in_use == 0) {
        /* Show debug message: "Gap found: LCN=%I64d, Size=%I64d" */
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, data->debug_msg_[13], cluster_start,
                        lcn - cluster_start);

        if (cluster_start >= minimum_lcn && lcn - cluster_start >= minimum_size) {
            if (find_highest_gap == false) {
                if (begin_lcn != nullptr) *begin_lcn = cluster_start;
                if (end_lcn != nullptr) *end_lcn = lcn;

                return true;
            }

            highest_begin_lcn = cluster_start;
            highest_end_lcn = lcn;
        }

        /* Remember the largest gap on the volume. */
        if (largest_begin_lcn == 0 ||
            largest_end_lcn - largest_begin_lcn < lcn - cluster_start) {
            largest_begin_lcn = cluster_start;
            largest_end_lcn = lcn;
        }
    }

    /* If the FindHighestGap flag is true then return the highest gap we have found. */
    if (find_highest_gap == true && highest_begin_lcn != 0) {
        if (begin_lcn != nullptr) *begin_lcn = highest_begin_lcn;
        if (end_lcn != nullptr) *end_lcn = highest_end_lcn;

        return true;
    }

    /* If the MustFit flag is false then return the largest gap we have found. */
    if (must_fit == false && largest_begin_lcn != 0) {
        if (begin_lcn != nullptr) *begin_lcn = largest_begin_lcn;
        if (end_lcn != nullptr) *end_lcn = largest_end_lcn;

        return true;
    }

    /* No gap found, return false. */
    return false;
}

/*

Calculate the begin of the 3 zones.
Unmovable files pose an interesting problem. Suppose an unmovable file is in
zone 1, then the calculation for the beginning of zone 2 must count that file.
But that changes the beginning of zone 2. Some unmovable files may now suddenly
be in another zone. So we have to recalculate, which causes another border
change, and again, and again....
Note: the program only knows if a file is unmovable after it has tried to move a
file. So we have to recalculate the beginning of the zones every time we encounter
an unmovable file.

*/
void DefragLib::calculate_zones(DefragDataStruct* data) {
    ItemStruct* item;
    uint64_t size_of_movable_files[3];
    uint64_t size_of_unmovable_fragments[3];
    uint64_t zone_end[3];
    uint64_t old_zone_end[3];
    int zone;
    int i;
    DefragGui* gui = DefragGui::get_instance();

    /* Calculate the number of clusters in movable items for every zone. */
    for (zone = 0; zone <= 2; zone++) size_of_movable_files[zone] = 0;

    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (item->is_unmovable_ == true) continue;
        if (item->is_excluded_ == true) continue;
        if (item->is_dir_ == true && data->cannot_move_dirs_ > 20) continue;

        zone = 1;

        if (item->is_hog_ == true) zone = 2;
        if (item->is_dir_ == true) zone = 0;

        size_of_movable_files[zone] = size_of_movable_files[zone] + item->clusters_count_;
    }

    /* Iterate until the calculation does not change anymore, max 10 times. */
    for (zone = 0; zone <= 2; zone++) size_of_unmovable_fragments[zone] = 0;

    for (zone = 0; zone <= 2; zone++) old_zone_end[zone] = 0;

    for (int iterate = 1; iterate <= 10; iterate++) {
        /* Calculate the end of the zones. */
        zone_end[0] = size_of_movable_files[0] + size_of_unmovable_fragments[0] +
                (uint64_t)(data->total_clusters_ * data->free_space_ / 100.0);

        zone_end[1] = zone_end[0] + size_of_movable_files[1] + size_of_unmovable_fragments[1] +
                (uint64_t)(data->total_clusters_ * data->free_space_ / 100.0);

        zone_end[2] = zone_end[1] + size_of_movable_files[2] + size_of_unmovable_fragments[2];

        /* Exit if there was no change. */
        if (old_zone_end[0] == zone_end[0] &&
            old_zone_end[1] == zone_end[1] &&
            old_zone_end[2] == zone_end[2])
            break;

        for (zone = 0; zone <= 2; zone++) old_zone_end[zone] = zone_end[zone];

        /* Show debug info. */
        gui->show_debug(DebugLevel::DetailedFileInfo, nullptr,
                        L"Zone calculation, iteration %u: 0 - %I64d - %I64d - %I64d", iterate,
                        zone_end[0], zone_end[1], zone_end[2]);

        /* Reset the SizeOfUnmovableFragments array. We are going to (re)calculate these numbers
        based on the just calculates ZoneEnd's. */
        for (zone = 0; zone <= 2; zone++) size_of_unmovable_fragments[zone] = 0;

        /* The MFT reserved areas are counted as unmovable data. */
        for (i = 0; i < 3; i++) {
            if (data->mft_excludes_[i].start_ < zone_end[0]) {
                size_of_unmovable_fragments[0] = size_of_unmovable_fragments[0]
                        + data->mft_excludes_[i].end_ - data->mft_excludes_[i].start_;
            }
            else if (data->mft_excludes_[i].start_ < zone_end[1]) {
                size_of_unmovable_fragments[1] = size_of_unmovable_fragments[1]
                        + data->mft_excludes_[i].end_ - data->mft_excludes_[i].start_;
            }
            else if (data->mft_excludes_[i].start_ < zone_end[2]) {
                size_of_unmovable_fragments[2] = size_of_unmovable_fragments[2]
                        + data->mft_excludes_[i].end_ - data->mft_excludes_[i].start_;
            }
        }

        /* Walk through all items and count the unmovable fragments. Ignore unmovable fragments
        in the MFT zones, we have already counted the zones. */
        for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
            if (item->is_unmovable_ == false &&
                item->is_excluded_ == false &&
                (item->is_dir_ == false || data->cannot_move_dirs_ <= 20))
                continue;

            uint64_t vcn = 0;
            uint64_t real_vcn = 0;

            for (const FragmentListStruct* Fragment = item->fragments_; Fragment != nullptr; Fragment = Fragment->
                 next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if ((Fragment->lcn_ < data->mft_excludes_[0].start_ || Fragment->lcn_ >= data->mft_excludes_[0].
                            end_)
                        &&
                        (Fragment->lcn_ < data->mft_excludes_[1].start_ || Fragment->lcn_ >= data->mft_excludes_[1].
                            end_)
                        &&
                        (Fragment->lcn_ < data->mft_excludes_[2].start_ || Fragment->lcn_ >= data->mft_excludes_[2].
                            end_)) {
                        if (Fragment->lcn_ < zone_end[0]) {
                            size_of_unmovable_fragments[0] = size_of_unmovable_fragments[0] + Fragment->next_vcn_ - vcn;
                        }
                        else if (Fragment->lcn_ < zone_end[1]) {
                            size_of_unmovable_fragments[1] = size_of_unmovable_fragments[1] + Fragment->next_vcn_ - vcn;
                        }
                        else if (Fragment->lcn_ < zone_end[2]) {
                            size_of_unmovable_fragments[2] = size_of_unmovable_fragments[2] + Fragment->next_vcn_ - vcn;
                        }
                    }

                    real_vcn = real_vcn + Fragment->next_vcn_ - vcn;
                }

                vcn = Fragment->next_vcn_;
            }
        }
    }

    /* Calculated the begin of the zones. */
    data->zones_[0] = 0;

    for (i = 1; i <= 3; i++) data->zones_[i] = zone_end[i - 1];
}

/**
 * \brief Subfunction for MoveItem(), see below. Move (part of) an item to a new location on disk. Return errorcode from DeviceIoControl().
 * The file is moved in a single FSCTL_MOVE_FILE call. If the file has fragments then Windows will join them up.
 * Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to.
 * \param offset Number of first cluster to be moved
 * \param size 
 * \return 
 */
uint32_t DefragLib::move_item1(DefragDataStruct* data, const HANDLE file_handle, const ItemStruct* item,
                               const uint64_t new_lcn, const uint64_t offset,
                               const uint64_t size) const
/* Number of clusters to be moved. */
{
    MOVE_FILE_DATA move_params;
    FragmentListStruct* fragment;
    uint64_t lcn;
    DWORD w;
    DefragGui* gui = DefragGui::get_instance();

    /* Find the first fragment that contains clusters inside the block, so we
    can translate the absolute cluster number of the block into the virtual
    cluster number used by Windows. */
    uint64_t vcn = 0;
    uint64_t real_vcn = 0;

    for (fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            if (real_vcn + fragment->next_vcn_ - vcn - 1 >= offset) break;

            real_vcn = real_vcn + fragment->next_vcn_ - vcn;
        }

        vcn = fragment->next_vcn_;
    }

    /* Setup the parameters for the move. */
    move_params.FileHandle = file_handle;
    move_params.StartingLcn.QuadPart = new_lcn;
    move_params.StartingVcn.QuadPart = vcn + (offset - real_vcn);
    move_params.ClusterCount = (uint32_t)size;

    if (fragment == nullptr) {
        lcn = 0;
    }
    else {
        lcn = fragment->lcn_ + (offset - real_vcn);
    }

    /* Show progress message. */
    gui->show_move(item, move_params.ClusterCount, lcn, new_lcn, move_params.StartingVcn.QuadPart);

    /* Draw the item and the destination clusters on the screen in the BUSY	color. */
    colorize_item(data, item, move_params.StartingVcn.QuadPart, move_params.ClusterCount, false);

    gui->draw_cluster(data, new_lcn, new_lcn + size, DefragStruct::COLORBUSY);

    /* Call Windows to perform the move. */
    uint32_t error_code = DeviceIoControl(data->disk_.volume_handle_,FSCTL_MOVE_FILE, &move_params,
                                          sizeof move_params, nullptr, 0, &w, nullptr);

    if (error_code != 0) {
        error_code = NO_ERROR;
    }
    else {
        error_code = GetLastError();
    }

    /* Update the PhaseDone counter for the progress bar. */
    data->phase_done_ = data->phase_done_ + move_params.ClusterCount;

    /* Undraw the destination clusters on the screen. */
    gui->draw_cluster(data, new_lcn, new_lcn + size, DefragStruct::COLOREMPTY);

    return error_code;
}

/*


*/
/**
 * \brief Subfunction for MoveItem(), see below. Move (part of) an item to a new location on disk. Return errorcode from DeviceIoControl().
 * Move the item one fragment at a time, a FSCTL_MOVE_FILE call per fragment. The fragments will be lined up on disk and the defragger will treat the
 * item as unfragmented. Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to
 * \param offset Number of first cluster to be moved
 * \param size Number of clusters to be moved
 * \return 
 */
uint32_t DefragLib::move_item2(DefragDataStruct* data, const HANDLE file_handle, const ItemStruct* item,
                               const uint64_t new_lcn, const uint64_t offset, const uint64_t size) const {
    MOVE_FILE_DATA move_params;
    uint64_t from_lcn;
    DWORD w;
    DefragGui* gui = DefragGui::get_instance();

    /* Walk through the fragments of the item and move them one by one to the new location. */
    uint32_t error_code = NO_ERROR;
    uint64_t vcn = 0;
    uint64_t real_vcn = 0;

    for (auto fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
        if (*data->running_ != RunningState::RUNNING) break;

        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            if (real_vcn >= offset + size) break;

            if (real_vcn + fragment->next_vcn_ - vcn - 1 >= offset) {
                /* Setup the parameters for the move. If the block that we want to move
                begins somewhere in the middle of a fragment then we have to setup
                slightly differently than when the fragment is at or after the begin
                of the block. */
                move_params.FileHandle = file_handle;

                if (real_vcn < offset) {
                    /* The fragment starts before the Offset and overlaps. Move the
                    part of the fragment from the Offset until the end of the
                    fragment or the block. */
                    move_params.StartingLcn.QuadPart = new_lcn;
                    move_params.StartingVcn.QuadPart = vcn + (offset - real_vcn);

                    if (size < fragment->next_vcn_ - vcn - (offset - real_vcn)) {
                        move_params.ClusterCount = (uint32_t)size;
                    }
                    else {
                        move_params.ClusterCount = (uint32_t)(fragment->next_vcn_ - vcn - (offset - real_vcn));
                    }

                    from_lcn = fragment->lcn_ + (offset - real_vcn);
                }
                else {
                    /* The fragment starts at or after the Offset. Move the part of
                    the fragment inside the block (up until Offset+Size). */
                    move_params.StartingLcn.QuadPart = new_lcn + real_vcn - offset;
                    move_params.StartingVcn.QuadPart = vcn;

                    if (fragment->next_vcn_ - vcn < offset + size - real_vcn) {
                        move_params.ClusterCount = (uint32_t)(fragment->next_vcn_ - vcn);
                    }
                    else {
                        move_params.ClusterCount = (uint32_t)(offset + size - real_vcn);
                    }
                    from_lcn = fragment->lcn_;
                }

                /* Show progress message. */
                gui->show_move(item, move_params.ClusterCount, from_lcn, move_params.StartingLcn.QuadPart,
                               move_params.StartingVcn.QuadPart);

                /* Draw the item and the destination clusters on the screen in the BUSY	color. */
                //				if (*Data->RedrawScreen == 0) {
                colorize_item(data, item, move_params.StartingVcn.QuadPart, move_params.ClusterCount, false);
                //				} else {
                //					m_jkGui->ShowDiskmap(Data);
                //				}

                gui->draw_cluster(data, move_params.StartingLcn.QuadPart,
                                  move_params.StartingLcn.QuadPart + move_params.ClusterCount,
                                  DefragStruct::COLORBUSY);

                /* Call Windows to perform the move. */
                error_code = DeviceIoControl(data->disk_.volume_handle_,FSCTL_MOVE_FILE, &move_params,
                                             sizeof move_params, nullptr, 0, &w, nullptr);

                if (error_code != 0) {
                    error_code = NO_ERROR;
                }
                else {
                    error_code = GetLastError();
                }

                /* Update the PhaseDone counter for the progress bar. */
                data->phase_done_ = data->phase_done_ + move_params.ClusterCount;

                /* Undraw the destination clusters on the screen. */
                gui->draw_cluster(data, move_params.StartingLcn.QuadPart,
                                  move_params.StartingLcn.QuadPart + move_params.ClusterCount,
                                  DefragStruct::COLOREMPTY);

                /* If there was an error then exit. */
                if (error_code != NO_ERROR) return error_code;
            }

            real_vcn = real_vcn + fragment->next_vcn_ - vcn;
        }

        /* Next fragment. */
        vcn = fragment->next_vcn_;
    }

    return error_code;
}

/*



*/
/**
 * \brief Subfunction for MoveItem(), see below. Move (part of) an item to a new location on disk.
 * Strategy 0: move the block in a single FSCTL_MOVE_FILE call. If the block has fragments then Windows will join them up.
 * Strategy 1: move the block one fragment at a time. The fragments will be lined up on disk and the defragger will treat them as unfragmented.
 * Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to
 * \param offset Number of first cluster to be moved
 * \param size Number of clusters to be moved
 * \param strategy 0: move in one part, 1: move individual fragments
 * \return 
 */
int DefragLib::move_item3(DefragDataStruct* data, ItemStruct* item, const HANDLE file_handle, const uint64_t new_lcn,
                          const uint64_t offset, const uint64_t size, const int strategy) const {
    uint32_t error_code;
    WCHAR error_string[BUFSIZ];
    DefragGui* gui = DefragGui::get_instance();

    /* Slow the program down if so selected. */
    slow_down(data);

    /* Move the item, either in a single block or fragment by fragment. */
    if (strategy == 0) {
        error_code = move_item1(data, file_handle, item, new_lcn, offset, size);
    }
    else {
        error_code = move_item2(data, file_handle, item, new_lcn, offset, size);
    }

    /* If there was an error then fetch the errormessage and save it. */
    if (error_code != NO_ERROR) system_error_str(error_code, error_string,BUFSIZ);

    /* Fetch the new fragment map of the item and refresh the screen. */
    colorize_item(data, item, 0, 0, true);

    tree_detach(data, item);

    const int result = get_fragments(data, item, file_handle);

    tree_insert(data, item);

    //		if (*Data->RedrawScreen == 0) {
    colorize_item(data, item, 0, 0, false);
    //		} else {
    //			m_jkGui->ShowDiskmap(Data);
    //		}

    /* If Windows reported an error while moving the item then show the
    errormessage and return false. */
    if (error_code != NO_ERROR) {
        gui->show_debug(DebugLevel::DetailedProgress, item, error_string);

        return false;
    }

    /* If there was an error analyzing the item then return false. */
    if (result == false) return false;

    return true;
}

/**
 * \brief Subfunction for MoveItem(), see below. Move the item with strategy 0. If this results in fragmentation then try again using strategy 1.
 * Note: The Windows defragmentation API does not report an error if it only moves part of the file and has fragmented the file. This can for example
 * happen when part of the file is locked and cannot be moved, or when (part of) the gap was previously in use by another file but has not yet been
 * released by the NTFS checkpoint system. Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to
 * \param offset Number of first cluster to be moved
 * \param size Number of clusters to be moved
 * \param direction 0: move up, 1: move down
 * \return true if success, false if failed to move without fragmenting the
item
 */
int DefragLib::move_item4(DefragDataStruct* data, ItemStruct* item, const HANDLE file_handle, const uint64_t new_lcn,
                          const uint64_t offset, const uint64_t size, const int direction) const {
    uint64_t cluster_start;
    uint64_t cluster_end;

    DefragGui* gui = DefragGui::get_instance();

    /* Remember the current position on disk of the item. */
    const uint64_t old_lcn = get_item_lcn(item);

    /* Move the Item to the requested LCN. If error then return false. */
    int result = move_item3(data, item, file_handle, new_lcn, offset, size, 0);

    if (result == false) return false;
    if (*data->running_ != RunningState::RUNNING) return false;

    /* If the block is not fragmented then return true. */
    if (is_fragmented(item, offset, size) == false) return true;

    /* Show debug message: "Windows could not move the file, trying alternative method." */
    gui->show_debug(DebugLevel::DetailedProgress, item, data->debug_msg_[42]);

    /* Find another gap on disk for the item. */
    if (direction == 0) {
        cluster_start = old_lcn + item->clusters_count_;

        if (cluster_start + item->clusters_count_ >= new_lcn &&
            cluster_start < new_lcn + item->clusters_count_) {
            cluster_start = new_lcn + item->clusters_count_;
        }

        result = find_gap(data, cluster_start, 0, size, true, false, &cluster_start, &cluster_end, FALSE);
    }
    else {
        result = find_gap(data, data->zones_[1], old_lcn, size, true, true, &cluster_start, &cluster_end, FALSE);
    }

    if (result == false) return false;

    /* Add the size of the item to the width of the progress bar, we have discovered
    that we have more work to do. */
    data->phase_todo_ = data->phase_todo_ + size;

    /* Move the item to the other gap using strategy 1. */
    if (direction == 0) {
        result = move_item3(data, item, file_handle, cluster_start, offset, size, 1);
    }
    else {
        result = move_item3(data, item, file_handle, cluster_end - size, offset, size, 1);
    }

    if (result == false) return false;

    /* If the block is still fragmented then return false. */
    if (is_fragmented(item, offset, size) == true) {
        /* Show debug message: "Alternative method failed, leaving file where it is." */
        gui->show_debug(DebugLevel::DetailedProgress, item, data->debug_msg_[45]);

        return false;
    }

    gui->show_debug(DebugLevel::DetailedProgress, item, L"");

    /* Add the size of the item to the width of the progress bar, we have more work to do. */
    data->phase_todo_ = data->phase_todo_ + size;

    /* Strategy 1 has helped. Move the Item again to where we want it, but
    this time use strategy 1. */
    result = move_item3(data, item, file_handle, new_lcn, offset, size, 1);

    return result;
}

/**
 * \brief Move (part of) an item to a new location on disk. Moving the Item will automatically defragment it. If unsuccesful then set the Unmovable
 * flag of the item and return false, otherwise return true. Note: the item will move to a different location in the tree.
 * Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to
 * \param offset Number of first cluster to be moved
 * \param size Number of clusters to be moved
 * \param direction 0: move up, 1: move down
 * \return 
 */
int DefragLib::move_item(DefragDataStruct* data, ItemStruct* item, const uint64_t new_lcn,
                         const uint64_t offset, const uint64_t size, const int direction) const {
    /* If the Item is Unmovable, Excluded, or has zero size then we cannot move it. */
    if (item->is_unmovable_ == true) return false;
    if (item->is_excluded_ == true) return false;
    if (item->clusters_count_ == 0) return false;

    /* Directories cannot be moved on FAT volumes. This is a known Windows limitation
    and not a bug in JkDefrag. But JkDefrag will still try, to allow for possible
    circumstances where the Windows defragmentation API can move them after all.
    To speed up things we count the number of directories that could not be moved,
    and when it reaches 20 we ignore all directories from then on. */
    if (item->is_dir_ == true && data->cannot_move_dirs_ > 20) {
        item->is_unmovable_ = true;

        colorize_item(data, item, 0, 0, false);

        return false;
    }

    /* Open a filehandle for the item and call the subfunctions (see above) to
    move the file. If success then return true. */
    uint64_t clusters_done = 0;
    bool result = true;

    while (clusters_done < size && *data->running_ == RunningState::RUNNING) {
        uint64_t clusters_todo = size - clusters_done;

        if (data->bytes_per_cluster_ > 0) {
            if (clusters_todo > 1073741824 / data->bytes_per_cluster_) {
                clusters_todo = 1073741824 / data->bytes_per_cluster_;
            }
        }
        else {
            if (clusters_todo > 262144) clusters_todo = 262144;
        }

        const HANDLE file_handle = open_item_handle(data, item);

        result = false;

        if (file_handle == nullptr) break;

        result = move_item4(data, item, file_handle, new_lcn + clusters_done, offset + clusters_done,
                            clusters_todo, direction);

        if (result == false) break;

        clusters_done = clusters_done + clusters_todo;

        FlushFileBuffers(file_handle); /* Is this useful? Can't hurt. */
        CloseHandle(file_handle);
    }

    if (result == true) {
        if (item->is_dir_ == true) data->cannot_move_dirs_ = 0;

        return true;
    }

    /* If error then set the Unmovable flag, colorize the item on the screen, recalculate
    the begin of the zone's, and return false. */
    item->is_unmovable_ = true;

    if (item->is_dir_ == true) data->cannot_move_dirs_++;

    colorize_item(data, item, 0, 0, false);
    calculate_zones(data);

    return false;
}

/**
 * \brief Look in the ItemTree and return the highest file above the gap that fits inside the gap (cluster start - cluster end).
 * \param direction 0=Search for files below the gap, 1=above
 * \param zone 0=only directories, 1=only regular files, 2=only space hogs, 3=all
 * \return Return a pointer to the item, or nullptr if no file could be found
 */
ItemStruct* DefragLib::find_highest_item(const DefragDataStruct* data, const uint64_t cluster_start,
                                         const uint64_t cluster_end, const int direction, const int zone) {
    DefragGui* gui = DefragGui::get_instance();

    /* "Looking for highest-fit %I64d[%I64d]" */
    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Looking for highest-fit %I64d[%I64d]",
                    cluster_start, cluster_end - cluster_start);

    /* Walk backwards through all the items on disk and select the first
    file that fits inside the free block. If we find an exact match then
    immediately return it. */
    for (auto item = tree_first(data->item_tree_, direction); item != nullptr; item = tree_next_prev(item, direction)) {
        const uint64_t item_lcn = get_item_lcn(item);

        if (item_lcn == 0) continue;

        if (direction == 1) {
            if (item_lcn < cluster_end) return nullptr;
        }
        else {
            if (item_lcn > cluster_start) return nullptr;
        }

        if (item->is_unmovable_ == true) continue;
        if (item->is_excluded_ == true) continue;

        if (zone != 3) {
            int file_zone = 1;

            if (item->is_hog_ == true) file_zone = 2;
            if (item->is_dir_ == true) file_zone = 0;
            if (zone != file_zone) continue;
        }

        if (item->clusters_count_ > cluster_end - cluster_start) continue;

        return item;
    }

    return nullptr;
}

/*

Find the highest item on disk that fits inside the gap (cluster start - cluster
end), and combined with other items will perfectly fill the gap. Return nullptr if
no perfect fit could be found. The subroutine will limit it's running time to 0.5
seconds.
Direction=0      Search for files below the gap.
Direction=1      Search for files above the gap.
Zone=0           Only search the directories.
Zone=1           Only search the regular files.
Zone=2           Only search the SpaceHogs.
Zone=3           Search all items.

*/
ItemStruct* DefragLib::find_best_item(const DefragDataStruct* data, const uint64_t cluster_start,
                                      const uint64_t cluster_end, const int direction, const int zone) {
    __timeb64 time{};
    DefragGui* gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Looking for perfect fit %I64d[%I64d]",
                      cluster_start, cluster_end - cluster_start);

    /* Walk backwards through all the items on disk and select the first item that
    fits inside the free block, and combined with other items will fill the gap
    perfectly. If we find an exact match then immediately return it. */

    _ftime64_s(&time);

    const int64_t MaxTime = time.time * 1000 + time.millitm + 500;
    ItemStruct* first_item = nullptr;
    uint64_t gap_size = cluster_end - cluster_start;
    uint64_t total_items_size = 0;

    for (auto item = tree_first(data->item_tree_, direction);
         item != nullptr;
         item = tree_next_prev(item, direction)) {
        /* If we have passed the top of the gap then.... */
        const uint64_t item_lcn = get_item_lcn(item);

        if (item_lcn == 0) continue;

        if ((direction == 1 && item_lcn < cluster_end) ||
            (direction == 0 && item_lcn > cluster_end)) {
            /* If we did not find an item that fits inside the gap then exit. */
            if (first_item == nullptr) break;

            /* Exit if the total size of all the items is less than the size of the gap.
            We know that we can never find a perfect fit. */
            if (total_items_size < cluster_end - cluster_start) {
                gui->show_debug(
                    DebugLevel::DetailedGapFilling, nullptr,
                    L"No perfect fit found, the total size of all the items above the gap is less than the size of the gap.");

                return nullptr;
            }

            /* Exit if the running time is more than 0.5 seconds. */
            _ftime64_s(&time);

            if (time.time * 1000 + time.millitm > MaxTime) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No perfect fit found, out of time.");

                return nullptr;
            }

            /* Rewind and try again. The item that we have found previously fits in the
            gap, but it does not combine with other items to perfectly fill the gap. */
            item = first_item;
            first_item = nullptr;
            gap_size = cluster_end - cluster_start;
            total_items_size = 0;

            continue;
        }

        /* Ignore all unsuitable items. */
        if (item->is_unmovable_ == true) continue;
        if (item->is_excluded_ == true) continue;

        if (zone != 3) {
            int file_zone = 1;

            if (item->is_hog_ == true) file_zone = 2;
            if (item->is_dir_ == true) file_zone = 0;
            if (zone != file_zone) continue;
        }

        if (item->clusters_count_ < cluster_end - cluster_start) {
            total_items_size = total_items_size + item->clusters_count_;
        }

        if (item->clusters_count_ > gap_size) continue;

        /* Exit if this item perfectly fills the gap, or if we have found a combination
        with a previous item that perfectly fills the gap. */
        if (item->clusters_count_ == gap_size) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Perfect fit found.");

            if (first_item != nullptr) return first_item;

            return item;
        }

        /* We have found an item that fit's inside the gap, but does not perfectly fill
        the gap. We are now looking to fill a smaller gap. */
        gap_size = gap_size - item->clusters_count_;

        /* Remember the first item that fits inside the gap. */
        if (first_item == nullptr) first_item = item;
    }

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                      L"No perfect fit found, all items above the gap are bigger than the gap.");

    return nullptr;
}

/* Update some numbers in the DefragData. */
void DefragLib::call_show_status(DefragDataStruct* data, const int phase, const int zone) {
    ItemStruct* item;
    STARTING_LCN_INPUT_BUFFER bitmap_param;

    struct {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        // Most efficient if power of 2 
        BYTE buffer_[65536]; 
    } bitmap_data{};

    uint32_t error_code;
    DWORD w;
    DefragGui* gui = DefragGui::get_instance();

    /* Count the number of free gaps on the disk. */
    data->count_gaps_ = 0;
    data->count_free_clusters_ = 0;
    data->biggest_gap_ = 0;
    data->count_gaps_less16_ = 0;
    data->count_clusters_less16_ = 0;

    uint64_t lcn = 0;
    uint64_t cluster_start = 0;
    int prev_in_use = 1;

    do {
        /* Fetch a block of cluster data. */
        bitmap_param.StartingLcn.QuadPart = lcn;
        error_code = DeviceIoControl(data->disk_.volume_handle_,FSCTL_GET_VOLUME_BITMAP,
                                    &bitmap_param, sizeof bitmap_param, &bitmap_data, sizeof bitmap_data, &w, nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        }
        else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) break;

        lcn = bitmap_data.starting_lcn_;
        int index = 0;
        BYTE mask = 1;
        int index_max = sizeof bitmap_data.buffer_;

        if (bitmap_data.bitmap_size_ / 8 < index_max) index_max = (int)(bitmap_data.bitmap_size_ / 8);

        while (index < index_max) {
            int in_use = bitmap_data.buffer_[index] & mask;

            if ((lcn >= data->mft_excludes_[0].start_ && lcn < data->mft_excludes_[0].end_) ||
                (lcn >= data->mft_excludes_[1].start_ && lcn < data->mft_excludes_[1].end_) ||
                (lcn >= data->mft_excludes_[2].start_ && lcn < data->mft_excludes_[2].end_)) {
                in_use = 1;
            }

            if (prev_in_use == 0 && in_use != 0) {
                data->count_gaps_ = data->count_gaps_ + 1;
                data->count_free_clusters_ = data->count_free_clusters_ + lcn - cluster_start;
                if (data->biggest_gap_ < lcn - cluster_start) data->biggest_gap_ = lcn - cluster_start;

                if (lcn - cluster_start < 16) {
                    data->count_gaps_less16_ = data->count_gaps_less16_ + 1;
                    data->count_clusters_less16_ = data->count_clusters_less16_ + lcn - cluster_start;
                }
            }

            if (prev_in_use != 0 && in_use == 0) cluster_start = lcn;

            prev_in_use = in_use;

            if (mask == 128) {
                mask = 1;
                index = index + 1;
            }
            else {
                mask = mask << 1;
            }

            lcn = lcn + 1;
        }
    }
    while (error_code == ERROR_MORE_DATA && lcn < bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_);

    if (prev_in_use == 0) {
        data->count_gaps_ = data->count_gaps_ + 1;
        data->count_free_clusters_ = data->count_free_clusters_ + lcn - cluster_start;

        if (data->biggest_gap_ < lcn - cluster_start) data->biggest_gap_ = lcn - cluster_start;

        if (lcn - cluster_start < 16) {
            data->count_gaps_less16_ = data->count_gaps_less16_ + 1;
            data->count_clusters_less16_ = data->count_clusters_less16_ + lcn - cluster_start;
        }
    }

    /* Walk through all files and update the counters. */
    data->count_directories_ = 0;
    data->count_all_files_ = 0;
    data->count_fragmented_items_ = 0;
    data->count_all_bytes_ = 0;
    data->count_fragmented_bytes_ = 0;
    data->count_all_clusters_ = 0;
    data->count_fragmented_clusters_ = 0;

    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (item->long_filename_ != nullptr &&
            (_wcsicmp(item->long_filename_, L"$BadClus") == 0 ||
                _wcsicmp(item->long_filename_, L"$BadClus:$Bad:$DATA") == 0)) {
            continue;
        }

        data->count_all_bytes_ = data->count_all_bytes_ + item->bytes_;
        data->count_all_clusters_ = data->count_all_clusters_ + item->clusters_count_;

        if (item->is_dir_ == true) {
            data->count_directories_ = data->count_directories_ + 1;
        }
        else {
            data->count_all_files_ = data->count_all_files_ + 1;
        }

        if (get_fragment_count(item) > 1) {
            data->count_fragmented_items_ = data->count_fragmented_items_ + 1;
            data->count_fragmented_bytes_ = data->count_fragmented_bytes_ + item->bytes_;
            data->count_fragmented_clusters_ = data->count_fragmented_clusters_ + item->clusters_count_;
        }
    }

    /* Calculate the average distance between the end of any file to the begin of
    any other file. After reading a file the harddisk heads will have to move to
    the beginning of another file. The number is a measure of how fast files can
    be accessed.

    For example these 3 files:
    File 1 begin = 107
    File 1 end = 312
    File 2 begin = 595
    File 2 end = 645
    File 3 begin = 917
    File 3 end = 923

    File 1 end - File 2 begin = 283
    File 1 end - File 3 begin = 605
    File 2 end - File 1 begin = 538
    File 2 end - File 3 begin = 272
    File 3 end - File 1 begin = 816
    File 3 end - File 2 begin = 328
    --> Average distance from end to begin = 473.6666

    The formula used is:
    N = number of files
    Bn = Begin of file n
    En = End of file n
    Average = ( (1-N)*(B1+E1) + (3-N)*(B2+E2) + (5-N)*(B3+E3) + .... + (2*N-1-N)*(BN+EN) ) / ( N * (N-1) )

    For the above example:
    Average = ( (1-3)*(107+312) + (3-3)*(595+645) + 5-3)*(917+923) ) / ( 3 * (3-1) ) = 473.6666

    */
    int64_t count = 0;

    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (item->long_filename_ != nullptr &&
            (_wcsicmp(item->long_filename_, L"$BadClus") == 0 ||
                _wcsicmp(item->long_filename_, L"$BadClus:$Bad:$DATA") == 0)) {
            continue;
        }

        if (item->clusters_count_ == 0) continue;

        count = count + 1;
    }

    if (count > 1) {
        int64_t factor = 1 - count;
        int64_t sum = 0;

        for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
            if (item->long_filename_ != nullptr &&
                (_wcsicmp(item->long_filename_, L"$BadClus") == 0 ||
                    _wcsicmp(item->long_filename_, L"$BadClus:$Bad:$DATA") == 0)) {
                continue;
            }

            if (item->clusters_count_ == 0) continue;

            sum = sum + factor * (get_item_lcn(item) * 2 + item->clusters_count_);

            factor = factor + 2;
        }

        data->average_distance_ = sum / (double)(count * (count - 1));
    }
    else {
        data->average_distance_ = 0;
    }

    data->phase_ = phase;
    data->zone_ = zone;
    data->phase_done_ = 0;
    data->phase_todo_ = 0;

    gui->show_status(data);
}

/* For debugging only: compare the data with the output from the
FSCTL_GET_RETRIEVAL_POINTERS function call.
Note: Reparse points will usually be flagged as different. A reparse point is
a symbolic link. The CreateFile call will resolve the symbolic link and retrieve
the info from the real item, but the MFT contains the info from the symbolic
link. */
void DefragLib::compare_items([[maybe_unused]] DefragDataStruct* data, const ItemStruct* item) const {
    HANDLE file_handle;
    uint64_t clusters; /* Total number of clusters. */
    STARTING_VCN_INPUT_BUFFER retrieve_param;

    struct {
        uint32_t extent_count_;
        uint64_t starting_vcn_;
        struct {
            uint64_t next_vcn_;
            uint64_t lcn_;
        } extents_[1000];
    } extent_data{};

    BY_HANDLE_FILE_INFORMATION file_information;
    uint64_t vcn;

    FragmentListStruct* Fragment;
    FragmentListStruct* LastFragment;

    uint32_t ErrorCode;

    WCHAR ErrorString[BUFSIZ];

    int MaxLoop;

    ULARGE_INTEGER u;

    uint32_t i;
    DWORD w;

    DefragGui* jkGui = DefragGui::get_instance();

    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"%I64u %s", get_item_lcn(item), item->long_filename_);

    if (item->is_dir_ == false) {
        file_handle = CreateFileW(item->long_path_,FILE_READ_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr,OPEN_EXISTING,FILE_FLAG_NO_BUFFERING, nullptr);
    }
    else {
        file_handle = CreateFileW(item->long_path_,GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    }

    if (file_handle == INVALID_HANDLE_VALUE) {
        system_error_str(GetLastError(), ErrorString,BUFSIZ);

        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Could not open: %s", ErrorString);

        return;
    }

    /* Fetch the date/times of the file. */
    if (GetFileInformationByHandle(file_handle, &file_information) != 0) {
        u.LowPart = file_information.ftCreationTime.dwLowDateTime;
        u.HighPart = file_information.ftCreationTime.dwHighDateTime;

        if (item->creation_time_ != u.QuadPart) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different CreationTime %I64u <> %I64u = %I64u",
                              item->creation_time_, u.QuadPart, item->creation_time_ - u.QuadPart);
        }

        u.LowPart = file_information.ftLastAccessTime.dwLowDateTime;
        u.HighPart = file_information.ftLastAccessTime.dwHighDateTime;

        if (item->last_access_time_ != u.QuadPart) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different LastAccessTime %I64u <> %I64u = %I64u",
                              item->last_access_time_, u.QuadPart, item->last_access_time_ - u.QuadPart);
        }
    }

#ifdef jk
	Vcn = 0;
	for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->Next) {
		if (Fragment->Lcn != VIRTUALFRAGMENT) {
			Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 1: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u",
				Fragment->Lcn,Vcn,Fragment->NextVcn);
		} else {
			Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 1 (virtual): Vcn=%I64u, NextVcn=%I64u",
				Vcn,Fragment->NextVcn);
		}
		Vcn = Fragment->NextVcn;
	}
#endif

    /* Ask Windows for the clustermap of the item and save it in memory.
    The buffer that is used to ask Windows for the clustermap has a
    fixed size, so we may have to loop a couple of times. */
    Fragment = item->fragments_;
    clusters = 0;
    vcn = 0;
    MaxLoop = 1000;
    LastFragment = nullptr;

    do {
        /* I strongly suspect that the FSCTL_GET_RETRIEVAL_POINTERS system call
        can sometimes return an empty bitmap and ERROR_MORE_DATA. That's not
        very nice of Microsoft, because it causes an infinite loop. I've
        therefore added a loop counter that will limit the loop to 1000
        iterations. This means the defragger cannot handle files with more
        than 100000 fragments, though. */
        if (MaxLoop <= 0) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  FSCTL_GET_RETRIEVAL_POINTERS error: Infinite loop");

            return;
        }

        MaxLoop = MaxLoop - 1;

        /* Ask Windows for the (next segment of the) clustermap of this file. If error
        then leave the loop. */
        retrieve_param.StartingVcn.QuadPart = vcn;

        ErrorCode = DeviceIoControl(file_handle,FSCTL_GET_RETRIEVAL_POINTERS,
                                    &retrieve_param, sizeof retrieve_param, &extent_data, sizeof extent_data, &w,
                                    nullptr);

        if (ErrorCode != 0) {
            ErrorCode = NO_ERROR;
        }
        else {
            ErrorCode = GetLastError();
        }

        if (ErrorCode != NO_ERROR && ErrorCode != ERROR_MORE_DATA) break;

        /* Walk through the clustermap, count the total number of clusters, and
        save all fragments in memory. */
        for (i = 0; i < extent_data.extent_count_; i++) {
            /* Show debug message. */
#ifdef jk
			if (ExtentData.Extents[i].Lcn != VIRTUALFRAGMENT) {
				Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 2: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u",
					ExtentData.Extents[i].Lcn,Vcn,ExtentData.Extents[i].NextVcn);
			} else {
				Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 2 (virtual): Vcn=%I64u, NextVcn=%I64u",
					Vcn,ExtentData.Extents[i].NextVcn);
			}
#endif

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */
            if (extent_data.extents_[i].lcn_ != VIRTUALFRAGMENT) {
                clusters = clusters + extent_data.extents_[i].next_vcn_ - vcn;
            }

            /* Compare the fragment. */
            if (Fragment == nullptr) {
                jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Extra fragment in FSCTL_GET_RETRIEVAL_POINTERS");
            }
            else {
                if (Fragment->lcn_ != extent_data.extents_[i].lcn_) {
                    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different LCN in fragment: %I64u <> %I64u",
                                      Fragment->lcn_, extent_data.extents_[i].lcn_);
                }

                if (Fragment->next_vcn_ != extent_data.extents_[i].next_vcn_) {
                    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different NextVcn in fragment: %I64u <> %I64u",
                                      Fragment->next_vcn_, extent_data.extents_[i].next_vcn_);
                }

                Fragment = Fragment->next_;
            }

            /* The Vcn of the next fragment is the NextVcn field in this record. */
            vcn = extent_data.extents_[i].next_vcn_;
        }

        /* Loop until we have processed the entire clustermap of the file. */
    }
    while (ErrorCode == ERROR_MORE_DATA);

    /* If there was an error while reading the clustermap then return false. */
    if (ErrorCode != NO_ERROR && ErrorCode != ERROR_HANDLE_EOF) {
        system_error_str(ErrorCode, ErrorString,BUFSIZ);

        jkGui->show_debug(DebugLevel::Fatal, item, L"  Error while processing clustermap: %s", ErrorString);

        return;
    }

    if (Fragment != nullptr) {
        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Extra fragment from MFT");
    }

    if (item->clusters_count_ != clusters) {
        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different cluster count: %I64u <> %I64u",
                          item->clusters_count_, clusters);
    }
}

/* Scan all files in a directory and all it's subdirectories (recursive)
and store the information in a tree in memory for later use by the
optimizer. */
void DefragLib::scan_dir(DefragDataStruct* Data, WCHAR* Mask, ItemStruct* ParentDirectory) {
    ItemStruct* Item;

    FragmentListStruct* Fragment;

    HANDLE FindHandle;

    WIN32_FIND_DATAW FindFileData;

    WCHAR* RootPath;
    WCHAR* TempPath;

    HANDLE FileHandle;

    uint64_t SystemTime;

    SYSTEMTIME Time1;

    FILETIME Time2;

    ULARGE_INTEGER Time3;

    int Result;

    size_t Length;

    WCHAR* p1;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Slow the program down to the percentage that was specified on the
    command line. */
    slow_down(Data);

    /* Determine the rootpath (base path of the directory) by stripping
    everything after the last backslash in the Mask. The FindFirstFile()
    system call only processes wildcards in the last section (i.e. after
    the last backslash). */
    RootPath = _wcsdup(Mask);

    if (RootPath == nullptr) return;

    p1 = wcsrchr(RootPath, '\\');

    if (p1 != nullptr) *p1 = 0;

    /* Show debug message: "Analyzing: %s". */
    jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, Data->debug_msg_[23], Mask);

    /* Fetch the current time in the uint64_t format (1 second = 10000000). */
    GetSystemTime(&Time1);

    if (SystemTimeToFileTime(&Time1, &Time2) == FALSE) {
        SystemTime = 0;
    }
    else {
        Time3.LowPart = Time2.dwLowDateTime;
        Time3.HighPart = Time2.dwHighDateTime;

        SystemTime = Time3.QuadPart;
    }

    /* Walk through all the files. If nothing found then exit.
    Note: I am using FindFirstFileW() instead of _findfirst() because the latter
    will crash (exit program) on files with badly formed dates. */
    FindHandle = FindFirstFileW(Mask, &FindFileData);

    if (FindHandle == INVALID_HANDLE_VALUE) {
        free(RootPath);
        return;
    }

    Item = nullptr;

    do {
        if (*Data->running_ != RunningState::RUNNING) break;

        if (wcscmp(FindFileData.cFileName, L".") == 0) continue;
        if (wcscmp(FindFileData.cFileName, L"..") == 0) continue;

        /* Ignore reparse-points, a directory where a volume is mounted
        with the MOUNTVOL command. */
        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            continue;
        }

        /* Cleanup old item. */
        if (Item != nullptr) {
            if (Item->short_path_ != nullptr) free(Item->short_path_);
            if (Item->short_filename_ != nullptr) free(Item->short_filename_);
            if (Item->long_path_ != nullptr) free(Item->long_path_);
            if (Item->long_filename_ != nullptr) free(Item->long_filename_);

            while (Item->fragments_ != nullptr) {
                Fragment = Item->fragments_->next_;

                free(Item->fragments_);

                Item->fragments_ = Fragment;
            }

            free(Item);

            Item = nullptr;
        }

        /* Create new item. */
        Item = (ItemStruct*)malloc(sizeof(ItemStruct));

        if (Item == nullptr) break;

        Item->short_path_ = nullptr;
        Item->short_filename_ = nullptr;
        Item->long_path_ = nullptr;
        Item->long_filename_ = nullptr;
        Item->fragments_ = nullptr;

        Length = wcslen(RootPath) + wcslen(FindFileData.cFileName) + 2;

        Item->long_path_ = (WCHAR*)malloc(sizeof(WCHAR) * Length);

        if (Item->long_path_ == nullptr) break;

        swprintf_s(Item->long_path_, Length, L"%s\\%s", RootPath, FindFileData.cFileName);

        Item->long_filename_ = _wcsdup(FindFileData.cFileName);

        if (Item->long_filename_ == nullptr) break;

        Length = wcslen(RootPath) + wcslen(FindFileData.cAlternateFileName) + 2;

        Item->short_path_ = (WCHAR*)malloc(sizeof(WCHAR) * Length);

        if (Item->short_path_ == nullptr) break;

        swprintf_s(Item->short_path_, Length, L"%s\\%s", RootPath, FindFileData.cAlternateFileName);

        Item->short_filename_ = _wcsdup(FindFileData.cAlternateFileName);

        if (Item->short_filename_ == nullptr) break;

        Item->bytes_ = FindFileData.nFileSizeHigh * ((uint64_t)MAXDWORD + 1) +
                FindFileData.nFileSizeLow;

        Item->clusters_count_ = 0;
        Item->creation_time_ = 0;
        Item->last_access_time_ = 0;
        Item->mft_change_time_ = 0;
        Item->parent_directory_ = ParentDirectory;
        Item->is_dir_ = false;

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            Item->is_dir_ = true;
        }
        Item->is_unmovable_ = false;
        Item->is_excluded_ = false;
        Item->is_hog_ = false;

        /* Analyze the item: Clusters and Fragments, and the CreationTime, LastAccessTime,
        and MftChangeTime. If the item could not be opened then ignore the item. */
        FileHandle = open_item_handle(Data, Item);

        if (FileHandle == nullptr) continue;

        Result = get_fragments(Data, Item, FileHandle);

        CloseHandle(FileHandle);

        if (Result == false) continue;

        /* Increment counters. */
        Data->count_all_files_ = Data->count_all_files_ + 1;
        Data->count_all_bytes_ = Data->count_all_bytes_ + Item->bytes_;
        Data->count_all_clusters_ = Data->count_all_clusters_ + Item->clusters_count_;

        if (is_fragmented(Item, 0, Item->clusters_count_) == true) {
            Data->count_fragmented_items_ = Data->count_fragmented_items_ + 1;
            Data->count_fragmented_bytes_ = Data->count_fragmented_bytes_ + Item->bytes_;
            Data->count_fragmented_clusters_ = Data->count_fragmented_clusters_ + Item->clusters_count_;
        }

        Data->phase_done_ = Data->phase_done_ + Item->clusters_count_;

        /* Show progress message. */
        jkGui->show_analyze(Data, Item);

        /* If it's a directory then iterate subdirectories. */
        if (Item->is_dir_ == true) {
            Data->count_directories_ = Data->count_directories_ + 1;

            Length = wcslen(RootPath) + wcslen(FindFileData.cFileName) + 4;

            TempPath = (WCHAR*)malloc(sizeof(WCHAR) * Length);

            if (TempPath != nullptr) {
                swprintf_s(TempPath, Length, L"%s\\%s\\*", RootPath, FindFileData.cFileName);
                scan_dir(Data, TempPath, Item);
                free(TempPath);
            }
        }

        /* Ignore the item if it has no clusters or no LCN. Very small
        files are stored in the MFT and are reported by Windows as
        having zero clusters and no fragments. */
        if (Item->clusters_count_ == 0 || Item->fragments_ == nullptr) continue;

        /* Draw the item on the screen. */
        //		if (*Data->RedrawScreen == 0) {
        colorize_item(Data, Item, 0, 0, false);
        //		} else {
        //			m_jkGui->ShowDiskmap(Data);
        //		}

        /* Show debug info about the file. */
        /* Show debug message: "%I64d clusters at %I64d, %I64d bytes" */
        jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[16], Item->clusters_count_,
                          get_item_lcn(Item), Item->bytes_);

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0) {
            /* Show debug message: "Special file attribute: Compressed" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[17]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0) {
            /* Show debug message: "Special file attribute: Encrypted" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[18]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) != 0) {
            /* Show debug message: "Special file attribute: Offline" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[19]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
            /* Show debug message: "Special file attribute: Read-only" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[20]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0) {
            /* Show debug message: "Special file attribute: Sparse-file" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[21]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0) {
            /* Show debug message: "Special file attribute: Temporary" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[22]);
        }

        /* Save some memory if short and long filename are the same. */
        if (Item->long_filename_ != nullptr &&
            Item->short_filename_ != nullptr &&
            _wcsicmp(Item->long_filename_, Item->short_filename_) == 0) {
            free(Item->short_filename_);
            Item->short_filename_ = Item->long_filename_;
        }

        if (Item->long_filename_ == nullptr && Item->short_filename_ != nullptr)
            Item->long_filename_ = Item->
                    short_filename_;
        if (Item->long_filename_ != nullptr && Item->short_filename_ == nullptr)
            Item->short_filename_ = Item->
                    long_filename_;

        if (Item->long_path_ != nullptr &&
            Item->short_path_ != nullptr &&
            _wcsicmp(Item->long_path_, Item->short_path_) == 0) {
            free(Item->short_path_);
            Item->short_path_ = Item->long_path_;
        }

        if (Item->long_path_ == nullptr && Item->short_path_ != nullptr) Item->long_path_ = Item->short_path_;
        if (Item->long_path_ != nullptr && Item->short_path_ == nullptr) Item->short_path_ = Item->long_path_;

        /* Add the item to the ItemTree in memory. */
        tree_insert(Data, Item);
        Item = nullptr;
    }
    while (FindNextFileW(FindHandle, &FindFileData) != 0);

    FindClose(FindHandle);

    /* Cleanup. */
    free(RootPath);

    if (Item != nullptr) {
        if (Item->short_path_ != nullptr) free(Item->short_path_);
        if (Item->short_filename_ != nullptr) free(Item->short_filename_);
        if (Item->long_path_ != nullptr) free(Item->long_path_);
        if (Item->long_filename_ != nullptr) free(Item->long_filename_);

        while (Item->fragments_ != nullptr) {
            Fragment = Item->fragments_->next_;

            free(Item->fragments_);

            Item->fragments_ = Fragment;
        }

        free(Item);
    }
}

/* Scan all files in a volume and store the information in a tree in
memory for later use by the optimizer. */
void DefragLib::analyze_volume(DefragDataStruct* Data) {
    ItemStruct* Item;

    BOOL Result;

    uint64_t SystemTime;

    SYSTEMTIME Time1;

    FILETIME Time2;

    ULARGE_INTEGER Time3;

    int i;

    DefragGui* jkGui = DefragGui::get_instance();
    JKScanFat* jkScanFat = JKScanFat::get_instance();
    ScanNtfs* jkScanNtfs = ScanNtfs::get_instance();

    call_show_status(Data, 1, -1); /* "Phase 1: Analyze" */

    /* Fetch the current time in the uint64_t format (1 second = 10000000). */
    GetSystemTime(&Time1);

    if (SystemTimeToFileTime(&Time1, &Time2) == FALSE) {
        SystemTime = 0;
    }
    else {
        Time3.LowPart = Time2.dwLowDateTime;
        Time3.HighPart = Time2.dwHighDateTime;

        SystemTime = Time3.QuadPart;
    }

    /* Scan NTFS disks. */
    Result = jkScanNtfs->analyze_ntfs_volume(Data);

    /* Scan FAT disks. */
    if (Result == FALSE && *Data->running_ == RunningState::RUNNING) Result = jkScanFat->analyze_fat_volume(Data);

    /* Scan all other filesystems. */
    if (Result == FALSE && *Data->running_ == RunningState::RUNNING) {
        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"This is not a FAT or NTFS disk, using the slow scanner.");

        /* Setup the width of the progress bar. */
        Data->phase_todo_ = Data->total_clusters_ - Data->count_free_clusters_;

        for (i = 0; i < 3; i++) {
            Data->phase_todo_ = Data->phase_todo_ - (Data->mft_excludes_[i].end_ - Data->mft_excludes_[i].start_);
        }

        /* Scan all the files. */
        scan_dir(Data, Data->include_mask_, nullptr);
    }

    /* Update the diskmap with the colors. */
    Data->phase_done_ = Data->phase_todo_;
    jkGui->draw_cluster(Data, 0, 0, 0);

    /* Setup the progress counter and the file/dir counters. */
    Data->phase_done_ = 0;
    Data->phase_todo_ = 0;

    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = tree_next(Item)) {
        Data->phase_todo_ = Data->phase_todo_ + 1;
    }

    jkGui->show_analyze(nullptr, nullptr);

    /* Walk through all the items one by one. */
    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = tree_next(Item)) {
        if (*Data->running_ != RunningState::RUNNING) break;

        /* If requested then redraw the diskmap. */
        //		if (*Data->RedrawScreen == 1) m_jkGui->ShowDiskmap(Data);

        /* Construct the full path's of the item. The MFT contains only the filename, plus
        a pointer to the directory. We have to construct the full paths's by joining
        all the names of the directories, and the name of the file. */
        if (Item->long_path_ == nullptr) Item->long_path_ = get_long_path(Data, Item);
        if (Item->short_path_ == nullptr) Item->short_path_ = get_short_path(Data, Item);

        /* Save some memory if the short and long paths are the same. */
        if (Item->long_path_ != nullptr &&
            Item->short_path_ != nullptr &&
            Item->long_path_ != Item->short_path_ &&
            _wcsicmp(Item->long_path_, Item->short_path_) == 0) {
            free(Item->short_path_);
            Item->short_path_ = Item->long_path_;
        }

        if (Item->long_path_ == nullptr && Item->short_path_ != nullptr) Item->long_path_ = Item->short_path_;
        if (Item->long_path_ != nullptr && Item->short_path_ == nullptr) Item->short_path_ = Item->long_path_;

        /* For debugging only: compare the data with the output from the
        FSCTL_GET_RETRIEVAL_POINTERS function call. */
        /*
        CompareItems(Data,Item);
        */

        /* Apply the Mask and set the Exclude flag of all items that do not match. */
        if (match_mask(Item->long_path_, Data->include_mask_) == false &&
            match_mask(Item->short_path_, Data->include_mask_) == false) {
            Item->is_excluded_ = true;

            colorize_item(Data, Item, 0, 0, false);
        }

        /* Determine if the item is to be excluded by comparing it's name with the
        Exclude masks. */
        if (Item->is_excluded_ == false && Data->excludes_ != nullptr) {
            for (i = 0; Data->excludes_[i] != nullptr; i++) {
                if (match_mask(Item->long_path_, Data->excludes_[i]) == true ||
                    match_mask(Item->short_path_, Data->excludes_[i]) == true) {
                    Item->is_excluded_ = true;

                    colorize_item(Data, Item, 0, 0, false);

                    break;
                }
            }
        }

        /* Exclude my own logfile. */
        if (Item->is_excluded_ == false &&
            Item->long_filename_ != nullptr &&
            (_wcsicmp(Item->long_filename_, L"jkdefrag.log") == 0 ||
                _wcsicmp(Item->long_filename_, L"jkdefragcmd.log") == 0 ||
                _wcsicmp(Item->long_filename_, L"jkdefragscreensaver.log") == 0)) {
            Item->is_excluded_ = true;

            colorize_item(Data, Item, 0, 0, false);
        }

        /* The item is a SpaceHog if it's larger than 50 megabytes, or last access time
        is more than 30 days ago, or if it's filename matches a SpaceHog mask. */
        if (Item->is_excluded_ == false && Item->is_dir_ == false) {
            if (Data->use_default_space_hogs_ == true && Item->bytes_ > 50 * 1024 * 1024) {
                Item->is_hog_ = true;
            }
            else if (Data->use_default_space_hogs_ == true &&
                Data->use_last_access_time_ == TRUE &&
                Item->last_access_time_ + (uint64_t)(30 * 24 * 60 * 60) * 10000000 < SystemTime) {
                Item->is_hog_ = true;
            }
            else if (Data->space_hogs_ != nullptr) {
                for (i = 0; Data->space_hogs_[i] != nullptr; i++) {
                    if (match_mask(Item->long_path_, Data->space_hogs_[i]) == true ||
                        match_mask(Item->short_path_, Data->space_hogs_[i]) == true) {
                        Item->is_hog_ = true;

                        break;
                    }
                }
            }

            if (Item->is_hog_ == true) colorize_item(Data, Item, 0, 0, false);
        }

        /* Special exception for "http://www.safeboot.com/". */
        if (match_mask(Item->long_path_, L"*\\safeboot.fs") == true) Item->is_unmovable_ = true;

        /* Special exception for Acronis OS Selector. */
        if (match_mask(Item->long_path_, L"?:\\bootwiz.sys") == true) Item->is_unmovable_ = true;
        if (match_mask(Item->long_path_, L"*\\BOOTWIZ\\*") == true) Item->is_unmovable_ = true;

        /* Special exception for DriveCrypt by "http://www.securstar.com/". */
        if (match_mask(Item->long_path_, L"?:\\BootAuth?.sys") == true) Item->is_unmovable_ = true;

        /* Special exception for Symantec GoBack. */
        if (match_mask(Item->long_path_, L"*\\Gobackio.bin") == true) Item->is_unmovable_ = true;

        /* The $BadClus file maps the entire disk and is always unmovable. */
        if (Item->long_filename_ != nullptr &&
            (_wcsicmp(Item->long_filename_, L"$BadClus") == 0 ||
                _wcsicmp(Item->long_filename_, L"$BadClus:$Bad:$DATA") == 0)) {
            Item->is_unmovable_ = true;
        }

        /* Update the progress percentage. */
        Data->phase_done_ = Data->phase_done_ + 1;

        if (Data->phase_done_ % 10000 == 0) jkGui->draw_cluster(Data, 0, 0, 0);
    }

    /* Force the percentage to 100%. */
    Data->phase_done_ = Data->phase_todo_;
    jkGui->draw_cluster(Data, 0, 0, 0);

    /* Calculate the begin of the zone's. */
    calculate_zones(Data);

    /* Call the ShowAnalyze() callback one last time. */
    jkGui->show_analyze(Data, nullptr);
}

/* Move items to their zone. This will:
- Defragment all fragmented files
- Move regular files out of the directory zone.
- Move SpaceHogs out of the directory- and regular zones.
- Move items out of the MFT reserved zones
*/
void DefragLib::fixup(DefragDataStruct* data) {
    ItemStruct* item;

    uint64_t gap_begin[3];
    uint64_t gap_end[3];

    int file_zone;

    WIN32_FILE_ATTRIBUTE_DATA attributes;

    FILETIME system_time1;

    bool result;

    ULARGE_INTEGER u;

    DefragGui* gui = DefragGui::get_instance();

    call_show_status(data, 8, -1); /* "Phase 3: Fixup" */

    /* Initialize: fetch the current time. */
    GetSystemTimeAsFileTime(&system_time1);

    u.LowPart = system_time1.dwLowDateTime;
    u.HighPart = system_time1.dwHighDateTime;

    uint64_t system_time = u.QuadPart;

    /* Initialize the width of the progress bar: the total number of clusters
    of all the items. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (item->is_unmovable_ == true) continue;
        if (item->is_excluded_ == true) continue;
        if (item->clusters_count_ == 0) continue;

        data->phase_todo_ = data->phase_todo_ + item->clusters_count_;
    }

    [[maybe_unused]] uint64_t last_calc_time = system_time;

    /* Exit if nothing to do. */
    if (data->phase_todo_ == 0) return;

    /* Walk through all files and move the files that need to be moved. */
    for (file_zone = 0; file_zone < 3; file_zone++) {
        gap_begin[file_zone] = 0;
        gap_end[file_zone] = 0;
    }

    auto next_item = tree_smallest(data->item_tree_);

    while (next_item != nullptr && *data->running_ == RunningState::RUNNING) {
        /* The loop will change the position of the item in the tree, so we have
        to determine the next item before executing the loop. */
        item = next_item;

        next_item = tree_next(item);

        /* Ignore items that are unmovable or excluded. */
        if (item->is_unmovable_ == true) continue;
        if (item->is_excluded_ == true) continue;
        if (item->clusters_count_ == 0) continue;

        /* Ignore items that do not need to be moved. */
        file_zone = 1;

        if (item->is_hog_ == true) file_zone = 2;
        if (item->is_dir_ == true) file_zone = 0;

        const uint64_t item_lcn = get_item_lcn(item);

        int move_me = false;

        if (is_fragmented(item, 0, item->clusters_count_) == true) {
            /* "I am fragmented." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->debug_msg_[53]);

            move_me = true;
        }

        if (move_me == false &&
            ((item_lcn >= data->mft_excludes_[0].start_ && item_lcn < data->mft_excludes_[0].end_) ||
                (item_lcn >= data->mft_excludes_[1].start_ && item_lcn < data->mft_excludes_[1].end_) ||
                (item_lcn >= data->mft_excludes_[2].start_ && item_lcn < data->mft_excludes_[2].end_)) &&
            (data->disk_.type_ != DiskType::NTFS || match_mask(item->long_path_, L"?:\\$MFT") != true)) {
            /* "I am in MFT reserved space." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->debug_msg_[54]);

            move_me = true;
        }

        if (file_zone == 1 && item_lcn < data->zones_[1] && move_me == false) {
            /* "I am a regular file in zone 1." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->debug_msg_[55]);

            move_me = true;
        }

        if (file_zone == 2 && item_lcn < data->zones_[2] && move_me == false) {
            /* "I am a spacehog in zone 1 or 2." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->debug_msg_[56]);

            move_me = true;
        }

        if (move_me == false) {
            data->phase_done_ = data->phase_done_ + item->clusters_count_;

            continue;
        }

        /* Ignore files that have been modified less than 15 minutes ago. */
        if (item->is_dir_ == false) {
            result = GetFileAttributesExW(item->long_path_, GetFileExInfoStandard, &attributes);

            if (result != 0) {
                u.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
                u.HighPart = attributes.ftLastWriteTime.dwHighDateTime;

                if (const uint64_t FileTime = u.QuadPart; FileTime + 15 * 60 * (uint64_t)10000000 > system_time) {
                    data->phase_done_ = data->phase_done_ + item->clusters_count_;

                    continue;
                }
            }
        }

        /* If the file does not fit in the current gap then find another gap. */
        if (item->clusters_count_ > gap_end[file_zone] - gap_begin[file_zone]) {
            result = find_gap(data, data->zones_[file_zone], 0, item->clusters_count_, true, false,
                              &gap_begin[file_zone],
                              &gap_end[file_zone], FALSE);

            if (result == false) {
                /* Show debug message: "Cannot move item away because no gap is big enough: %I64d[%lu]" */
                gui->show_debug(DebugLevel::Progress, item, data->debug_msg_[25], get_item_lcn(item),
                                item->clusters_count_);

                gap_end[file_zone] = gap_begin[file_zone]; /* Force re-scan of gap. */

                data->phase_done_ = data->phase_done_ + item->clusters_count_;

                continue;
            }
        }

        /* Move the item. */
        result = move_item(data, item, gap_begin[file_zone], 0, item->clusters_count_, 0);

        if (result == true) {
            gap_begin[file_zone] = gap_begin[file_zone] + item->clusters_count_;
        }
        else {
            gap_end[file_zone] = gap_begin[file_zone]; /* Force re-scan of gap. */
        }

        /* Get new system time. */
        GetSystemTimeAsFileTime(&system_time1);

        u.LowPart = system_time1.dwLowDateTime;
        u.HighPart = system_time1.dwHighDateTime;

        system_time = u.QuadPart;
    }
}

/* Defragment all the fragmented files. */
void DefragLib::defragment(DefragDataStruct* Data) {
    ItemStruct* Item;
    ItemStruct* NextItem;

    uint64_t GapBegin;
    uint64_t GapEnd;
    uint64_t ClustersDone;
    uint64_t Clusters;

    FragmentListStruct* Fragment;

    uint64_t Vcn;
    uint64_t RealVcn;

    HANDLE FileHandle;

    int FileZone;
    int Result;

    DefragGui* jkGui = DefragGui::get_instance();

    call_show_status(Data, 2, -1); /* "Phase 2: Defragment" */

    /* Setup the width of the progress bar: the number of clusters in all
    fragmented files. */
    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = tree_next(Item)) {
        if (Item->is_unmovable_ == true) continue;
        if (Item->is_excluded_ == true) continue;
        if (Item->clusters_count_ == 0) continue;

        if (is_fragmented(Item, 0, Item->clusters_count_) == false) continue;

        Data->phase_todo_ = Data->phase_todo_ + Item->clusters_count_;
    }

    /* Exit if nothing to do. */
    if (Data->phase_todo_ == 0) return;

    /* Walk through all files and defrag. */
    NextItem = tree_smallest(Data->item_tree_);

    while (NextItem != nullptr && *Data->running_ == RunningState::RUNNING) {
        /* The loop may change the position of the item in the tree, so we have
        to determine and remember the next item now. */
        Item = NextItem;

        NextItem = tree_next(Item);

        /* Ignore if the Item cannot be moved, or is Excluded, or is not fragmented. */
        if (Item->is_unmovable_ == true) continue;
        if (Item->is_excluded_ == true) continue;
        if (Item->clusters_count_ == 0) continue;

        if (is_fragmented(Item, 0, Item->clusters_count_) == false) continue;

        /* Find a gap that is large enough to hold the item, or the largest gap
        on the volume. If the disk is full then show a message and exit. */
        FileZone = 1;

        if (Item->is_hog_ == true) FileZone = 2;
        if (Item->is_dir_ == true) FileZone = 0;

        Result = find_gap(Data, Data->zones_[FileZone], 0, Item->clusters_count_, false, false, &GapBegin, &GapEnd,
                          FALSE);

        if (Result == false) {
            /* Try finding a gap again, this time including the free area. */
            Result = find_gap(Data, 0, 0, Item->clusters_count_, false, false, &GapBegin, &GapEnd, FALSE);

            if (Result == false) {
                /* Show debug message: "Disk is full, cannot defragment." */
                jkGui->show_debug(DebugLevel::Progress, Item, Data->debug_msg_[44]);

                return;
            }
        }

        /* If the gap is big enough to hold the entire item then move the file
        in a single go, and loop. */
        if (GapEnd - GapBegin >= Item->clusters_count_) {
            move_item(Data, Item, GapBegin, 0, Item->clusters_count_, 0);

            continue;
        }

        /* Open a filehandle for the item. If error then set the Unmovable flag,
        colorize the item on the screen, and loop. */
        FileHandle = open_item_handle(Data, Item);

        if (FileHandle == nullptr) {
            Item->is_unmovable_ = true;

            colorize_item(Data, Item, 0, 0, false);

            continue;
        }

        /* Move the file in parts, each time selecting the biggest gap
        available. */
        ClustersDone = 0;

        do {
            Clusters = GapEnd - GapBegin;

            if (Clusters > Item->clusters_count_ - ClustersDone) {
                Clusters = Item->clusters_count_ - ClustersDone;
            }

            /* Make sure that the gap is bigger than the first fragment of the
            block that we're about to move. If not then the result would be
            more fragments, not less. */
            Vcn = 0;
            RealVcn = 0;

            for (Fragment = Item->fragments_; Fragment != nullptr; Fragment = Fragment->next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (RealVcn >= ClustersDone) {
                        if (Clusters > Fragment->next_vcn_ - Vcn) break;

                        ClustersDone = RealVcn + Fragment->next_vcn_ - Vcn;

                        Data->phase_done_ = Data->phase_done_ + Fragment->next_vcn_ - Vcn;
                    }

                    RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
                }

                Vcn = Fragment->next_vcn_;
            }

            if (ClustersDone >= Item->clusters_count_) break;

            /* Move the segment. */
            Result = move_item4(Data, Item, FileHandle, GapBegin, ClustersDone, Clusters, 0);

            /* Next segment. */
            ClustersDone = ClustersDone + Clusters;

            /* Find a gap large enough to hold the remainder, or the largest gap
            on the volume. */
            if (ClustersDone < Item->clusters_count_) {
                Result = find_gap(Data, Data->zones_[FileZone], 0, Item->clusters_count_ - ClustersDone,
                                  false, false, &GapBegin, &GapEnd, FALSE);

                if (Result == false) break;
            }
        }
        while (ClustersDone < Item->clusters_count_ && *Data->running_ == RunningState::RUNNING);

        /* Close the item. */
        FlushFileBuffers(FileHandle); /* Is this useful? Can't hurt. */
        CloseHandle(FileHandle);
    }
}

/* Fill all the gaps at the beginning of the disk with fragments from the files above. */
void DefragLib::forced_fill(DefragDataStruct* Data) {
    uint64_t GapBegin;
    uint64_t GapEnd;

    ItemStruct* Item;
    FragmentListStruct* Fragment;
    ItemStruct* HighestItem;

    uint64_t MaxLcn;
    uint64_t HighestLcn;
    uint64_t HighestVcn;
    uint64_t HighestSize;
    uint64_t Clusters;
    uint64_t Vcn;
    uint64_t RealVcn;

    int Result;

    call_show_status(Data, 3, -1); /* "Phase 3: ForcedFill" */

    /* Walk through all the gaps. */
    GapBegin = 0;
    MaxLcn = Data->total_clusters_;

    while (*Data->running_ == RunningState::RUNNING) {
        /* Find the next gap. If there are no more gaps then exit. */
        Result = find_gap(Data, GapBegin, 0, 0, true, false, &GapBegin, &GapEnd, FALSE);

        if (Result == false) break;

        /* Find the item with the highest fragment on disk. */
        HighestItem = nullptr;
        HighestLcn = 0;
        HighestVcn = 0;
        HighestSize = 0;

        for (Item = tree_biggest(Data->item_tree_); Item != nullptr; Item = tree_prev(Item)) {
            if (Item->is_unmovable_ == true) continue;
            if (Item->is_excluded_ == true) continue;
            if (Item->clusters_count_ == 0) continue;

            Vcn = 0;
            RealVcn = 0;

            for (Fragment = Item->fragments_; Fragment != nullptr; Fragment = Fragment->next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (Fragment->lcn_ > HighestLcn && Fragment->lcn_ < MaxLcn) {
                        HighestItem = Item;
                        HighestLcn = Fragment->lcn_;
                        HighestVcn = RealVcn;
                        HighestSize = Fragment->next_vcn_ - Vcn;
                    }

                    RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
                }

                Vcn = Fragment->next_vcn_;
            }
        }

        if (HighestItem == nullptr) break;

        /* If the highest fragment is before the gap then exit, we're finished. */
        if (HighestLcn <= GapBegin) break;

        /* Move as much of the item into the gap as possible. */
        Clusters = GapEnd - GapBegin;

        if (Clusters > HighestSize) Clusters = HighestSize;

        Result = move_item(Data, HighestItem, GapBegin, HighestVcn + HighestSize - Clusters, Clusters, 0);

        GapBegin = GapBegin + Clusters;
        MaxLcn = HighestLcn + HighestSize - Clusters;
    }
}

/* Vacate an area by moving files upward. If there are unmovable files at the Lcn then
skip them. Then move files upward until the gap is bigger than Clusters, or when we
encounter an unmovable file. */
void DefragLib::vacate(DefragDataStruct* Data, uint64_t Lcn, uint64_t Clusters, BOOL IgnoreMftExcludes) {
    uint64_t TestGapBegin;
    uint64_t TestGapEnd;
    uint64_t MoveGapBegin;
    uint64_t MoveGapEnd;

    ItemStruct* Item;
    FragmentListStruct* Fragment;

    uint64_t Vcn;
    uint64_t RealVcn;

    ItemStruct* BiggerItem;

    uint64_t BiggerBegin;
    uint64_t BiggerEnd;
    uint64_t BiggerRealVcn;
    uint64_t MoveTo;
    uint64_t DoneUntil;

    DefragGui* jkGui = DefragGui::get_instance();

    jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Vacating %I64u clusters starting at LCN=%I64u",
                      Clusters, Lcn);

    /* Sanity check. */
    if (Lcn >= Data->total_clusters_) {
        jkGui->show_debug(DebugLevel::Warning, nullptr, L"Error: trying to vacate an area beyond the end of the disk.");

        return;
    }

    /* Determine the point to above which we will be moving the data. We want at least the
    end of the zone if everything was perfectly optimized, so data will not be moved
    again and again. */
    MoveTo = Lcn + Clusters;

    if (Data->zone_ == 0) MoveTo = Data->zones_[1];
    if (Data->zone_ == 1) MoveTo = Data->zones_[2];

    if (Data->zone_ == 2) {
        /* Zone 2: end of disk minus all the free space. */
        MoveTo = Data->total_clusters_ - Data->count_free_clusters_ +
                (uint64_t)(Data->total_clusters_ * 2.0 * Data->free_space_ / 100.0);
    }

    if (MoveTo < Lcn + Clusters) MoveTo = Lcn + Clusters;

    jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"MoveTo = %I64u", MoveTo);

    /* Loop forever. */
    MoveGapBegin = 0;
    MoveGapEnd = 0;
    DoneUntil = Lcn;

    while (*Data->running_ == RunningState::RUNNING) {
        /* Find the first movable data fragment at or above the DoneUntil Lcn. If there is nothing
        then return, we have reached the end of the disk. */
        BiggerItem = nullptr;
        BiggerBegin = 0;

        for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = tree_next(Item)) {
            if (Item->is_unmovable_ == true || Item->is_excluded_ == true || Item->clusters_count_ == 0) {
                continue;
            }

            Vcn = 0;
            RealVcn = 0;

            for (Fragment = Item->fragments_; Fragment != nullptr; Fragment = Fragment->next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (Fragment->lcn_ >= DoneUntil &&
                        (BiggerBegin > Fragment->lcn_ || BiggerItem == nullptr)) {
                        BiggerItem = Item;
                        BiggerBegin = Fragment->lcn_;
                        BiggerEnd = Fragment->lcn_ + Fragment->next_vcn_ - Vcn;
                        BiggerRealVcn = RealVcn;

                        if (BiggerBegin == Lcn) break;
                    }

                    RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
                }

                Vcn = Fragment->next_vcn_;
            }

            if (BiggerBegin != 0 && BiggerBegin == Lcn) break;
        }

        if (BiggerItem == nullptr) {
            jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No data found above LCN=%I64u", Lcn);

            return;
        }

        jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Data found at LCN=%I64u, %s", BiggerBegin,
                          BiggerItem->long_path_);

        /* Find the first gap above the Lcn. */
        bool result = find_gap(Data, Lcn, 0, 0, true, false, &TestGapBegin, &TestGapEnd, IgnoreMftExcludes);

        if (result == false) {
            jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No gaps found above LCN=%I64u", Lcn);

            return;
        }

        /* Exit if the end of the first gap is below the first movable item, the gap cannot
        be enlarged. */
        if (TestGapEnd < BiggerBegin) {
            jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                              L"Cannot enlarge the gap from %I64u to %I64u (%I64u clusters) any further.",
                              TestGapBegin, TestGapEnd, TestGapEnd - TestGapBegin);

            return;
        }

        /* Exit if the first movable item is at the end of the gap and the gap is big enough,
        no need to enlarge any further. */
        if (TestGapEnd == BiggerBegin && TestGapEnd - TestGapBegin >= Clusters) {
            jkGui->show_debug(
                DebugLevel::DetailedGapFilling, nullptr,
                L"Finished vacating, the gap from %I64u to %I64u (%I64u clusters) is now bigger than %I64u clusters.",
                TestGapBegin, TestGapEnd, TestGapEnd - TestGapBegin, Clusters);

            return;
        }

        /* Exit if we have moved the item before. We don't want a worm. */
        if (Lcn >= MoveTo) {
            jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Stopping vacate because of possible worm.");
            return;
        }

        /* Determine where we want to move the fragment to. Maybe the previously used
        gap is big enough, otherwise we have to locate another gap. */
        if (BiggerEnd - BiggerBegin >= MoveGapEnd - MoveGapBegin) {
            result = false;

            /* First try to find a gap above the MoveTo point. */
            if (MoveTo < Data->total_clusters_ && MoveTo >= BiggerEnd) {
                jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Finding gap above MoveTo=%I64u", MoveTo);

                result = find_gap(Data, MoveTo, 0, BiggerEnd - BiggerBegin, true, false, &MoveGapBegin, &MoveGapEnd,
                                  FALSE);
            }

            /* If no gap was found then try to find a gap as high on disk as possible, but
            above the item. */
            if (result == false) {
                jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                  L"Finding gap from end of disk above BiggerEnd=%I64u", BiggerEnd);

                result = find_gap(Data, BiggerEnd, 0, BiggerEnd - BiggerBegin, true, true, &MoveGapBegin,
                                  &MoveGapEnd, FALSE);
            }

            /* If no gap was found then exit, we cannot move the item. */
            if (result == false) {
                jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No gap found.");

                return;
            }
        }

        /* Move the fragment to the gap. */
        result = move_item(Data, BiggerItem, MoveGapBegin, BiggerRealVcn, BiggerEnd - BiggerBegin, 0);

        if (result == true) {
            if (MoveGapBegin < MoveTo) MoveTo = MoveGapBegin;

            MoveGapBegin = MoveGapBegin + BiggerEnd - BiggerBegin;
        }
        else {
            MoveGapEnd = MoveGapBegin; /* Force re-scan of gap. */
        }

        /* Adjust the DoneUntil Lcn. We don't want an infinite loop. */
        DoneUntil = BiggerEnd;
    }
}

/* Compare two items.
SortField=0    Filename
SortField=1    Filesize, smallest first
SortField=2    Date/Time LastAccess, oldest first
SortField=3    Date/Time LastChange, oldest first
SortField=4    Date/Time Creation, oldest first
Return values:
-1   Item1 is smaller than Item2
0    Equal
1    Item1 is bigger than Item2
*/
int DefragLib::compare_items(ItemStruct* Item1, ItemStruct* Item2, int SortField) {
    int Result;

    /* If one of the items is nullptr then the other item is bigger. */
    if (Item1 == nullptr) return -1;
    if (Item2 == nullptr) return 1;

    /* Return zero if the items are exactly the same. */
    if (Item1 == Item2) return 0;

    /* Compare the SortField of the items and return 1 or -1 if they are not equal. */
    if (SortField == 0) {
        if (Item1->long_path_ == nullptr && Item2->long_path_ == nullptr) return 0;
        if (Item1->long_path_ == nullptr) return -1;
        if (Item2->long_path_ == nullptr) return 1;

        Result = _wcsicmp(Item1->long_path_, Item2->long_path_);

        if (Result != 0) return Result;
    }

    if (SortField == 1) {
        if (Item1->bytes_ < Item2->bytes_) return -1;
        if (Item1->bytes_ > Item2->bytes_) return 1;
    }

    if (SortField == 2) {
        if (Item1->last_access_time_ > Item2->last_access_time_) return -1;
        if (Item1->last_access_time_ < Item2->last_access_time_) return 1;
    }

    if (SortField == 3) {
        if (Item1->mft_change_time_ < Item2->mft_change_time_) return -1;
        if (Item1->mft_change_time_ > Item2->mft_change_time_) return 1;
    }

    if (SortField == 4) {
        if (Item1->creation_time_ < Item2->creation_time_) return -1;
        if (Item1->creation_time_ > Item2->creation_time_) return 1;
    }

    /* The SortField of the items is equal, so we must compare all the other fields
    to see if they are really equal. */
    if (Item1->long_path_ != nullptr && Item2->long_path_ != nullptr) {
        if (Item1->long_path_ == nullptr) return -1;
        if (Item2->long_path_ == nullptr) return 1;

        Result = _wcsicmp(Item1->long_path_, Item2->long_path_);

        if (Result != 0) return Result;
    }

    if (Item1->bytes_ < Item2->bytes_) return -1;
    if (Item1->bytes_ > Item2->bytes_) return 1;
    if (Item1->last_access_time_ < Item2->last_access_time_) return -1;
    if (Item1->last_access_time_ > Item2->last_access_time_) return 1;
    if (Item1->mft_change_time_ < Item2->mft_change_time_) return -1;
    if (Item1->mft_change_time_ > Item2->mft_change_time_) return 1;
    if (Item1->creation_time_ < Item2->creation_time_) return -1;
    if (Item1->creation_time_ > Item2->creation_time_) return 1;

    /* As a last resort compare the location on harddisk. */
    if (get_item_lcn(Item1) < get_item_lcn(Item2)) return -1;
    if (get_item_lcn(Item1) > get_item_lcn(Item2)) return 1;

    return 0;
}

/* Optimize the volume by moving all the files into a sorted order.
SortField=0    Filename
SortField=1    Filesize
SortField=2    Date/Time LastAccess
SortField=3    Date/Time LastChange
SortField=4    Date/Time Creation
*/
void DefragLib::optimize_sort(DefragDataStruct* data, const int sort_field) {
    uint64_t gap_begin;
    uint64_t gap_end;

    bool result;

    DefragGui* gui = DefragGui::get_instance();

    /* Sanity check. */
    if (data->item_tree_ == nullptr) return;

    /* Process all the zones. */
    [[maybe_unused]] uint64_t vacated_until = 0;
    const uint64_t minimum_vacate = data->total_clusters_ / 200;

    for (data->zone_ = 0; data->zone_ < 3; data->zone_++) {
        call_show_status(data, 4, data->zone_); /* "Zone N: Sort" */

        /* Start at the begin of the zone and move all the items there, one by one
        in the requested sorting order, making room as we go. */
        ItemStruct* previous_item = nullptr;

        uint64_t lcn = data->zones_[data->zone_];

        gap_begin = 0;
        gap_end = 0;

        while (*data->running_ == RunningState::RUNNING) {
            /* Find the next item that we want to place. */
            ItemStruct* item = nullptr;
            uint64_t phase_temp = 0;

            for (auto temp_item = tree_smallest(data->item_tree_); temp_item != nullptr; temp_item =
                 tree_next(temp_item)) {
                if (temp_item->is_unmovable_ == true) continue;
                if (temp_item->is_excluded_ == true) continue;
                if (temp_item->clusters_count_ == 0) continue;

                int file_zone = 1;

                if (temp_item->is_hog_ == true) file_zone = 2;
                if (temp_item->is_dir_ == true) file_zone = 0;
                if (file_zone != data->zone_) continue;

                if (previous_item != nullptr &&
                    compare_items(previous_item, temp_item, sort_field) >= 0) {
                    continue;
                }

                phase_temp = phase_temp + temp_item->clusters_count_;

                if (item != nullptr && compare_items(temp_item, item, sort_field) >= 0) continue;

                item = temp_item;
            }

            if (item == nullptr) {
                gui->show_debug(DebugLevel::Progress, nullptr, L"Finished sorting zone %u.", data->zone_ + 1);

                break;
            }

            previous_item = item;
            data->phase_todo_ = data->phase_done_ + phase_temp;

            /* If the item is already at the Lcn then skip. */
            if (get_item_lcn(item) == lcn) {
                lcn = lcn + item->clusters_count_;

                continue;
            }

            /* Move the item to the Lcn. If the gap at Lcn is not big enough then fragment
            the file into whatever gaps are available. */
            uint64_t clusters_done = 0;

            while (*data->running_ == RunningState::RUNNING &&
                clusters_done < item->clusters_count_ &&
                item->is_unmovable_ == false) {
                if (clusters_done > 0) {
                    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                    L"Item partially placed, %I64u clusters more to do",
                                    item->clusters_count_ - clusters_done);
                }

                /* Call the Vacate() function to make a gap at Lcn big enough to hold the item.
                The Vacate() function may not be able to move whatever is now at the Lcn, so
                after calling it we have to locate the first gap after the Lcn. */
                if (gap_begin + item->clusters_count_ - clusters_done + 16 > gap_end) {
                    vacate(data, lcn, item->clusters_count_ - clusters_done + minimum_vacate,FALSE);

                    result = find_gap(data, lcn, 0, 0, true, false, &gap_begin, &gap_end, FALSE);

                    if (result == false) return; /* No gaps found, exit. */
                }

                /* If the gap is not big enough to hold the entire item then calculate how much
                of the item will fit in the gap. */
                uint64_t clusters = item->clusters_count_ - clusters_done;

                if (clusters > gap_end - gap_begin) {
                    clusters = gap_end - gap_begin;

                    /* It looks like a partial move only succeeds if the number of clusters is a
                    multiple of 8. */
                    clusters = clusters - clusters % 8;

                    if (clusters == 0) {
                        lcn = gap_end;
                        continue;
                    }
                }

                /* Move the item to the gap. */
                result = move_item(data, item, gap_begin, clusters_done, clusters, 0);

                if (result == true) {
                    gap_begin = gap_begin + clusters;
                }
                else {
                    result = find_gap(data, gap_begin, 0, 0, true, false, &gap_begin, &gap_end, FALSE);
                    if (result == false) return; /* No gaps found, exit. */
                }

                lcn = gap_begin;
                clusters_done = clusters_done + clusters;
            }
        }
    }
}

/*

Move the MFT to the beginning of the harddisk.
- The Microsoft defragmentation api only supports moving the MFT on Vista.
- What to do if there is unmovable data at the beginning of the disk? I have
chosen to wrap the MFT around that data. The fragments will be aligned, so
the performance loss is minimal, and still faster than placing the MFT
higher on the disk.

*/
void DefragLib::move_mft_to_begin_of_disk(DefragDataStruct* data) {
    ItemStruct* item;

    uint64_t lcn;
    uint64_t gap_begin;
    uint64_t gap_end;
    uint64_t clusters;
    uint64_t clusters_done;

    bool result;

    OSVERSIONINFO os_version;

    DefragGui* gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::Progress, nullptr, L"Moving the MFT to the beginning of the volume.");

    /* Exit if this is not an NTFS disk. */
    if (data->disk_.type_ != DiskType::NTFS) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                        L"Cannot move the MFT because this is not an NTFS disk.");

        return;
    }

    /* The Microsoft defragmentation api only supports moving the MFT on Vista. */
    ZeroMemory(&os_version, sizeof(OSVERSIONINFO));

    os_version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if (GetVersionEx(&os_version) != 0 && os_version.dwMajorVersion < 6) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                        L"Cannot move the MFT because it is not supported by this version of Windows.");

        return;
    }

    /* Locate the Item for the MFT. If not found then exit. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (match_mask(item->long_path_, L"?:\\$MFT") == true) break;
    }

    if (item == nullptr) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Cannot move the MFT because I cannot find it.");

        return;
    }

    /* Exit if the MFT is at the beginning of the volume (inside zone 0) and is not
    fragmented. */
#ifdef jk
	if ((Item->Fragments != nullptr) &&
		(Item->Fragments->NextVcn == Data->Disk.MftLockedClusters) &&
		(Item->Fragments->Next != nullptr) &&
		(Item->Fragments->Next->Lcn < Data->Zones[1]) &&
		(IsFragmented(Item,Data->Disk.MftLockedClusters,Item->Clusters - Data->Disk.MftLockedClusters) == false)) {
			m_jkGui->ShowDebug(DebugLevel::DetailedGapFilling,nullptr,L"No need to move the MFT because it's already at the beginning of the volume and it's data part is not fragmented.");
			return;
	}
#endif

    lcn = 0;
    gap_begin = 0;
    gap_end = 0;
    clusters_done = data->disk_.mft_locked_clusters_;

    while (*data->running_ == RunningState::RUNNING && clusters_done < item->clusters_count_) {
        if (clusters_done > data->disk_.mft_locked_clusters_) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Partially placed, %I64u clusters more to do",
                            item->clusters_count_ - clusters_done);
        }

        /* Call the Vacate() function to make a gap at Lcn big enough to hold the MFT.
        The Vacate() function may not be able to move whatever is now at the Lcn, so
        after calling it we have to locate the first gap after the Lcn. */
        if (gap_begin + item->clusters_count_ - clusters_done + 16 > gap_end) {
            vacate(data, lcn, item->clusters_count_ - clusters_done,TRUE);

            result = find_gap(data, lcn, 0, 0, true, false, &gap_begin, &gap_end, TRUE);

            if (result == false) return; /* No gaps found, exit. */
        }

        /* If the gap is not big enough to hold the entire MFT then calculate how much
        will fit in the gap. */
        clusters = item->clusters_count_ - clusters_done;

        if (clusters > gap_end - gap_begin) {
            clusters = gap_end - gap_begin;
            /* It looks like a partial move only succeeds if the number of clusters is a
            multiple of 8. */
            clusters = clusters - clusters % 8;

            if (clusters == 0) {
                lcn = gap_end;

                continue;
            }
        }

        /* Move the MFT to the gap. */
        result = move_item(data, item, gap_begin, clusters_done, clusters, 0);

        if (result == true) {
            gap_begin = gap_begin + clusters;
        }
        else {
            result = find_gap(data, gap_begin, 0, 0, true, false, &gap_begin, &gap_end, TRUE);

            if (result == false) return; /* No gaps found, exit. */
        }

        lcn = gap_begin;
        clusters_done = clusters_done + clusters;
    }

    /* Make the MFT unmovable. We don't want it moved again by any other subroutine. */
    item->is_unmovable_ = true;

    colorize_item(data, item, 0, 0, false);
    calculate_zones(data);

    /* Note: The MftExcludes do not change by moving the MFT. */
}

/* Optimize the harddisk by filling gaps with files from above. */
void DefragLib::optimize_volume(DefragDataStruct* data) {
    ItemStruct* item;

    uint64_t gap_begin;
    uint64_t gap_end;

    DefragGui* gui = DefragGui::get_instance();

    /* Sanity check. */
    if (data->item_tree_ == nullptr) return;

    /* Process all the zones. */
    for (int zone = 0; zone < 3; zone++) {
        call_show_status(data, 5, zone); /* "Zone N: Fast Optimize" */

        /* Walk through all the gaps. */
        gap_begin = data->zones_[zone];
        int retry = 0;

        while (*data->running_ == RunningState::RUNNING) {
            /* Find the next gap. */
            bool result = find_gap(data, gap_begin, 0, 0, true, false, &gap_begin, &gap_end, FALSE);

            if (result == false) break;

            /* Update the progress counter: the number of clusters in all the files
            above the gap. Exit if there are no more files. */
            uint64_t phase_temp = 0;

            for (item = tree_biggest(data->item_tree_); item != nullptr; item = tree_prev(item)) {
                if (get_item_lcn(item) < gap_end) break;
                if (item->is_unmovable_ == true) continue;
                if (item->is_excluded_ == true) continue;

                int file_zone = 1;

                if (item->is_hog_ == true) file_zone = 2;
                if (item->is_dir_ == true) file_zone = 0;
                if (file_zone != zone) continue;

                phase_temp = phase_temp + item->clusters_count_;
            }

            data->phase_todo_ = data->phase_done_ + phase_temp;
            if (phase_temp == 0) break;

            /* Loop until the gap is filled. First look for combinations of files that perfectly
            fill the gap. If no combination can be found, or if there are less files than
            the gap is big, then fill with the highest file(s) that fit in the gap. */
            bool perfect_fit = true;
            if (gap_end - gap_begin > phase_temp) perfect_fit = false;

            while (gap_begin < gap_end && retry < 5 && *data->running_ == RunningState::RUNNING) {
                /* Find the Item that is the best fit for the gap. If nothing found (no files
                fit the gap) then exit the loop. */
                if (perfect_fit == true) {
                    item = find_best_item(data, gap_begin, gap_end, 1, zone);

                    if (item == nullptr) {
                        perfect_fit = false;

                        item = find_highest_item(data, gap_begin, gap_end, 1, zone);
                    }
                }
                else {
                    item = find_highest_item(data, gap_begin, gap_end, 1, zone);
                }

                if (item == nullptr) break;

                /* Move the item. */
                result = move_item(data, item, gap_begin, 0, item->clusters_count_, 0);

                if (result == true) {
                    gap_begin = gap_begin + item->clusters_count_;
                    retry = 0;
                }
                else {
                    gap_end = gap_begin; /* Force re-scan of gap. */
                    retry = retry + 1;
                }
            }

            /* If the gap could not be filled then skip. */
            if (gap_begin < gap_end) {
                /* Show debug message: "Skipping gap, cannot fill: %I64d[%I64d]" */
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, data->debug_msg_[28], gap_begin,
                                gap_end - gap_begin);

                gap_begin = gap_end;
                retry = 0;
            }
        }
    }
}

/* Optimize the harddisk by moving the selected items up. */
void DefragLib::optimize_up(DefragDataStruct* data) {
    ItemStruct* item;

    uint64_t gap_begin;
    uint64_t gap_end;

    DefragGui* gui = DefragGui::get_instance();

    call_show_status(data, 6, -1); /* "Phase 3: Move Up" */

    /* Setup the progress counter: the total number of clusters in all files. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        data->phase_todo_ = data->phase_todo_ + item->clusters_count_;
    }

    /* Exit if nothing to do. */
    if (data->item_tree_ == nullptr) return;

    /* Walk through all the gaps. */
    gap_end = data->total_clusters_;
    int retry = 0;

    while (*data->running_ == RunningState::RUNNING) {
        /* Find the previous gap. */
        bool result = find_gap(data, data->zones_[1], gap_end, 0, true, true, &gap_begin, &gap_end, FALSE);

        if (result == false) break;

        /* Update the progress counter: the number of clusters in all the files
        below the gap. */
        uint64_t phase_temp = 0;

        for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
            if (item->is_unmovable_ == true) continue;
            if (item->is_excluded_ == true) continue;
            if (get_item_lcn(item) >= gap_end) break;

            phase_temp = phase_temp + item->clusters_count_;
        }

        data->phase_todo_ = data->phase_done_ + phase_temp;
        if (phase_temp == 0) break;

        /* Loop until the gap is filled. First look for combinations of files that perfectly
        fill the gap. If no combination can be found, or if there are less files than
        the gap is big, then fill with the highest file(s) that fit in the gap. */
        bool perfect_fit = true;
        if (gap_end - gap_begin > phase_temp) perfect_fit = false;

        while (gap_begin < gap_end && retry < 5 && *data->running_ == RunningState::RUNNING) {
            /* Find the Item that is the best fit for the gap. If nothing found (no files
            fit the gap) then exit the loop. */
            if (perfect_fit == true) {
                item = find_best_item(data, gap_begin, gap_end, 0, 3);

                if (item == nullptr) {
                    perfect_fit = false;
                    item = find_highest_item(data, gap_begin, gap_end, 0, 3);
                }
            }
            else {
                item = find_highest_item(data, gap_begin, gap_end, 0, 3);
            }

            if (item == nullptr) break;

            /* Move the item. */
            result = move_item(data, item, gap_end - item->clusters_count_, 0, item->clusters_count_, 1);

            if (result == true) {
                gap_end = gap_end - item->clusters_count_;
                retry = 0;
            }
            else {
                gap_begin = gap_end; /* Force re-scan of gap. */
                retry = retry + 1;
            }
        }

        /* If the gap could not be filled then skip. */
        if (gap_begin < gap_end) {
            /* Show debug message: "Skipping gap, cannot fill: %I64d[%I64d]" */
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, data->debug_msg_[28], gap_begin,
                            gap_end - gap_begin);

            gap_end = gap_begin;
            retry = 0;
        }
    }
}

/* Run the defragmenter. Input is the name of a disk, mountpoint, directory, or file,
and may contain wildcards '*' and '?'. */
void DefragLib::defrag_one_path(DefragDataStruct* data, WCHAR* path, OptimizeMode opt_mode) {
    HANDLE ProcessTokenHandle;

    LUID TakeOwnershipValue;

    TOKEN_PRIVILEGES TokenPrivileges;

    STARTING_LCN_INPUT_BUFFER BitmapParam;

    struct {
        uint64_t StartingLcn;
        uint64_t BitmapSize;

        BYTE Buffer[8];
    } BitmapData;

    NTFS_VOLUME_DATA_BUFFER NtfsData;

    uint64_t FreeBytesToCaller;
    uint64_t TotalBytes;
    uint64_t FreeBytes;

    int Result;

    uint32_t ErrorCode;

    size_t Length;

    __timeb64 Time;

    FILE* Fin;

    WCHAR s1[BUFSIZ];
    WCHAR* p1;

    DWORD w;

    int i;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Initialize the data. Some items are inherited from the caller and are not
    initialized. */
    data->phase_ = 0;
    data->disk_.volume_handle_ = nullptr;
    data->disk_.mount_point_ = nullptr;
    data->disk_.mount_point_slash_ = nullptr;
    data->disk_.volume_name_[0] = 0;
    data->disk_.volume_name_slash_[0] = 0;
    data->disk_.type_ = DiskType::UnknownType;
    data->item_tree_ = nullptr;
    data->balance_count_ = 0;
    data->mft_excludes_[0].start_ = 0;
    data->mft_excludes_[0].end_ = 0;
    data->mft_excludes_[1].start_ = 0;
    data->mft_excludes_[1].end_ = 0;
    data->mft_excludes_[2].start_ = 0;
    data->mft_excludes_[2].end_ = 0;
    data->total_clusters_ = 0;
    data->bytes_per_cluster_ = 0;

    for (i = 0; i < 3; i++) data->zones_[i] = 0;

    data->cannot_move_dirs_ = 0;
    data->count_directories_ = 0;
    data->count_all_files_ = 0;
    data->count_fragmented_items_ = 0;
    data->count_all_bytes_ = 0;
    data->count_fragmented_bytes_ = 0;
    data->count_all_clusters_ = 0;
    data->count_fragmented_clusters_ = 0;
    data->count_free_clusters_ = 0;
    data->count_gaps_ = 0;
    data->biggest_gap_ = 0;
    data->count_gaps_less16_ = 0;
    data->count_clusters_less16_ = 0;
    data->phase_todo_ = 0;
    data->phase_done_ = 0;

    _ftime64_s(&Time);

    data->start_time_ = Time.time * 1000 + Time.millitm;
    data->last_checkpoint_ = data->start_time_;
    data->running_time_ = 0;

    /* Compare the item with the Exclude masks. If a mask matches then return,
    ignoring the item. */
    if (data->excludes_ != nullptr) {
        for (i = 0; data->excludes_[i] != nullptr; i++) {
            if (this->match_mask(path, data->excludes_[i]) == true) break;
            if (wcschr(data->excludes_[i], L'*') == nullptr &&
                wcslen(data->excludes_[i]) <= 3 &&
                lower_case(path[0]) == lower_case(data->excludes_[i][0]))
                break;
        }

        if (data->excludes_[i] != nullptr) {
            /* Show debug message: "Ignoring volume '%s' because of exclude mask '%s'." */
            jkGui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[47], path, data->excludes_[i]);
            return;
        }
    }

    /* Clear the screen and show "Processing '%s'" message. */
    jkGui->clear_screen(data->debug_msg_[14], path);

    /* Try to change our permissions so we can access special files and directories
    such as "C:\System Volume Information". If this does not succeed then quietly
    continue, we'll just have to do with whatever permissions we have.
    SE_BACKUP_NAME = Backup and Restore Privileges.
    */
    if (OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                         &ProcessTokenHandle) != 0 &&
        LookupPrivilegeValue(0,SE_BACKUP_NAME, &TakeOwnershipValue) != 0) {
        TokenPrivileges.PrivilegeCount = 1;
        TokenPrivileges.Privileges[0].Luid = TakeOwnershipValue;
        TokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (AdjustTokenPrivileges(ProcessTokenHandle,FALSE, &TokenPrivileges,
                                  sizeof(TOKEN_PRIVILEGES), 0, 0) == FALSE) {
            jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, L"Info: could not elevate to SeBackupPrivilege.");
        }
    }
    else {
        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, L"Info: could not elevate to SeBackupPrivilege.");
    }

    /* Try finding the MountPoint by treating the input path as a path to
    something on the disk. If this does not succeed then use the Path as
    a literal MountPoint name. */
    data->disk_.mount_point_ = _wcsdup(path);
    if (data->disk_.mount_point_ == nullptr) return;

    Result = GetVolumePathNameW(path, data->disk_.mount_point_, (uint32_t)wcslen(data->disk_.mount_point_) + 1);

    if (Result == 0) wcscpy_s(data->disk_.mount_point_, wcslen(path) + 1, path);

    /* Make two versions of the MountPoint, one with a trailing backslash and one without. */
    p1 = wcschr(data->disk_.mount_point_, 0);

    if (p1 != data->disk_.mount_point_) {
        p1--;
        if (*p1 == '\\') *p1 = 0;
    }

    Length = wcslen(data->disk_.mount_point_) + 2;

    data->disk_.mount_point_slash_ = (WCHAR*)malloc(sizeof(WCHAR) * Length);

    if (data->disk_.mount_point_slash_ == nullptr) {
        free(data->disk_.mount_point_);
        return;
    }

    swprintf_s(data->disk_.mount_point_slash_, Length, L"%s\\", data->disk_.mount_point_);

    /* Determine the name of the volume (something like
    "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\"). */
    Result = GetVolumeNameForVolumeMountPointW(data->disk_.mount_point_slash_,
                                               data->disk_.volume_name_slash_,MAX_PATH);

    if (Result == 0) {
        if (wcslen(data->disk_.mount_point_slash_) > 52 - 1 - 4) {
            /* "Cannot find volume name for mountpoint '%s': %s" */
            system_error_str(GetLastError(), s1,BUFSIZ);

            jkGui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[40], data->disk_.mount_point_slash_, s1);

            free(data->disk_.mount_point_);
            free(data->disk_.mount_point_slash_);

            return;
        }

        swprintf_s(data->disk_.volume_name_slash_, 52, L"\\\\.\\%s", data->disk_.mount_point_slash_);
    }

    /* Make a copy of the VolumeName without the trailing backslash. */
    wcscpy_s(data->disk_.volume_name_, 51, data->disk_.volume_name_slash_);

    p1 = wcschr(data->disk_.volume_name_, 0);

    if (p1 != data->disk_.volume_name_) {
        p1--;
        if (*p1 == '\\') *p1 = 0;
    }

    /* Exit if the disk is hybernated (if "?/hiberfil.sys" exists and does not begin
    with 4 zero bytes). */
    Length = wcslen(data->disk_.mount_point_slash_) + 14;

    p1 = (WCHAR*)malloc(sizeof(WCHAR) * Length);

    if (p1 == nullptr) {
        free(data->disk_.mount_point_slash_);
        free(data->disk_.mount_point_);

        return;
    }

    swprintf_s(p1, Length, L"%s\\hiberfil.sys", data->disk_.mount_point_slash_);

    Result = _wfopen_s(&Fin, p1, L"rb");

    if (Result == 0 && Fin != nullptr) {
        w = 0;

        if (fread(&w, 4, 1, Fin) == 1 && w != 0) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"Will not process this disk, it contains hybernated data.");

            free(data->disk_.mount_point_);
            free(data->disk_.mount_point_slash_);
            free(p1);

            return;
        }
    }

    free(p1);

    /* Show debug message: "Opening volume '%s' at mountpoint '%s'" */
    jkGui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[29], data->disk_.volume_name_,
                      data->disk_.mount_point_);

    /* Open the VolumeHandle. If error then leave. */
    data->disk_.volume_handle_ = CreateFileW(data->disk_.volume_name_,GENERIC_READ,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,OPEN_EXISTING, 0, nullptr);

    if (data->disk_.volume_handle_ == INVALID_HANDLE_VALUE) {
        system_error_str(GetLastError(), s1,BUFSIZ);

        jkGui->show_debug(DebugLevel::Warning, nullptr, L"Cannot open volume '%s' at mountpoint '%s': %s",
                          data->disk_.volume_name_, data->disk_.mount_point_, s1);

        free(data->disk_.mount_point_);
        free(data->disk_.mount_point_slash_);

        return;
    }

    /* Determine the maximum LCN (maximum cluster number). A single call to
    FSCTL_GET_VOLUME_BITMAP is enough, we don't have to walk through the
    entire bitmap.
    It's a pity we have to do it in this roundabout manner, because
    there is no system call that reports the total number of clusters
    in a volume. GetDiskFreeSpace() does, but is limited to 2Gb volumes,
    GetDiskFreeSpaceEx() reports in bytes, not clusters, _getdiskfree()
    requires a drive letter so cannot be used on unmounted volumes or
    volumes that are mounted on a directory, and FSCTL_GET_NTFS_VOLUME_DATA
    only works for NTFS volumes. */
    BitmapParam.StartingLcn.QuadPart = 0;

    //	long koko = FSCTL_GET_VOLUME_BITMAP;

    ErrorCode = DeviceIoControl(data->disk_.volume_handle_,FSCTL_GET_VOLUME_BITMAP,
                                &BitmapParam, sizeof BitmapParam, &BitmapData, sizeof BitmapData, &w, nullptr);

    if (ErrorCode != 0) {
        ErrorCode = NO_ERROR;
    }
    else {
        ErrorCode = GetLastError();
    }

    if (ErrorCode != NO_ERROR && ErrorCode != ERROR_MORE_DATA) {
        /* Show debug message: "Cannot defragment volume '%s' at mountpoint '%s'" */
        jkGui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[32], data->disk_.volume_name_,
                          data->disk_.mount_point_);

        CloseHandle(data->disk_.volume_handle_);

        free(data->disk_.mount_point_);
        free(data->disk_.mount_point_slash_);

        return;
    }

    data->total_clusters_ = BitmapData.StartingLcn + BitmapData.BitmapSize;

    /* Determine the number of bytes per cluster.
    Again I have to do this in a roundabout manner. As far as I know there is
    no system call that returns the number of bytes per cluster, so first I have
    to get the total size of the disk and then divide by the number of clusters.
    */
    ErrorCode = GetDiskFreeSpaceExW(path, (PULARGE_INTEGER)&FreeBytesToCaller,
                                    (PULARGE_INTEGER)&TotalBytes, (PULARGE_INTEGER)&FreeBytes);

    if (ErrorCode != 0) data->bytes_per_cluster_ = TotalBytes / data->total_clusters_;

    /* Setup the list of clusters that cannot be used. The Master File
    Table cannot be moved and cannot be used by files. All this is
    only necessary for NTFS volumes. */
    ErrorCode = DeviceIoControl(data->disk_.volume_handle_,FSCTL_GET_NTFS_VOLUME_DATA,
                                nullptr, 0, &NtfsData, sizeof NtfsData, &w, nullptr);

    if (ErrorCode != 0) {
        /* Note: NtfsData.TotalClusters.QuadPart should be exactly the same
        as the Data->TotalClusters that was determined in the previous block. */

        data->bytes_per_cluster_ = NtfsData.BytesPerCluster;

        data->mft_excludes_[0].start_ = NtfsData.MftStartLcn.QuadPart;
        data->mft_excludes_[0].end_ = NtfsData.MftStartLcn.QuadPart +
                NtfsData.MftValidDataLength.QuadPart / NtfsData.BytesPerCluster;
        data->mft_excludes_[1].start_ = NtfsData.MftZoneStart.QuadPart;
        data->mft_excludes_[1].end_ = NtfsData.MftZoneEnd.QuadPart;
        data->mft_excludes_[2].start_ = NtfsData.Mft2StartLcn.QuadPart;
        data->mft_excludes_[2].end_ = NtfsData.Mft2StartLcn.QuadPart +
                NtfsData.MftValidDataLength.QuadPart / NtfsData.BytesPerCluster;

        /* Show debug message: "MftStartLcn=%I64d, MftZoneStart=%I64d, MftZoneEnd=%I64d, Mft2StartLcn=%I64d, MftValidDataLength=%I64d" */
        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, data->debug_msg_[33],
                          NtfsData.MftStartLcn.QuadPart, NtfsData.MftZoneStart.QuadPart,
                          NtfsData.MftZoneEnd.QuadPart, NtfsData.Mft2StartLcn.QuadPart,
                          NtfsData.MftValidDataLength.QuadPart / NtfsData.BytesPerCluster);

        /* Show debug message: "MftExcludes[%u].Start=%I64d, MftExcludes[%u].End=%I64d" */
        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, data->debug_msg_[34], 0, data->mft_excludes_[0].start_,
                          0,
                          data->mft_excludes_[0].end_);
        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, data->debug_msg_[34], 1, data->mft_excludes_[1].start_,
                          1,
                          data->mft_excludes_[1].end_);
        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, data->debug_msg_[34], 2, data->mft_excludes_[2].start_,
                          2,
                          data->mft_excludes_[2].end_);
    }

    /* Fixup the input mask.
    - If the length is 2 or 3 characters then rewrite into "c:\*".
    - If it does not contain a wildcard then append '*'.
    */
    Length = wcslen(path) + 3;

    data->include_mask_ = (WCHAR*)malloc(sizeof(WCHAR) * Length);

    if (data->include_mask_ == nullptr) return;

    wcscpy_s(data->include_mask_, Length, path);

    if (wcslen(path) == 2 || wcslen(path) == 3) {
        swprintf_s(data->include_mask_, Length, L"%c:\\*", lower_case(path[0]));
    }
    else if (wcschr(path, L'*') == nullptr) {
        swprintf_s(data->include_mask_, Length, L"%s*", path);
    }

    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"Input mask: %s", data->include_mask_);

    /* Defragment and optimize. */
    jkGui->ShowDiskmap(data);

    if (*data->running_ == RunningState::RUNNING) analyze_volume(data);

    if (*data->running_ == RunningState::RUNNING && opt_mode.mode_ == 1) {
        defragment(data);
    }

    if (*data->running_ == RunningState::RUNNING && (opt_mode.mode_ == 2 || opt_mode.mode_ == 3)) {
        defragment(data);

        if (*data->running_ == RunningState::RUNNING) fixup(data);
        if (*data->running_ == RunningState::RUNNING) optimize_volume(data);
        if (*data->running_ == RunningState::RUNNING) fixup(data); /* Again, in case of new zone startpoint. */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode.mode_ == 4) {
        forced_fill(data);
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode.mode_ == 5) {
        optimize_up(data);
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode.mode_ == 6) {
        optimize_sort(data, 0); /* Filename */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode.mode_ == 7) {
        optimize_sort(data, 1); /* Filesize */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode.mode_ == 8) {
        optimize_sort(data, 2); /* Last access */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode.mode_ == 9) {
        optimize_sort(data, 3); /* Last change */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode.mode_ == 10) {
        optimize_sort(data, 4); /* Creation */
    }
    /*
    if ((*Data->Running == RUNNING) && (Mode == 11)) {
    MoveMftToBeginOfDisk(Data);
    }
    */

    call_show_status(data, 7, -1); /* "Finished." */

    /* Close the volume handles. */
    if (data->disk_.volume_handle_ != nullptr &&
        data->disk_.volume_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(data->disk_.volume_handle_);
    }

    /* Cleanup. */
    delete_item_tree(data->item_tree_);

    if (data->disk_.mount_point_ != nullptr) free(data->disk_.mount_point_);
    if (data->disk_.mount_point_slash_ != nullptr) free(data->disk_.mount_point_slash_);
}

/* Subfunction for DefragAllDisks(). It will ignore removable disks, and
will iterate for disks that are mounted on a subdirectory of another
disk (instead of being mounted on a drive). */
void DefragLib::defrag_mountpoints(DefragDataStruct* Data, WCHAR* MountPoint, OptimizeMode opt_mode) {
    WCHAR VolumeNameSlash[BUFSIZ];
    WCHAR VolumeName[BUFSIZ];

    int DriveType;

    DWORD FileSystemFlags;

    HANDLE FindMountpointHandle;

    WCHAR RootPath[MAX_PATH + BUFSIZ];
    WCHAR* FullRootPath;

    HANDLE VolumeHandle;

    int Result;

    size_t Length;

    uint32_t ErrorCode;

    WCHAR s1[BUFSIZ];
    WCHAR* p1;

    DWORD w;

    DefragGui* jkGui = DefragGui::get_instance();

    if (*Data->running_ != RunningState::RUNNING) return;

    /* Clear the screen and show message "Analyzing volume '%s'" */
    jkGui->clear_screen(Data->debug_msg_[37], MountPoint);

    /* Return if this is not a fixed disk. */
    DriveType = GetDriveTypeW(MountPoint);

    if (DriveType != DRIVE_FIXED) {
        if (DriveType == DRIVE_UNKNOWN) {
            jkGui->clear_screen(L"Ignoring volume '%s' because the drive type cannot be determined.", MountPoint);
        }

        if (DriveType == DRIVE_NO_ROOT_DIR) {
            jkGui->clear_screen(L"Ignoring volume '%s' because there is no volume mounted.", MountPoint);
        }

        if (DriveType == DRIVE_REMOVABLE) {
            jkGui->clear_screen(L"Ignoring volume '%s' because it has removable media.", MountPoint);
        }

        if (DriveType == DRIVE_REMOTE) {
            jkGui->clear_screen(L"Ignoring volume '%s' because it is a remote (network) drive.", MountPoint);
        }

        if (DriveType == DRIVE_CDROM) {
            jkGui->clear_screen(L"Ignoring volume '%s' because it is a CD-ROM drive.", MountPoint);
        }

        if (DriveType == DRIVE_RAMDISK) {
            jkGui->clear_screen(L"Ignoring volume '%s' because it is a RAM disk.", MountPoint);
        }

        return;
    }

    /* Determine the name of the volume, something like
    "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\". */
    Result = GetVolumeNameForVolumeMountPointW(MountPoint, VolumeNameSlash,BUFSIZ);

    if (Result == 0) {
        ErrorCode = GetLastError();

        if (ErrorCode == 3) {
            /* "Ignoring volume '%s' because it is not a harddisk." */
            jkGui->show_debug(DebugLevel::Fatal, nullptr, Data->debug_msg_[57], MountPoint);
        }
        else {
            /* "Cannot find volume name for mountpoint: %s" */
            system_error_str(ErrorCode, s1,BUFSIZ);

            jkGui->show_debug(DebugLevel::Fatal, nullptr, Data->debug_msg_[40], MountPoint, s1);
        }

        return;
    }

    /* Return if the disk is read-only. */
    GetVolumeInformationW(VolumeNameSlash, nullptr, 0, nullptr, nullptr, &FileSystemFlags, nullptr, 0);

    if ((FileSystemFlags & FILE_READ_ONLY_VOLUME) != 0) {
        /* Clear the screen and show message "Ignoring disk '%s' because it is read-only." */
        jkGui->clear_screen(Data->debug_msg_[36], MountPoint);

        return;
    }

    /* If the volume is not mounted then leave. Unmounted volumes can be
    defragmented, but the system administrator probably has unmounted
    the volume because he wants it untouched. */
    wcscpy_s(VolumeName,BUFSIZ, VolumeNameSlash);

    p1 = wcschr(VolumeName, 0);

    if (p1 != VolumeName) {
        p1--;
        if (*p1 == '\\') *p1 = 0;
    }

    VolumeHandle = CreateFileW(VolumeName,GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,OPEN_EXISTING, 0, nullptr);

    if (VolumeHandle == INVALID_HANDLE_VALUE) {
        system_error_str(GetLastError(), s1,BUFSIZ);

        jkGui->show_debug(DebugLevel::Warning, nullptr, L"Cannot open volume '%s' at mountpoint '%s': %s",
                          VolumeName, MountPoint, s1);

        return;
    }

    if (DeviceIoControl(VolumeHandle,FSCTL_IS_VOLUME_MOUNTED, nullptr, 0, nullptr, 0, &w, nullptr) == 0) {
        /* Show debug message: "Volume '%s' at mountpoint '%s' is not mounted." */
        jkGui->show_debug(DebugLevel::Fatal, nullptr, Data->debug_msg_[31], VolumeName, MountPoint);

        CloseHandle(VolumeHandle);

        return;
    }

    CloseHandle(VolumeHandle);

    /* Defrag the disk. */
    Length = wcslen(MountPoint) + 2;

    p1 = (WCHAR*)malloc(sizeof(WCHAR) * Length);

    if (p1 != nullptr) {
        swprintf_s(p1, Length, L"%s*", MountPoint);

        defrag_one_path(Data, p1, opt_mode);

        free(p1);
    }

    /* According to Microsoft I should check here if the disk has support for
    reparse points:
    if ((FileSystemFlags & FILE_SUPPORTS_REPARSE_POINTS) == 0) return;
    However, I have found this test will frequently cause a false return
    on Windows 2000. So I've removed it, everything seems to be working
    nicely without it. */

    /* Iterate for all the mountpoints on the disk. */
    FindMountpointHandle = FindFirstVolumeMountPointW(VolumeNameSlash, RootPath,MAX_PATH + BUFSIZ);

    if (FindMountpointHandle == INVALID_HANDLE_VALUE) return;

    do {
        Length = wcslen(MountPoint) + wcslen(RootPath) + 1;
        FullRootPath = (WCHAR*)malloc(sizeof(WCHAR) * Length);

        if (FullRootPath != nullptr) {
            swprintf_s(FullRootPath, Length, L"%s%s", MountPoint, RootPath);

            defrag_mountpoints(Data, FullRootPath, opt_mode);

            free(FullRootPath);
        }
    }
    while (FindNextVolumeMountPointW(FindMountpointHandle, RootPath,MAX_PATH + BUFSIZ) != 0);

    FindVolumeMountPointClose(FindMountpointHandle);
}

/* Run the defragger/optimizer. See the .h file for a full explanation. */
void DefragLib::run_jk_defrag(
    WCHAR* path,
    OptimizeMode optimize_mode,
    int speed,
    double free_space,
    WCHAR** excludes,
    WCHAR** space_hogs,
    RunningState* run_state,
    WCHAR** debug_msg) {
    DefragDataStruct data{};

    //	int DefaultRedrawScreen;

    uint32_t NtfsDisableLastAccessUpdate;

    LONG Result;

    HKEY Key;

    DWORD KeyDisposition;
    DWORD Length;

    WCHAR s1[BUFSIZ];

    int i;

    DefragGui* gui = DefragGui::get_instance();

    /* Copy the input values to the data struct. */
    data.speed_ = speed;
    data.free_space_ = free_space;
    data.excludes_ = excludes;

    RunningState default_running;
    if (run_state == nullptr) {
        data.running_ = &default_running;
    }
    else {
        data.running_ = run_state;
    }

    *data.running_ = RunningState::RUNNING;

    /*
        if (RedrawScreen == nullptr) {
            Data.RedrawScreen = &DefaultRedrawScreen;
        } else {
            Data.RedrawScreen = RedrawScreen;
        }
        *Data.RedrawScreen = 0;
    */

    if (debug_msg == nullptr || debug_msg[0] == nullptr) {
        data.debug_msg_ = DefaultDebugMsg;
    }
    else {
        data.debug_msg_ = debug_msg;
    }

    /* Make a copy of the SpaceHogs array. */
    data.space_hogs_ = nullptr;
    data.use_default_space_hogs_ = TRUE;

    if (space_hogs != nullptr) {
        for (i = 0; space_hogs[i] != nullptr; i++) {
            if (_wcsicmp(space_hogs[i], L"DisableDefaults") == 0) {
                data.use_default_space_hogs_ = FALSE;
            }
            else {
                data.space_hogs_ = add_array_string(data.space_hogs_, space_hogs[i]);
            }
        }
    }

    if (data.use_default_space_hogs_ == TRUE) {
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\$RECYCLE.BIN\\*"); /* Vista */
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\RECYCLED\\*"); /* FAT on 2K/XP */
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\RECYCLER\\*"); /* NTFS on 2K/XP */
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\$*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\Downloaded Installations\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\Ehome\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\Fonts\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\Help\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\I386\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\IME\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\Installer\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\ServicePackFiles\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\SoftwareDistribution\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\Speech\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\Symbols\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\ie7updates\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINDOWS\\system32\\dllcache\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINNT\\$*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINNT\\Downloaded Installations\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINNT\\I386\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINNT\\Installer\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINNT\\ServicePackFiles\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINNT\\SoftwareDistribution\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\WINNT\\ie7updates\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\*\\Installshield Installation Information\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\I386\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\System Volume Information\\*");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"?:\\windows.old\\*");

        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.7z");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.arj");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.avi");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.bak");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.bup"); /* DVD */
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.bz2");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.cab");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.chm"); /* Help files */
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.dvr-ms");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.gz");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.ifo"); /* DVD */
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.log");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.lzh");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.mp3");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.msi");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.old");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.pdf");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.rar");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.rpm");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.tar");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.wmv");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.vob"); /* DVD */
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.z");
        data.space_hogs_ = add_array_string(data.space_hogs_, L"*.zip");
    }

    /* If the NtfsDisableLastAccessUpdate setting in the registry is 1, then disable
    the LastAccessTime check for the spacehogs. */
    data.use_last_access_time_ = TRUE;

    if (data.use_default_space_hogs_ == TRUE) {
        Result = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                                 L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", 0,
                                 nullptr,REG_OPTION_NON_VOLATILE,KEY_READ, nullptr, &Key, &KeyDisposition);

        if (Result == ERROR_SUCCESS) {
            Length = sizeof NtfsDisableLastAccessUpdate;

            Result = RegQueryValueExW(Key, L"NtfsDisableLastAccessUpdate", nullptr, nullptr,
                                      (BYTE*)&NtfsDisableLastAccessUpdate, &Length);

            if (Result == ERROR_SUCCESS && NtfsDisableLastAccessUpdate == 1) {
                data.use_last_access_time_ = FALSE;
            }

            RegCloseKey(Key);
        }

        if (data.use_last_access_time_ == TRUE) {
            gui->show_debug(
                DebugLevel::Warning, nullptr,
                L"NtfsDisableLastAccessUpdate is inactive, using LastAccessTime for SpaceHogs.");
        }
        else {
            gui->show_debug(
                DebugLevel::Warning, nullptr,
                L"NtfsDisableLastAccessUpdate is active, ignoring LastAccessTime for SpaceHogs.");
        }
    }

    /* If a Path is specified then call DefragOnePath() for that path. Otherwise call
    DefragMountpoints() for every disk in the system. */
    if (path != nullptr && *path != 0) {
        defrag_one_path(&data, path, optimize_mode);
    }
    else {
        WCHAR* drives;
        uint32_t drives_size;
        drives_size = GetLogicalDriveStringsW(0, nullptr);

        drives = (WCHAR*)malloc(sizeof(WCHAR) * (drives_size + 1));

        if (drives != nullptr) {
            drives_size = GetLogicalDriveStringsW(drives_size, drives);

            if (drives_size == 0) {
                /* "Could not get list of volumes: %s" */
                system_error_str(GetLastError(), s1,BUFSIZ);

                gui->show_debug(DebugLevel::Warning, nullptr, data.debug_msg_[39], s1);
            }
            else {
                WCHAR* drive;
                drive = drives;

                while (*drive != '\0') {
                    defrag_mountpoints(&data, drive, optimize_mode);
                    while (*drive != '\0') drive++;
                    drive++;
                }
            }

            free(drives);
        }

        gui->clear_screen(data.debug_msg_[38]);
    }

    /* Cleanup. */
    if (data.space_hogs_ != nullptr) {
        for (i = 0; data.space_hogs_[i] != nullptr; i++) free(data.space_hogs_[i]);

        free(data.space_hogs_);
    }

    *data.running_ = RunningState::STOPPED;
}

/*

Stop the defragger. The "Running" variable must be the same as what was given to
the RunJkDefrag() subroutine. Wait for a maximum of TimeOut milliseconds for the
defragger to stop. If TimeOut is zero then wait indefinitely. If TimeOut is
negative then immediately return without waiting.

*/
void DefragLib::StopJkDefrag(RunningState* run_state, int TimeOut) {
    int TimeWaited;

    /* Sanity check. */
    if (run_state == nullptr) return;

    /* All loops in the library check if the Running variable is set to
    RUNNING. If not then the loop will exit. In effect this will stop
    the defragger. */
    if (*run_state == RunningState::RUNNING) {
        *run_state = RunningState::STOPPING;
    }

    /* Wait for a maximum of TimeOut milliseconds for the defragger to stop.
    If TimeOut is zero then wait indefinitely. If TimeOut is negative then
    immediately return without waiting. */
    TimeWaited = 0;

    while (TimeWaited <= TimeOut) {
        if (*run_state == RunningState::STOPPED) break;

        Sleep(100);

        if (TimeOut > 0) TimeWaited = TimeWaited + 100;
    }
}
