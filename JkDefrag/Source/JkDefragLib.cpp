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

#include "StdAfx.h"

DefragLib* DefragLib::instance_ = 0;

DefragLib::DefragLib() = default;

DefragLib::~DefragLib() {
    delete instance_;
}

DefragLib* DefragLib::getInstance() {
    if (instance_ == nullptr) {
        instance_ = new DefragLib();
    }

    return instance_;
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
    if (haystack == nullptr || needle == nullptr) return (nullptr);

    char* p1 = haystack;
    const size_t i = strlen(needle);

    while (*p1 != '\0') {
        if (_strnicmp(p1, needle, i) == 0) return (p1);

        p1++;
    }

    return (nullptr);
}

/* Search case-insensitive for a substring. */
WCHAR* DefragLib::stristr_w(WCHAR* haystack, const WCHAR* needle) {
    if (haystack == nullptr || needle == nullptr) return (nullptr);

    WCHAR* p1 = haystack;
    const size_t i = wcslen(needle);

    while (*p1 != 0) {
        if (_wcsnicmp(p1, needle, i) == 0) return (p1);

        p1++;
    }

    return (nullptr);
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

        if ((*p1 != ' ') && (*p1 != '\t') && (*p1 != '\n') && (*p1 != '\r')) break;

        *p1 = '\0';
    }

    /* Add error number. */
    swprintf_s(out, width, L"[%lu] %s", error_code, s1);
}

/* Translate character to lowercase. */
WCHAR DefragLib::lower_case(WCHAR c) {
    if ((c >= 'A') && (c <= 'Z')) return ((c - 'A') + 'a');

    return (c);
}

/* Dump a block of data to standard output, for debugging purposes. */
void DefragLib::show_hex([[maybe_unused]] struct DefragDataStruct* data, const BYTE* buffer,
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

    while ((*m != '\0') && (*s != '\0')) {
        if ((lower_case(*m) != lower_case(*s)) && (*m != '?')) {
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

    if ((*s == '\0') && (*m == '\0')) return true;

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
    if (new_string == nullptr) return (array);

    if (array == nullptr) {
        new_array = (WCHAR**)malloc(2 * sizeof(WCHAR*));

        if (new_array == nullptr) return (nullptr);

        new_array[0] = _wcsdup(new_string);

        if (new_array[0] == nullptr) return (nullptr);

        new_array[1] = nullptr;

        return (new_array);
    }

    int i = 0;

    while (array[i] != nullptr) i++;

    new_array = (WCHAR**)realloc(array, (i + 2) * sizeof(WCHAR*));

    if (new_array == nullptr) return (nullptr);

    new_array[i] = _wcsdup(new_string);

    if (new_array[i] == nullptr) return (nullptr);

    new_array[i + 1] = nullptr;

    return new_array;
}

/* Subfunction of GetShortPath(). */
void DefragLib::append_to_short_path(const struct ItemStruct* item, WCHAR* path, const size_t length) {
    if (item->ParentDirectory != nullptr) append_to_short_path(item->ParentDirectory, path, length);

    wcscat_s(path, length, L"\\");

    if (item->ShortFilename != nullptr) {
        wcscat_s(path, length, item->ShortFilename);
    }
    else if (item->LongFilename != nullptr) {
        wcscat_s(path, length, item->LongFilename);
    }
}

/*

Return a string with the full path of an item, constructed from the short names.
Return nullptr if error. The caller must free() the new string.

*/
WCHAR* DefragLib::get_short_path(const struct DefragDataStruct* data, struct ItemStruct* item) {
    /* Sanity check. */
    if (item == nullptr) return (nullptr);

    /* Count the size of all the ShortFilename's. */
    size_t length = wcslen(data->disk_.mount_point_) + 1;

    for (auto temp_item = item; temp_item != nullptr; temp_item = temp_item->ParentDirectory) {
        if (temp_item->ShortFilename != nullptr) {
            length = length + wcslen(temp_item->ShortFilename) + 1;
        }
        else if (temp_item->LongFilename != nullptr) {
            length = length + wcslen(temp_item->LongFilename) + 1;
        }
        else {
            length = length + 1;
        }
    }

    /* Allocate new string. */
    const auto path = (WCHAR*)malloc(sizeof(WCHAR) * length);

    if (path == nullptr) return (nullptr);

    wcscpy_s(path, length, data->disk_.mount_point_);

    /* Append all the strings. */
    append_to_short_path(item, path, length);

    return (path);
}

/* Subfunction of GetLongPath(). */
void DefragLib::append_to_long_path(const struct ItemStruct* item, WCHAR* path, const size_t length) {
    if (item->ParentDirectory != nullptr) append_to_long_path(item->ParentDirectory, path, length);

    wcscat_s(path, length, L"\\");

    if (item->LongFilename != nullptr) {
        wcscat_s(path, length, item->LongFilename);
    }
    else if (item->ShortFilename != nullptr) {
        wcscat_s(path, length, item->ShortFilename);
    }
}

/*

Return a string with the full path of an item, constructed from the long names.
Return nullptr if error. The caller must free() the new string.

*/
WCHAR* DefragLib::get_long_path(const struct DefragDataStruct* data, struct ItemStruct* item) {
    /* Sanity check. */
    if (item == nullptr) return (nullptr);

    /* Count the size of all the LongFilename's. */
    size_t length = wcslen(data->disk_.mount_point_) + 1;

    for (const ItemStruct* temp_item = item; temp_item != nullptr; temp_item = temp_item->ParentDirectory) {
        if (temp_item->LongFilename != nullptr) {
            length = length + wcslen(temp_item->LongFilename) + 1;
        }
        else if (item->ShortFilename != nullptr) {
            length = length + wcslen(temp_item->ShortFilename) + 1;
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
void DefragLib::slow_down(struct DefragDataStruct* Data) {
    struct __timeb64 t{};

    /* Sanity check. */
    if ((Data->Speed <= 0) || (Data->Speed >= 100)) return;

    /* Calculate the time we have to sleep so that the wall time is 100% and the
    actual running time is the "-s" parameter percentage. */
    _ftime64_s(&t);

    const int64_t now = t.time * 1000 + t.millitm;

    if (now > Data->LastCheckpoint) {
        Data->RunningTime = Data->RunningTime + now - Data->LastCheckpoint;
    }

    if (now < Data->StartTime) Data->StartTime = now; /* Should never happen. */

    /* Sleep. */
    if (Data->RunningTime > 0) {
        int64_t delay = Data->RunningTime * (int64_t)100 / (int64_t)(Data->Speed) - (now - Data->StartTime);

        if (delay > 30000) delay = 30000;
        if (delay > 0) Sleep((uint32_t)delay);
    }

    /* Save the current wall time, so next time we can calculate the time spent in	the program. */
    _ftime64_s(&t);

    Data->LastCheckpoint = t.time * 1000 + t.millitm;
}

/* Return the location on disk (LCN, Logical Cluster Number) of an item. */
uint64_t DefragLib::get_item_lcn(const struct ItemStruct* item) {
    /* Sanity check. */
    if (item == nullptr) return 0;

    const struct FragmentListStruct* fragment = item->Fragments;

    while ((fragment != nullptr) && (fragment->lcn_ == VIRTUALFRAGMENT)) {
        fragment = fragment->next_;
    }

    if (fragment == nullptr) return 0;

    return (fragment->lcn_);
}

/* Return pointer to the first item in the tree (the first file on the volume). */
struct ItemStruct* DefragLib::tree_smallest(struct ItemStruct* top) {
    if (top == nullptr) return (nullptr);

    while (top->Smaller != nullptr) top = top->Smaller;

    return (top);
}

/* Return pointer to the last item in the tree (the last file on the volume). */
struct ItemStruct* DefragLib::TreeBiggest(struct ItemStruct* Top) {
    if (Top == nullptr) return (nullptr);

    while (Top->Bigger != nullptr) Top = Top->Bigger;

    return (Top);
}

/*

If Direction=0 then return a pointer to the first file on the volume,
if Direction=1 then the last file.

*/
struct ItemStruct* DefragLib::TreeFirst(struct ItemStruct* Top, int Direction) {
    if (Direction == 0) return (tree_smallest(Top));

    return (TreeBiggest(Top));
}

/* Return pointer to the previous item in the tree. */
struct ItemStruct* DefragLib::TreePrev(struct ItemStruct* Here) {
    struct ItemStruct* Temp;

    if (Here == nullptr) return (Here);

    if (Here->Smaller != nullptr) {
        Here = Here->Smaller;

        while (Here->Bigger != nullptr) Here = Here->Bigger;

        return (Here);
    }

    do {
        Temp = Here;
        Here = Here->Parent;
    }
    while ((Here != nullptr) && (Here->Smaller == Temp));

    return (Here);
}

/* Return pointer to the next item in the tree. */
struct ItemStruct* DefragLib::TreeNext(struct ItemStruct* Here) {
    struct ItemStruct* Temp;

    if (Here == nullptr) return (nullptr);

    if (Here->Bigger != nullptr) {
        Here = Here->Bigger;

        while (Here->Smaller != nullptr) Here = Here->Smaller;

        return (Here);
    }

    do {
        Temp = Here;
        Here = Here->Parent;
    }
    while ((Here != nullptr) && (Here->Bigger == Temp));

    return (Here);
}

/*

If Direction=0 then return a pointer to the next file on the volume,
if Direction=1 then the previous file.

*/
struct ItemStruct* DefragLib::TreeNextPrev(struct ItemStruct* Here, int Direction) {
    if (Direction == 0) return (TreeNext(Here));

    return (TreePrev(Here));
}

/* Insert a record into the tree. The tree is sorted by LCN (Logical Cluster Number). */
void DefragLib::TreeInsert(struct DefragDataStruct* Data, struct ItemStruct* New) {
    struct ItemStruct* Here;
    struct ItemStruct* Ins;

    uint64_t HereLcn;
    uint64_t NewLcn;

    int Found;

    struct ItemStruct* A;
    struct ItemStruct* B;
    struct ItemStruct* C;

    long Count;
    long Skip;

    if (New == nullptr) return;

    NewLcn = get_item_lcn(New);

    /* Locate the place where the record should be inserted. */
    Here = Data->item_tree_;
    Ins = nullptr;
    Found = 1;

    while (Here != nullptr) {
        Ins = Here;
        Found = 0;

        HereLcn = get_item_lcn(Here);

        if (HereLcn > NewLcn) {
            Found = 1;
            Here = Here->Smaller;
        }
        else {
            if (HereLcn < NewLcn) Found = -1;

            Here = Here->Bigger;
        }
    }

    /* Insert the record. */
    New->Parent = Ins;
    New->Smaller = nullptr;
    New->Bigger = nullptr;

    if (Ins == nullptr) {
        Data->item_tree_ = New;
    }
    else {
        if (Found > 0) {
            Ins->Smaller = New;
        }
        else {
            Ins->Bigger = New;
        }
    }

    /* If there have been less than 1000 inserts then return. */
    Data->balance_count_ = Data->balance_count_ + 1;

    if (Data->balance_count_ < 1000) return;

    /* Balance the tree.
    It's difficult to explain what exactly happens here. For an excellent
    tutorial see:
    http://www.stanford.edu/~blp/avl/libavl.html/Balancing-a-BST.html
    */

    Data->balance_count_ = 0;

    /* Convert the tree into a vine. */
    A = Data->item_tree_;
    C = A;
    Count = 0;

    while (A != nullptr) {
        /* If A has no Bigger child then move down the tree. */
        if (A->Bigger == nullptr) {
            Count = Count + 1;
            C = A;
            A = A->Smaller;

            continue;
        }

        /* Rotate left at A. */
        B = A->Bigger;

        if (Data->item_tree_ == A) Data->item_tree_ = B;

        A->Bigger = B->Smaller;

        if (A->Bigger != nullptr) A->Bigger->Parent = A;

        B->Parent = A->Parent;

        if (B->Parent != nullptr) {
            if (B->Parent->Smaller == A) {
                B->Parent->Smaller = B;
            }
            else {
                A->Parent->Bigger = B;
            }
        }

        B->Smaller = A;
        A->Parent = B;

        /* Do again. */
        A = B;
    }

    /* Calculate the number of skips. */
    Skip = 1;

    while (Skip < Count + 2) Skip = (Skip << 1);

    Skip = Count + 1 - (Skip >> 1);

    /* Compress the tree. */
    while (C != nullptr) {
        if (Skip <= 0) C = C->Parent;

        A = C;

        while (A != nullptr) {
            B = A;
            A = A->Parent;

            if (A == nullptr) break;

            /* Rotate right at A. */
            if (Data->item_tree_ == A) Data->item_tree_ = B;

            A->Smaller = B->Bigger;

            if (A->Smaller != nullptr) A->Smaller->Parent = A;

            B->Parent = A->Parent;

            if (B->Parent != nullptr) {
                if (B->Parent->Smaller == A) {
                    B->Parent->Smaller = B;
                }
                else {
                    B->Parent->Bigger = B;
                }
            }

            A->Parent = B;
            B->Bigger = A;

            /* Next item. */
            A = B->Parent;

            /* If there were skips then leave if all done. */
            Skip = Skip - 1;
            if (Skip == 0) break;
        }
    }
}

/*

Detach (unlink) a record from the tree. The record is not freed().
See: http://www.stanford.edu/~blp/avl/libavl.html/Deleting-from-a-BST.html

*/
void DefragLib::TreeDetach(struct DefragDataStruct* Data, struct ItemStruct* Item) {
    struct ItemStruct* B;

    /* Sanity check. */
    if ((Data->item_tree_ == nullptr) || (Item == nullptr)) return;

    if (Item->Bigger == nullptr) {
        /* It is trivial to delete a node with no Bigger child. We replace
        the pointer leading to the node by it's Smaller child. In
        other words, we replace the deleted node by its Smaller child. */
        if (Item->Parent != nullptr) {
            if (Item->Parent->Smaller == Item) {
                Item->Parent->Smaller = Item->Smaller;
            }
            else {
                Item->Parent->Bigger = Item->Smaller;
            }
        }
        else {
            Data->item_tree_ = Item->Smaller;
        }

        if (Item->Smaller != nullptr) Item->Smaller->Parent = Item->Parent;
    }
    else if (Item->Bigger->Smaller == nullptr) {
        /* The Bigger child has no Smaller child. In this case, we move Bigger
        into the node's place, attaching the node's Smaller subtree as the
        new Smaller. */
        if (Item->Parent != nullptr) {
            if (Item->Parent->Smaller == Item) {
                Item->Parent->Smaller = Item->Bigger;
            }
            else {
                Item->Parent->Bigger = Item->Bigger;
            }
        }
        else {
            Data->item_tree_ = Item->Bigger;
        }

        Item->Bigger->Parent = Item->Parent;
        Item->Bigger->Smaller = Item->Smaller;

        if (Item->Smaller != nullptr) Item->Smaller->Parent = Item->Bigger;
    }
    else {
        /* Replace the node by it's inorder successor, that is, the node with
        the smallest value greater than the node. We know it exists because
        otherwise this would be case 1 or case 2, and it cannot have a Smaller
        value because that would be the node itself. The successor can
        therefore be detached and can be used to replace the node. */

        /* Find the inorder successor. */
        B = Item->Bigger;
        while (B->Smaller != nullptr) B = B->Smaller;

        /* Detach the successor. */
        if (B->Parent != nullptr) {
            if (B->Parent->Bigger == B) {
                B->Parent->Bigger = B->Bigger;
            }
            else {
                B->Parent->Smaller = B->Bigger;
            }
        }

        if (B->Bigger != nullptr) B->Bigger->Parent = B->Parent;

        /* Replace the node with the successor. */
        if (Item->Parent != nullptr) {
            if (Item->Parent->Smaller == Item) {
                Item->Parent->Smaller = B;
            }
            else {
                Item->Parent->Bigger = B;
            }
        }
        else {
            Data->item_tree_ = B;
        }

        B->Parent = Item->Parent;
        B->Smaller = Item->Smaller;

        if (B->Smaller != nullptr) B->Smaller->Parent = B;

        B->Bigger = Item->Bigger;

        if (B->Bigger != nullptr) B->Bigger->Parent = B;
    }
}

/* Delete the entire ItemTree. */
void DefragLib::DeleteItemTree(struct ItemStruct* Top) {
    struct FragmentListStruct* Fragment;

    if (Top == nullptr) return;
    if (Top->Smaller != nullptr) DeleteItemTree(Top->Smaller);
    if (Top->Bigger != nullptr) DeleteItemTree(Top->Bigger);

    if ((Top->ShortPath != nullptr) &&
        ((Top->LongPath == nullptr) ||
            (Top->ShortPath != Top->LongPath))) {
        free(Top->ShortPath);

        Top->ShortPath = nullptr;
    }

    if ((Top->ShortFilename != nullptr) &&
        ((Top->LongFilename == nullptr) ||
            (Top->ShortFilename != Top->LongFilename))) {
        free(Top->ShortFilename);

        Top->ShortFilename = nullptr;
    }

    if (Top->LongPath != nullptr) free(Top->LongPath);
    if (Top->LongFilename != nullptr) free(Top->LongFilename);

    while (Top->Fragments != nullptr) {
        Fragment = Top->Fragments->next_;

        free(Top->Fragments);

        Top->Fragments = Fragment;
    }

    free(Top);
}

/*

Return the LCN of the fragment that contains a cluster at the LCN. If the
item has no fragment that occupies the LCN then return zero.

*/
uint64_t DefragLib::FindFragmentBegin(struct ItemStruct* Item, uint64_t Lcn) {
    struct FragmentListStruct* Fragment;
    uint64_t Vcn;

    /* Sanity check. */
    if ((Item == nullptr) || (Lcn == 0)) return (0);

    /* Walk through all the fragments of the item. If a fragment is found
    that contains the LCN then return the begin of that fragment. */
    Vcn = 0;
    for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->next_) {
        if (Fragment->lcn_ != VIRTUALFRAGMENT) {
            if ((Lcn >= Fragment->lcn_) &&
                (Lcn < Fragment->lcn_ + Fragment->next_vcn_ - Vcn)) {
                return (Fragment->lcn_);
            }
        }

        Vcn = Fragment->next_vcn_;
    }

    /* Not found: return zero. */
    return (0);
}

/*

Search the list for the item that occupies the cluster at the LCN. Return a
pointer to the item. If not found then return nullptr.

*/
struct ItemStruct* DefragLib::FindItemAtLcn(struct DefragDataStruct* Data, uint64_t Lcn) {
    struct ItemStruct* Item;
    uint64_t ItemLcn;

    /* Locate the item by descending the sorted tree in memory. If found then
    return the item. */
    Item = Data->item_tree_;

    while (Item != nullptr) {
        ItemLcn = get_item_lcn(Item);

        if (ItemLcn == Lcn) return (Item);

        if (Lcn < ItemLcn) {
            Item = Item->Smaller;
        }
        else {
            Item = Item->Bigger;
        }
    }

    /* Walk through all the fragments of all the items in the sorted tree. If a
    fragment is found that occupies the LCN then return a pointer to the item. */
    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
        if (FindFragmentBegin(Item, Lcn) != 0) return (Item);
    }

    /* LCN not found, return nullptr. */
    return (nullptr);
}

/*

Open the item as a file or as a directory. If the item could not be
opened then show an error message and return nullptr.

*/
HANDLE DefragLib::OpenItemHandle(struct DefragDataStruct* Data, struct ItemStruct* Item) {
    HANDLE FileHandle;

    WCHAR ErrorString[BUFSIZ];
    WCHAR* Path;

    size_t Length;

    Length = wcslen(Item->LongPath) + 5;

    Path = (WCHAR*)malloc(sizeof(WCHAR) * Length);

    swprintf_s(Path, Length, L"\\\\?\\%s", Item->LongPath);

    if (Item->is_dir_ == false) {
        FileHandle = CreateFileW(Path,FILE_READ_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr,OPEN_EXISTING,FILE_FLAG_NO_BUFFERING, nullptr);
    }
    else {
        FileHandle = CreateFileW(Path,GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    }

    free(Path);

    if (FileHandle != INVALID_HANDLE_VALUE) return (FileHandle);

    /* Show error message: "Could not open '%s': %s" */
    system_error_str(GetLastError(), ErrorString,BUFSIZ);

    DefragGui* jkGui = DefragGui::get_instance();

    jkGui->show_debug(DebugLevel::DetailedFileInfo, nullptr, Data->DebugMsg[15], Item->LongPath, ErrorString);

    return (nullptr);
}

/*

Analyze an item (file, directory) and update it's Clusters and Fragments
in memory. If there was an error then return false, otherwise return true.
Note: Very small files are stored by Windows in the MFT and have no
clusters (zero) and no fragments (nullptr).

*/
int DefragLib::GetFragments(struct DefragDataStruct* Data, struct ItemStruct* Item, HANDLE FileHandle) {
    STARTING_VCN_INPUT_BUFFER RetrieveParam;

    struct {
        uint32_t ExtentCount;
        uint64_t StartingVcn;

        struct {
            uint64_t NextVcn;
            uint64_t Lcn;
        } Extents[1000];
    } ExtentData;

    BY_HANDLE_FILE_INFORMATION FileInformation;
    uint64_t Vcn;

    struct FragmentListStruct* NewFragment;
    struct FragmentListStruct* LastFragment;

    uint32_t ErrorCode;

    WCHAR ErrorString[BUFSIZ];

    int MaxLoop;

    ULARGE_INTEGER u;

    uint32_t i;
    DWORD w;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Initialize. If the item has an old list of fragments then delete it. */
    Item->Clusters = 0;

    while (Item->Fragments != nullptr) {
        LastFragment = Item->Fragments->next_;

        free(Item->Fragments);

        Item->Fragments = LastFragment;
    }

    /* Fetch the date/times of the file. */
    if ((Item->CreationTime == 0) &&
        (Item->LastAccessTime == 0) &&
        (Item->MftChangeTime == 0) &&
        (GetFileInformationByHandle(FileHandle, &FileInformation) != 0)) {
        u.LowPart = FileInformation.ftCreationTime.dwLowDateTime;
        u.HighPart = FileInformation.ftCreationTime.dwHighDateTime;

        Item->CreationTime = u.QuadPart;

        u.LowPart = FileInformation.ftLastAccessTime.dwLowDateTime;
        u.HighPart = FileInformation.ftLastAccessTime.dwHighDateTime;

        Item->LastAccessTime = u.QuadPart;

        u.LowPart = FileInformation.ftLastWriteTime.dwLowDateTime;
        u.HighPart = FileInformation.ftLastWriteTime.dwHighDateTime;

        Item->MftChangeTime = u.QuadPart;
    }

    /* Show debug message: "Getting cluster bitmap: %s" */
    jkGui->show_debug(DebugLevel::DetailedFileInfo, nullptr, Data->DebugMsg[10], Item->LongPath);

    /* Ask Windows for the clustermap of the item and save it in memory.
    The buffer that is used to ask Windows for the clustermap has a
    fixed size, so we may have to loop a couple of times. */
    Vcn = 0;
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
            jkGui->show_debug(DebugLevel::Progress, nullptr, L"FSCTL_GET_RETRIEVAL_POINTERS error: Infinite loop");

            return (false);
        }

        MaxLoop = MaxLoop - 1;

        /* Ask Windows for the (next segment of the) clustermap of this file. If error
        then leave the loop. */
        RetrieveParam.StartingVcn.QuadPart = Vcn;

        ErrorCode = DeviceIoControl(FileHandle,FSCTL_GET_RETRIEVAL_POINTERS,
                                    &RetrieveParam, sizeof(RetrieveParam), &ExtentData, sizeof(ExtentData), &w,
                                    nullptr);

        if (ErrorCode != 0) {
            ErrorCode = NO_ERROR;
        }
        else {
            ErrorCode = GetLastError();
        }

        if ((ErrorCode != NO_ERROR) && (ErrorCode != ERROR_MORE_DATA)) break;

        /* Walk through the clustermap, count the total number of clusters, and
        save all fragments in memory. */
        for (i = 0; i < ExtentData.ExtentCount; i++) {
            /* Show debug message. */
            if (ExtentData.Extents[i].Lcn != VIRTUALFRAGMENT) {
                /* "Extent: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u" */
                jkGui->show_debug(DebugLevel::DetailedFileInfo, nullptr, Data->DebugMsg[11], ExtentData.Extents[i].Lcn,
                                 Vcn,
                                 ExtentData.Extents[i].NextVcn);
            }
            else {
                /* "Extent (virtual): Vcn=%I64u, NextVcn=%I64u" */
                jkGui->show_debug(DebugLevel::DetailedFileInfo, nullptr, Data->DebugMsg[46], Vcn,
                                 ExtentData.Extents[i].NextVcn);
            }

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */
            if (ExtentData.Extents[i].Lcn != VIRTUALFRAGMENT) {
                Item->Clusters = Item->Clusters + ExtentData.Extents[i].NextVcn - Vcn;
            }

            /* Add the fragment to the Fragments. */
            NewFragment = (struct FragmentListStruct*)malloc(sizeof(struct FragmentListStruct));

            if (NewFragment != nullptr) {
                NewFragment->lcn_ = ExtentData.Extents[i].Lcn;
                NewFragment->next_vcn_ = ExtentData.Extents[i].NextVcn;
                NewFragment->next_ = nullptr;

                if (Item->Fragments == nullptr) {
                    Item->Fragments = NewFragment;
                }
                else {
                    if (LastFragment != nullptr) LastFragment->next_ = NewFragment;
                }

                LastFragment = NewFragment;
            }

            /* The Vcn of the next fragment is the NextVcn field in this record. */
            Vcn = ExtentData.Extents[i].NextVcn;
        }

        /* Loop until we have processed the entire clustermap of the file. */
    }
    while (ErrorCode == ERROR_MORE_DATA);

    /* If there was an error while reading the clustermap then return false. */
    if ((ErrorCode != NO_ERROR) && (ErrorCode != ERROR_HANDLE_EOF)) {
        /* Show debug message: "Cannot process clustermap of '%s': %s" */
        system_error_str(ErrorCode, ErrorString,BUFSIZ);

        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, Data->DebugMsg[43], Item->LongPath, ErrorString);

        return (false);
    }

    return (true);
}

/* Return the number of fragments in the item. */
int DefragLib::FragmentCount(struct ItemStruct* Item) {
    struct FragmentListStruct* Fragment;

    int Fragments;

    uint64_t Vcn;
    uint64_t NextLcn;

    Fragments = 0;
    Vcn = 0;
    NextLcn = 0;

    for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->next_) {
        if (Fragment->lcn_ != VIRTUALFRAGMENT) {
            if ((NextLcn != 0) && (Fragment->lcn_ != NextLcn)) Fragments++;

            NextLcn = Fragment->lcn_ + Fragment->next_vcn_ - Vcn;
        }

        Vcn = Fragment->next_vcn_;
    }

    if (NextLcn != 0) Fragments++;

    return (Fragments);
}

/*

Return true if the block in the item starting at Offset with Size clusters
is fragmented, otherwise return false.
Note: this function does not ask Windows for a fresh list of fragments,
it only looks at cached information in memory.

*/
bool DefragLib::IsFragmented(struct ItemStruct* Item, uint64_t Offset, uint64_t Size) {
    struct FragmentListStruct* Fragment;

    uint64_t FragmentBegin;
    uint64_t FragmentEnd;
    uint64_t Vcn;
    uint64_t NextLcn;

    /* Walk through all fragments. If a fragment is found where either the
    begin or the end of the fragment is inside the block then the file is
    fragmented and return true. */
    FragmentBegin = 0;
    FragmentEnd = 0;
    Vcn = 0;
    NextLcn = 0;
    Fragment = Item->Fragments;

    while (Fragment != nullptr) {
        /* Virtual fragments do not occupy space on disk and do not count as fragments. */
        if (Fragment->lcn_ != VIRTUALFRAGMENT) {
            /* Treat aligned fragments as a single fragment. Windows will frequently
            split files in fragments even though they are perfectly aligned on disk,
            especially system files and very large files. The defragger treats these
            files as unfragmented. */
            if ((NextLcn != 0) && (Fragment->lcn_ != NextLcn)) {
                /* If the fragment is above the block then return false, the block is
                not fragmented and we don't have to scan any further. */
                if (FragmentBegin >= Offset + Size) return (false);

                /* If the first cluster of the fragment is above the first cluster of
                the block, or the last cluster of the fragment is before the last
                cluster of the block, then the block is fragmented, return true. */
                if ((FragmentBegin > Offset) ||
                    ((FragmentEnd - 1 >= Offset) &&
                        (FragmentEnd - 1 < Offset + Size - 1))) {
                    return (true);
                }

                FragmentBegin = FragmentEnd;
            }

            FragmentEnd = FragmentEnd + Fragment->next_vcn_ - Vcn;
            NextLcn = Fragment->lcn_ + Fragment->next_vcn_ - Vcn;
        }

        /* Next fragment. */
        Vcn = Fragment->next_vcn_;
        Fragment = Fragment->next_;
    }

    /* Handle the last fragment. */
    if (FragmentBegin >= Offset + Size) return (false);

    if ((FragmentBegin > Offset) ||
        ((FragmentEnd - 1 >= Offset) &&
            (FragmentEnd - 1 < Offset + Size - 1))) {
        return true;
    }

    /* Return false, the item is not fragmented inside the block. */
    return false;
}

/*

Colorize an item (file, directory) on the screen in the proper color
(fragmented, unfragmented, unmovable, empty). If specified then highlight
part of the item. If Undraw=true then remove the item from the screen.
Note: the offset and size of the highlight block is in absolute clusters,
not virtual clusters.

*/
void DefragLib::ColorizeItem(struct DefragDataStruct* Data,
                             struct ItemStruct* Item,
                             uint64_t BusyOffset, /* Number of first cluster to be highlighted. */
                             uint64_t BusySize, /* Number of clusters to be highlighted. */
                             int UnDraw) /* true to undraw the file from the screen. */
{
    uint64_t Vcn;
    uint64_t RealVcn;

    uint64_t SegmentBegin;
    uint64_t SegmentEnd;

    int Color;
    int i;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Determine if the item is fragmented. */
    bool is_fragmented = IsFragmented(Item, 0, Item->Clusters);

    /* Walk through all the fragments of the file. */
    Vcn = 0;
    RealVcn = 0;

    const struct FragmentListStruct* fragment = Item->Fragments;

    while (fragment != nullptr) {
        /*
        Ignore virtual fragments. They do not occupy space on disk and do not require colorization.
        */
        if (fragment->lcn_ == VIRTUALFRAGMENT) {
            Vcn = fragment->next_vcn_;
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
        SegmentBegin = RealVcn;

        while (SegmentBegin < RealVcn + fragment->next_vcn_ - Vcn) {
            SegmentEnd = RealVcn + fragment->next_vcn_ - Vcn;

            /* Determine the color with which to draw this segment. */
            if (UnDraw == false) {
                Color = DefragStruct::COLORUNFRAGMENTED;

                if (Item->is_hog_ == true) Color = DefragStruct::COLORSPACEHOG;
                if (is_fragmented == true) Color = DefragStruct::COLORFRAGMENTED;
                if (Item->is_unmovable_ == true) Color = DefragStruct::COLORUNMOVABLE;
                if (Item->is_excluded_ == true) Color = DefragStruct::COLORUNMOVABLE;

                if ((Vcn + SegmentBegin - RealVcn < BusyOffset) &&
                    (Vcn + SegmentEnd - RealVcn > BusyOffset)) {
                    SegmentEnd = RealVcn + BusyOffset - Vcn;
                }

                if ((Vcn + SegmentBegin - RealVcn >= BusyOffset) &&
                    (Vcn + SegmentBegin - RealVcn < BusyOffset + BusySize)) {
                    if (Vcn + SegmentEnd - RealVcn > BusyOffset + BusySize) {
                        SegmentEnd = RealVcn + BusyOffset + BusySize - Vcn;
                    }

                    Color = DefragStruct::COLORBUSY;
                }
            }
            else {
                Color = DefragStruct::COLOREMPTY;

                for (i = 0; i < 3; i++) {
                    if ((fragment->lcn_ + SegmentBegin - RealVcn < Data->mft_excludes_[i].Start) &&
                        (fragment->lcn_ + SegmentEnd - RealVcn > Data->mft_excludes_[i].Start)) {
                        SegmentEnd = RealVcn + Data->mft_excludes_[i].Start - fragment->lcn_;
                    }

                    if ((fragment->lcn_ + SegmentBegin - RealVcn >= Data->mft_excludes_[i].Start) &&
                        (fragment->lcn_ + SegmentBegin - RealVcn < Data->mft_excludes_[i].End)) {
                        if (fragment->lcn_ + SegmentEnd - RealVcn > Data->mft_excludes_[i].End) {
                            SegmentEnd = RealVcn + Data->mft_excludes_[i].End - fragment->lcn_;
                        }

                        Color = DefragStruct::COLORMFT;
                    }
                }
            }

            /* Colorize the segment. */
            jkGui->draw_cluster(Data, fragment->lcn_ + SegmentBegin - RealVcn, fragment->lcn_ + SegmentEnd - RealVcn,
                               Color);

            /* Next segment. */
            SegmentBegin = SegmentEnd;
        }

        /* Next fragment. */
        RealVcn = RealVcn + fragment->next_vcn_ - Vcn;

        Vcn = fragment->next_vcn_;
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

/*

Look for a gap, a block of empty clusters on the volume.
MinimumLcn: Start scanning for gaps at this location. If there is a gap
at this location then return it. Zero is the begin of the disk.
MaximumLcn: Stop scanning for gaps at this location. Zero is the end of
the disk.
MinimumSize: The gap must have at least this many contiguous free clusters.
Zero will match any gap, so will return the first gap at or above
MinimumLcn.
MustFit: if true then only return a gap that is bigger/equal than the
MinimumSize. If false then return a gap bigger/equal than MinimumSize,
or if no such gap is found return the largest gap on the volume (above
MinimumLcn).
FindHighestGap: if false then return the lowest gap that is bigger/equal
than the MinimumSize. If true then return the highest gap.
Return true if succes, false if no gap was found or an error occurred.
The routine asks Windows for the cluster bitmap every time. It would be
faster to cache the bitmap in memory, but that would cause more fails
because of stale information.

*/
bool DefragLib::find_gap(struct DefragDataStruct* data,
                         uint64_t minimum_lcn, /* Gap must be at or above this LCN. */
                         uint64_t maximum_lcn, /* Gap must be below this LCN. */
                         uint64_t minimum_size, /* Gap must be at least this big. */
                         int must_fit, /* true: gap must be at least MinimumSize. */
                         bool find_highest_gap, /* true: return the last gap that fits. */
                         uint64_t* begin_lcn, /* Result, LCN of begin of cluster. */
                         uint64_t* end_lcn, /* Result, LCN of end of cluster. */
                         BOOL ignore_mft_excludes) const {
    STARTING_LCN_INPUT_BUFFER BitmapParam;

    struct {
        uint64_t StartingLcn;
        uint64_t BitmapSize;

        BYTE Buffer[65536]; /* Most efficient if binary multiple. */
    } BitmapData;

    uint64_t Lcn;
    uint64_t ClusterStart;
    uint64_t HighestBeginLcn;
    uint64_t HighestEndLcn;
    uint64_t LargestBeginLcn;
    uint64_t LargestEndLcn;

    int Index;
    int IndexMax;

    BYTE Mask;

    int InUse;
    int PrevInUse;

    uint32_t ErrorCode;

    WCHAR s1[BUFSIZ];

    DWORD w;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Sanity check. */
    if (minimum_lcn >= data->total_clusters_) return (false);

    /* Main loop to walk through the entire clustermap. */
    Lcn = minimum_lcn;
    ClusterStart = 0;
    PrevInUse = 1;
    HighestBeginLcn = 0;
    HighestEndLcn = 0;
    LargestBeginLcn = 0;
    LargestEndLcn = 0;

    do {
        /* Fetch a block of cluster data. If error then return false. */
        BitmapParam.StartingLcn.QuadPart = Lcn;
        ErrorCode = DeviceIoControl(data->disk_.volume_handle_,FSCTL_GET_VOLUME_BITMAP,
                                    &BitmapParam, sizeof(BitmapParam), &BitmapData, sizeof(BitmapData), &w, nullptr);

        if (ErrorCode != 0) {
            ErrorCode = NO_ERROR;
        }
        else {
            ErrorCode = GetLastError();
        }

        if ((ErrorCode != NO_ERROR) && (ErrorCode != ERROR_MORE_DATA)) {
            /* Show debug message: "ERROR: could not get volume bitmap: %s" */
            system_error_str(GetLastError(), s1,BUFSIZ);

            jkGui->show_debug(DebugLevel::Warning, nullptr, data->DebugMsg[12], s1);

            return (false);
        }

        /* Sanity check. */
        if (Lcn >= BitmapData.StartingLcn + BitmapData.BitmapSize) return (false);
        if (maximum_lcn == 0) maximum_lcn = BitmapData.StartingLcn + BitmapData.BitmapSize;

        /* Analyze the clusterdata. We resume where the previous block left
        off. If a cluster is found that matches the criteria then return
        it's LCN (Logical Cluster Number). */
        Lcn = BitmapData.StartingLcn;
        Index = 0;
        Mask = 1;

        IndexMax = sizeof(BitmapData.Buffer);

        if (BitmapData.BitmapSize / 8 < IndexMax) IndexMax = (int)(BitmapData.BitmapSize / 8);

        while ((Index < IndexMax) && (Lcn < maximum_lcn)) {
            if (Lcn >= minimum_lcn) {
                InUse = (BitmapData.Buffer[Index] & Mask);

                if (((Lcn >= data->mft_excludes_[0].Start) && (Lcn < data->mft_excludes_[0].End)) ||
                    ((Lcn >= data->mft_excludes_[1].Start) && (Lcn < data->mft_excludes_[1].End)) ||
                    ((Lcn >= data->mft_excludes_[2].Start) && (Lcn < data->mft_excludes_[2].End))) {
                    if (ignore_mft_excludes == FALSE) InUse = 1;
                }

                if ((PrevInUse == 0) && (InUse != 0)) {
                    /* Show debug message: "Gap found: LCN=%I64d, Size=%I64d" */
                    jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, data->DebugMsg[13], ClusterStart,
                                     Lcn - ClusterStart);

                    /* If the gap is bigger/equal than the mimimum size then return it,
                    or remember it, depending on the FindHighestGap parameter. */
                    if ((ClusterStart >= minimum_lcn) &&
                        (Lcn - ClusterStart >= minimum_size)) {
                        if (find_highest_gap == false) {
                            if (begin_lcn != nullptr) *begin_lcn = ClusterStart;

                            if (end_lcn != nullptr) *end_lcn = Lcn;

                            return (true);
                        }

                        HighestBeginLcn = ClusterStart;
                        HighestEndLcn = Lcn;
                    }

                    /* Remember the largest gap on the volume. */
                    if ((LargestBeginLcn == 0) ||
                        (LargestEndLcn - LargestBeginLcn < Lcn - ClusterStart)) {
                        LargestBeginLcn = ClusterStart;
                        LargestEndLcn = Lcn;
                    }
                }

                if ((PrevInUse != 0) && (InUse == 0)) ClusterStart = Lcn;

                PrevInUse = InUse;
            }

            if (Mask == 128) {
                Mask = 1;
                Index = Index + 1;
            }
            else {
                Mask = Mask << 1;
            }

            Lcn = Lcn + 1;
        }
    }
    while ((ErrorCode == ERROR_MORE_DATA) &&
        (Lcn < BitmapData.StartingLcn + BitmapData.BitmapSize) &&
        (Lcn < maximum_lcn));

    /* Process the last gap. */
    if (PrevInUse == 0) {
        /* Show debug message: "Gap found: LCN=%I64d, Size=%I64d" */
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, data->DebugMsg[13], ClusterStart, Lcn - ClusterStart);

        if ((ClusterStart >= minimum_lcn) && (Lcn - ClusterStart >= minimum_size)) {
            if (find_highest_gap == false) {
                if (begin_lcn != nullptr) *begin_lcn = ClusterStart;
                if (end_lcn != nullptr) *end_lcn = Lcn;

                return (true);
            }

            HighestBeginLcn = ClusterStart;
            HighestEndLcn = Lcn;
        }

        /* Remember the largest gap on the volume. */
        if ((LargestBeginLcn == 0) ||
            (LargestEndLcn - LargestBeginLcn < Lcn - ClusterStart)) {
            LargestBeginLcn = ClusterStart;
            LargestEndLcn = Lcn;
        }
    }

    /* If the FindHighestGap flag is true then return the highest gap we have found. */
    if ((find_highest_gap == true) && (HighestBeginLcn != 0)) {
        if (begin_lcn != nullptr) *begin_lcn = HighestBeginLcn;
        if (end_lcn != nullptr) *end_lcn = HighestEndLcn;

        return (true);
    }

    /* If the MustFit flag is false then return the largest gap we have found. */
    if ((must_fit == false) && (LargestBeginLcn != 0)) {
        if (begin_lcn != nullptr) *begin_lcn = LargestBeginLcn;
        if (end_lcn != nullptr) *end_lcn = LargestEndLcn;

        return (true);
    }

    /* No gap found, return false. */
    return (false);
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
void DefragLib::CalculateZones(struct DefragDataStruct* Data) {
    struct ItemStruct* Item;

    struct FragmentListStruct* Fragment;

    uint64_t SizeOfMovableFiles[3];
    uint64_t SizeOfUnmovableFragments[3];
    uint64_t ZoneEnd[3];
    uint64_t OldZoneEnd[3];
    uint64_t Vcn;
    uint64_t RealVcn;

    int Zone;
    int Iterate;
    int i;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Calculate the number of clusters in movable items for every zone. */
    for (Zone = 0; Zone <= 2; Zone++) SizeOfMovableFiles[Zone] = 0;

    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
        if (Item->is_unmovable_ == true) continue;
        if (Item->is_excluded_ == true) continue;
        if ((Item->is_dir_ == true) && (Data->cannot_move_dirs_ > 20)) continue;

        Zone = 1;

        if (Item->is_hog_ == true) Zone = 2;
        if (Item->is_dir_ == true) Zone = 0;

        SizeOfMovableFiles[Zone] = SizeOfMovableFiles[Zone] + Item->Clusters;
    }

    /* Iterate until the calculation does not change anymore, max 10 times. */
    for (Zone = 0; Zone <= 2; Zone++) SizeOfUnmovableFragments[Zone] = 0;

    for (Zone = 0; Zone <= 2; Zone++) OldZoneEnd[Zone] = 0;

    for (Iterate = 1; Iterate <= 10; Iterate++) {
        /* Calculate the end of the zones. */
        ZoneEnd[0] = SizeOfMovableFiles[0] + SizeOfUnmovableFragments[0] +
                (uint64_t)(Data->total_clusters_ * Data->free_space_ / 100.0);

        ZoneEnd[1] = ZoneEnd[0] + SizeOfMovableFiles[1] + SizeOfUnmovableFragments[1] +
                (uint64_t)(Data->total_clusters_ * Data->free_space_ / 100.0);

        ZoneEnd[2] = ZoneEnd[1] + SizeOfMovableFiles[2] + SizeOfUnmovableFragments[2];

        /* Exit if there was no change. */
        if ((OldZoneEnd[0] == ZoneEnd[0]) &&
            (OldZoneEnd[1] == ZoneEnd[1]) &&
            (OldZoneEnd[2] == ZoneEnd[2]))
            break;

        for (Zone = 0; Zone <= 2; Zone++) OldZoneEnd[Zone] = ZoneEnd[Zone];

        /* Show debug info. */
        jkGui->show_debug(DebugLevel::DetailedFileInfo, nullptr,
                         L"Zone calculation, iteration %u: 0 - %I64d - %I64d - %I64d", Iterate,
                         ZoneEnd[0], ZoneEnd[1], ZoneEnd[2]);

        /* Reset the SizeOfUnmovableFragments array. We are going to (re)calculate these numbers
        based on the just calculates ZoneEnd's. */
        for (Zone = 0; Zone <= 2; Zone++) SizeOfUnmovableFragments[Zone] = 0;

        /* The MFT reserved areas are counted as unmovable data. */
        for (i = 0; i < 3; i++) {
            if (Data->mft_excludes_[i].Start < ZoneEnd[0]) {
                SizeOfUnmovableFragments[0] = SizeOfUnmovableFragments[0] + Data->mft_excludes_[i].End - Data->
                        mft_excludes_
                        [i].Start;
            }
            else if (Data->mft_excludes_[i].Start < ZoneEnd[1]) {
                SizeOfUnmovableFragments[1] = SizeOfUnmovableFragments[1] + Data->mft_excludes_[i].End - Data->
                        mft_excludes_
                        [i].Start;
            }
            else if (Data->mft_excludes_[i].Start < ZoneEnd[2]) {
                SizeOfUnmovableFragments[2] = SizeOfUnmovableFragments[2] + Data->mft_excludes_[i].End - Data->
                        mft_excludes_
                        [i].Start;
            }
        }

        /* Walk through all items and count the unmovable fragments. Ignore unmovable fragments
        in the MFT zones, we have already counted the zones. */
        for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
            if ((Item->is_unmovable_ == false) &&
                (Item->is_excluded_ == false) &&
                ((Item->is_dir_ == false) || (Data->cannot_move_dirs_ <= 20)))
                continue;

            Vcn = 0;
            RealVcn = 0;

            for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (((Fragment->lcn_ < Data->mft_excludes_[0].Start) || (Fragment->lcn_ >= Data->mft_excludes_[0].
                            End))
                        &&
                        ((Fragment->lcn_ < Data->mft_excludes_[1].Start) || (Fragment->lcn_ >= Data->mft_excludes_[1].
                            End))
                        &&
                        ((Fragment->lcn_ < Data->mft_excludes_[2].Start) || (Fragment->lcn_ >= Data->mft_excludes_[2].
                            End))) {
                        if (Fragment->lcn_ < ZoneEnd[0]) {
                            SizeOfUnmovableFragments[0] = SizeOfUnmovableFragments[0] + Fragment->next_vcn_ - Vcn;
                        }
                        else if (Fragment->lcn_ < ZoneEnd[1]) {
                            SizeOfUnmovableFragments[1] = SizeOfUnmovableFragments[1] + Fragment->next_vcn_ - Vcn;
                        }
                        else if (Fragment->lcn_ < ZoneEnd[2]) {
                            SizeOfUnmovableFragments[2] = SizeOfUnmovableFragments[2] + Fragment->next_vcn_ - Vcn;
                        }
                    }

                    RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
                }

                Vcn = Fragment->next_vcn_;
            }
        }
    }

    /* Calculated the begin of the zones. */
    Data->zones_[0] = 0;

    for (i = 1; i <= 3; i++) Data->zones_[i] = ZoneEnd[i - 1];
}

/*

Subfunction for MoveItem(), see below. Move (part of) an item to a new
location on disk. Return errorcode from DeviceIoControl().
The file is moved in a single FSCTL_MOVE_FILE call. If the file has
fragments then Windows will join them up.
Note: the offset and size of the block is in absolute clusters, not
virtual clusters.

*/
uint32_t DefragLib::MoveItem1(struct DefragDataStruct* Data,
                              HANDLE FileHandle,
                              struct ItemStruct* Item,
                              uint64_t NewLcn, /* Where to move to. */
                              uint64_t Offset, /* Number of first cluster to be moved. */
                              uint64_t Size) /* Number of clusters to be moved. */
{
    MOVE_FILE_DATA MoveParams;

    struct FragmentListStruct* Fragment;

    uint64_t Vcn;
    uint64_t RealVcn;
    uint64_t Lcn;

    uint32_t ErrorCode;
    DWORD w;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Find the first fragment that contains clusters inside the block, so we
    can translate the absolute cluster number of the block into the virtual
    cluster number used by Windows. */
    Vcn = 0;
    RealVcn = 0;

    for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->next_) {
        if (Fragment->lcn_ != VIRTUALFRAGMENT) {
            if (RealVcn + Fragment->next_vcn_ - Vcn - 1 >= Offset) break;

            RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
        }

        Vcn = Fragment->next_vcn_;
    }

    /* Setup the parameters for the move. */
    MoveParams.FileHandle = FileHandle;
    MoveParams.StartingLcn.QuadPart = NewLcn;
    MoveParams.StartingVcn.QuadPart = Vcn + (Offset - RealVcn);
    MoveParams.ClusterCount = (uint32_t)(Size);

    if (Fragment == nullptr) {
        Lcn = 0;
    }
    else {
        Lcn = Fragment->lcn_ + (Offset - RealVcn);
    }

    /* Show progress message. */
    jkGui->show_move(Item, MoveParams.ClusterCount, Lcn, NewLcn, MoveParams.StartingVcn.QuadPart);

    /* Draw the item and the destination clusters on the screen in the BUSY	color. */
    ColorizeItem(Data, Item, MoveParams.StartingVcn.QuadPart, MoveParams.ClusterCount, false);

    jkGui->draw_cluster(Data, NewLcn, NewLcn + Size, DefragStruct::COLORBUSY);

    /* Call Windows to perform the move. */
    ErrorCode = DeviceIoControl(Data->disk_.volume_handle_,FSCTL_MOVE_FILE, &MoveParams,
                                sizeof(MoveParams), nullptr, 0, &w, nullptr);

    if (ErrorCode != 0) {
        ErrorCode = NO_ERROR;
    }
    else {
        ErrorCode = GetLastError();
    }

    /* Update the PhaseDone counter for the progress bar. */
    Data->PhaseDone = Data->PhaseDone + MoveParams.ClusterCount;

    /* Undraw the destination clusters on the screen. */
    jkGui->draw_cluster(Data, NewLcn, NewLcn + Size, DefragStruct::COLOREMPTY);

    return (ErrorCode);
}

/*

Subfunction for MoveItem(), see below. Move (part of) an item to a new
location on disk. Return errorcode from DeviceIoControl().
Move the item one fragment at a time, a FSCTL_MOVE_FILE call per fragment.
The fragments will be lined up on disk and the defragger will treat the
item as unfragmented.
Note: the offset and size of the block is in absolute clusters, not
virtual clusters.

*/
uint32_t DefragLib::MoveItem2(struct DefragDataStruct* Data,
                              HANDLE FileHandle,
                              struct ItemStruct* Item,
                              uint64_t NewLcn, /* Where to move to. */
                              uint64_t Offset, /* Number of first cluster to be moved. */
                              uint64_t Size) /* Number of clusters to be moved. */
{
    MOVE_FILE_DATA MoveParams;

    struct FragmentListStruct* Fragment;

    uint64_t Vcn;
    uint64_t RealVcn;
    uint64_t FromLcn;

    uint32_t ErrorCode;
    DWORD w;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Walk through the fragments of the item and move them one by one to the new location. */
    ErrorCode = NO_ERROR;
    Vcn = 0;
    RealVcn = 0;

    for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->next_) {
        if (*Data->running_ != RunningState::RUNNING) break;

        if (Fragment->lcn_ != VIRTUALFRAGMENT) {
            if (RealVcn >= Offset + Size) break;

            if (RealVcn + Fragment->next_vcn_ - Vcn - 1 >= Offset) {
                /* Setup the parameters for the move. If the block that we want to move
                begins somewhere in the middle of a fragment then we have to setup
                slightly differently than when the fragment is at or after the begin
                of the block. */
                MoveParams.FileHandle = FileHandle;

                if (RealVcn < Offset) {
                    /* The fragment starts before the Offset and overlaps. Move the
                    part of the fragment from the Offset until the end of the
                    fragment or the block. */
                    MoveParams.StartingLcn.QuadPart = NewLcn;
                    MoveParams.StartingVcn.QuadPart = Vcn + (Offset - RealVcn);

                    if (Size < (Fragment->next_vcn_ - Vcn) - (Offset - RealVcn)) {
                        MoveParams.ClusterCount = (uint32_t)Size;
                    }
                    else {
                        MoveParams.ClusterCount = (uint32_t)((Fragment->next_vcn_ - Vcn) - (Offset - RealVcn));
                    }

                    FromLcn = Fragment->lcn_ + (Offset - RealVcn);
                }
                else {
                    /* The fragment starts at or after the Offset. Move the part of
                    the fragment inside the block (up until Offset+Size). */
                    MoveParams.StartingLcn.QuadPart = NewLcn + RealVcn - Offset;
                    MoveParams.StartingVcn.QuadPart = Vcn;

                    if (Fragment->next_vcn_ - Vcn < Offset + Size - RealVcn) {
                        MoveParams.ClusterCount = (uint32_t)(Fragment->next_vcn_ - Vcn);
                    }
                    else {
                        MoveParams.ClusterCount = (uint32_t)(Offset + Size - RealVcn);
                    }
                    FromLcn = Fragment->lcn_;
                }

                /* Show progress message. */
                jkGui->show_move(Item, MoveParams.ClusterCount, FromLcn, MoveParams.StartingLcn.QuadPart,
                                MoveParams.StartingVcn.QuadPart);

                /* Draw the item and the destination clusters on the screen in the BUSY	color. */
                //				if (*Data->RedrawScreen == 0) {
                ColorizeItem(Data, Item, MoveParams.StartingVcn.QuadPart, MoveParams.ClusterCount, false);
                //				} else {
                //					m_jkGui->ShowDiskmap(Data);
                //				}

                jkGui->draw_cluster(Data, MoveParams.StartingLcn.QuadPart,
                                   MoveParams.StartingLcn.QuadPart + MoveParams.ClusterCount,
                                   DefragStruct::COLORBUSY);

                /* Call Windows to perform the move. */
                ErrorCode = DeviceIoControl(Data->disk_.volume_handle_,FSCTL_MOVE_FILE, &MoveParams,
                                            sizeof(MoveParams), nullptr, 0, &w, nullptr);

                if (ErrorCode != 0) {
                    ErrorCode = NO_ERROR;
                }
                else {
                    ErrorCode = GetLastError();
                }

                /* Update the PhaseDone counter for the progress bar. */
                Data->PhaseDone = Data->PhaseDone + MoveParams.ClusterCount;

                /* Undraw the destination clusters on the screen. */
                jkGui->draw_cluster(Data, MoveParams.StartingLcn.QuadPart,
                                   MoveParams.StartingLcn.QuadPart + MoveParams.ClusterCount,
                                   DefragStruct::COLOREMPTY);

                /* If there was an error then exit. */
                if (ErrorCode != NO_ERROR) return (ErrorCode);
            }

            RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
        }

        /* Next fragment. */
        Vcn = Fragment->next_vcn_;
    }

    return (ErrorCode);
}

/*

Subfunction for MoveItem(), see below. Move (part of) an item to a new
location on disk. Return true if success, false if failure.
Strategy 0: move the block in a single FSCTL_MOVE_FILE call. If the block
has fragments then Windows will join them up.
Strategy 1: move the block one fragment at a time. The fragments will be
lined up on disk and the defragger will treat them as unfragmented.
Note: the offset and size of the block is in absolute clusters, not
virtual clusters.

*/
int DefragLib::MoveItem3(struct DefragDataStruct* Data,
                         struct ItemStruct* Item,
                         HANDLE FileHandle,
                         uint64_t NewLcn, /* Where to move to. */
                         uint64_t Offset, /* Number of first cluster to be moved. */
                         uint64_t Size, /* Number of clusters to be moved. */
                         int Strategy) /* 0: move in one part, 1: move individual fragments. */
{
    uint32_t ErrorCode;

    WCHAR ErrorString[BUFSIZ];

    int Result;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Slow the program down if so selected. */
    slow_down(Data);

    /* Move the item, either in a single block or fragment by fragment. */
    if (Strategy == 0) {
        ErrorCode = MoveItem1(Data, FileHandle, Item, NewLcn, Offset, Size);
    }
    else {
        ErrorCode = MoveItem2(Data, FileHandle, Item, NewLcn, Offset, Size);
    }

    /* If there was an error then fetch the errormessage and save it. */
    if (ErrorCode != NO_ERROR) system_error_str(ErrorCode, ErrorString,BUFSIZ);

    /* Fetch the new fragment map of the item and refresh the screen. */
    ColorizeItem(Data, Item, 0, 0, true);

    TreeDetach(Data, Item);

    Result = GetFragments(Data, Item, FileHandle);

    TreeInsert(Data, Item);

    //		if (*Data->RedrawScreen == 0) {
    ColorizeItem(Data, Item, 0, 0, false);
    //		} else {
    //			m_jkGui->ShowDiskmap(Data);
    //		}

    /* If Windows reported an error while moving the item then show the
    errormessage and return false. */
    if (ErrorCode != NO_ERROR) {
        jkGui->show_debug(DebugLevel::DetailedProgress, Item, ErrorString);

        return (false);
    }

    /* If there was an error analyzing the item then return false. */
    if (Result == false) return (false);

    return (true);
}

/*

Subfunction for MoveItem(), see below. Move the item with strategy 0.
If this results in fragmentation then try again using strategy 1.
Return true if success, false if failed to move without fragmenting the
item.
Note: The Windows defragmentation API does not report an error if it only
moves part of the file and has fragmented the file. This can for example
happen when part of the file is locked and cannot be moved, or when (part
of) the gap was previously in use by another file but has not yet been
released by the NTFS checkpoint system.
Note: the offset and size of the block is in absolute clusters, not
virtual clusters.

*/
int DefragLib::MoveItem4(struct DefragDataStruct* Data,
                         struct ItemStruct* Item,
                         HANDLE FileHandle,
                         uint64_t NewLcn, /* Where to move to. */
                         uint64_t Offset, /* Number of first cluster to be moved. */
                         uint64_t Size, /* Number of clusters to be moved. */
                         int Direction) /* 0: move up, 1: move down. */
{
    uint64_t OldLcn;
    uint64_t ClusterStart;
    uint64_t ClusterEnd;

    int Result;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Remember the current position on disk of the item. */
    OldLcn = get_item_lcn(Item);

    /* Move the Item to the requested LCN. If error then return false. */
    Result = MoveItem3(Data, Item, FileHandle, NewLcn, Offset, Size, 0);

    if (Result == false) return (false);
    if (*Data->running_ != RunningState::RUNNING) return (false);

    /* If the block is not fragmented then return true. */
    if (IsFragmented(Item, Offset, Size) == false) return (true);

    /* Show debug message: "Windows could not move the file, trying alternative method." */
    jkGui->show_debug(DebugLevel::DetailedProgress, Item, Data->DebugMsg[42]);

    /* Find another gap on disk for the item. */
    if (Direction == 0) {
        ClusterStart = OldLcn + Item->Clusters;

        if ((ClusterStart + Item->Clusters >= NewLcn) &&
            (ClusterStart < NewLcn + Item->Clusters)) {
            ClusterStart = NewLcn + Item->Clusters;
        }

        Result = find_gap(Data, ClusterStart, 0, Size, true, false, &ClusterStart, &ClusterEnd, FALSE);
    }
    else {
        Result = find_gap(Data, Data->zones_[1], OldLcn, Size, true, true, &ClusterStart, &ClusterEnd, FALSE);
    }

    if (Result == false) return (false);

    /* Add the size of the item to the width of the progress bar, we have discovered
    that we have more work to do. */
    Data->PhaseTodo = Data->PhaseTodo + Size;

    /* Move the item to the other gap using strategy 1. */
    if (Direction == 0) {
        Result = MoveItem3(Data, Item, FileHandle, ClusterStart, Offset, Size, 1);
    }
    else {
        Result = MoveItem3(Data, Item, FileHandle, ClusterEnd - Size, Offset, Size, 1);
    }

    if (Result == false) return (false);

    /* If the block is still fragmented then return false. */
    if (IsFragmented(Item, Offset, Size) == true) {
        /* Show debug message: "Alternative method failed, leaving file where it is." */
        jkGui->show_debug(DebugLevel::DetailedProgress, Item, Data->DebugMsg[45]);

        return (false);
    }

    jkGui->show_debug(DebugLevel::DetailedProgress, Item, L"");

    /* Add the size of the item to the width of the progress bar, we have more work to do. */
    Data->PhaseTodo = Data->PhaseTodo + Size;

    /* Strategy 1 has helped. Move the Item again to where we want it, but
    this time use strategy 1. */
    Result = MoveItem3(Data, Item, FileHandle, NewLcn, Offset, Size, 1);

    return (Result);
}

/*

Move (part of) an item to a new location on disk. Moving the Item will
automatically defragment it. If unsuccesful then set the Unmovable
flag of the item and return false, otherwise return true.
Note: the item will move to a different location in the tree.
Note: the offset and size of the block is in absolute clusters, not
virtual clusters.

*/
int DefragLib::MoveItem(struct DefragDataStruct* Data,
                        struct ItemStruct* Item,
                        uint64_t NewLcn, /* Where to move to. */
                        uint64_t Offset, /* Number of first cluster to be moved. */
                        uint64_t Size, /* Number of clusters to be moved. */
                        int Direction) /* 0: move up, 1: move down. */
{
    uint64_t ClustersTodo;

    /* If the Item is Unmovable, Excluded, or has zero size then we cannot move it. */
    if (Item->is_unmovable_ == true) return (false);
    if (Item->is_excluded_ == true) return (false);
    if (Item->Clusters == 0) return (false);

    /* Directories cannot be moved on FAT volumes. This is a known Windows limitation
    and not a bug in JkDefrag. But JkDefrag will still try, to allow for possible
    circumstances where the Windows defragmentation API can move them after all.
    To speed up things we count the number of directories that could not be moved,
    and when it reaches 20 we ignore all directories from then on. */
    if ((Item->is_dir_ == true) && (Data->cannot_move_dirs_ > 20)) {
        Item->is_unmovable_ = true;

        ColorizeItem(Data, Item, 0, 0, false);

        return (false);
    }

    /* Open a filehandle for the item and call the subfunctions (see above) to
    move the file. If success then return true. */
    uint64_t clusters_done = 0;
    bool result = true;

    while ((clusters_done < Size) && (*Data->running_ == RunningState::RUNNING)) {
        ClustersTodo = Size - clusters_done;

        if (Data->bytes_per_cluster_ > 0) {
            if (ClustersTodo > 1073741824 / Data->bytes_per_cluster_) {
                ClustersTodo = 1073741824 / Data->bytes_per_cluster_;
            }
        }
        else {
            if (ClustersTodo > 262144) ClustersTodo = 262144;
        }

        const HANDLE file_handle = OpenItemHandle(Data, Item);

        result = false;

        if (file_handle == nullptr) break;

        result = MoveItem4(Data, Item, file_handle, NewLcn + clusters_done, Offset + clusters_done,
                           ClustersTodo, Direction);

        if (result == false) break;

        clusters_done = clusters_done + ClustersTodo;

        FlushFileBuffers(file_handle); /* Is this useful? Can't hurt. */
        CloseHandle(file_handle);
    }

    if (result == true) {
        if (Item->is_dir_ == true) Data->cannot_move_dirs_ = 0;

        return (true);
    }

    /* If error then set the Unmovable flag, colorize the item on the screen, recalculate
    the begin of the zone's, and return false. */
    Item->is_unmovable_ = true;

    if (Item->is_dir_ == true) Data->cannot_move_dirs_++;

    ColorizeItem(Data, Item, 0, 0, false);
    CalculateZones(Data);

    return (false);
}

/*

Look in the ItemTree and return the highest file above the gap that fits inside
the gap (cluster start - cluster end). Return a pointer to the item, or nullptr if
no file could be found.
Direction=0      Search for files below the gap.
Direction=1      Search for files above the gap.
Zone=0           Only search the directories.
Zone=1           Only search the regular files.
Zone=2           Only search the SpaceHogs.
Zone=3           Search all items.

*/
struct ItemStruct* DefragLib::FindHighestItem(const struct DefragDataStruct* data,
                                              uint64_t ClusterStart,
                                              uint64_t ClusterEnd,
                                              int Direction,
                                              int Zone) {
    uint64_t ItemLcn;

    int FileZone;

    DefragGui* jkGui = DefragGui::get_instance();

    /* "Looking for highest-fit %I64d[%I64d]" */
    jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Looking for highest-fit %I64d[%I64d]",
                     ClusterStart, ClusterEnd - ClusterStart);

    /* Walk backwards through all the items on disk and select the first
    file that fits inside the free block. If we find an exact match then
    immediately return it. */
    for (auto item = TreeFirst(data->item_tree_, Direction);
         item != nullptr;
         item = TreeNextPrev(item, Direction)) {
        ItemLcn = get_item_lcn(item);

        if (ItemLcn == 0) continue;

        if (Direction == 1) {
            if (ItemLcn < ClusterEnd) return (nullptr);
        }
        else {
            if (ItemLcn > ClusterStart) return (nullptr);
        }

        if (item->is_unmovable_ == true) continue;
        if (item->is_excluded_ == true) continue;

        if (Zone != 3) {
            FileZone = 1;

            if (item->is_hog_ == true) FileZone = 2;
            if (item->is_dir_ == true) FileZone = 0;
            if (Zone != FileZone) continue;
        }

        if (item->Clusters > ClusterEnd - ClusterStart) continue;

        return (item);
    }

    return (nullptr);
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
struct ItemStruct* DefragLib::FindBestItem(const struct DefragDataStruct* data,
                                           uint64_t ClusterStart,
                                           uint64_t ClusterEnd,
                                           int Direction,
                                           int Zone) {
    struct ItemStruct* FirstItem;

    uint64_t GapSize;
    uint64_t TotalItemsSize;

    int FileZone;

    struct __timeb64 Time;

    int64_t MaxTime;

    DefragGui* jkGui = DefragGui::get_instance();

    jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Looking for perfect fit %I64d[%I64d]",
                     ClusterStart, ClusterEnd - ClusterStart);

    /* Walk backwards through all the items on disk and select the first item that
    fits inside the free block, and combined with other items will fill the gap
    perfectly. If we find an exact match then immediately return it. */

    _ftime64_s(&Time);

    MaxTime = Time.time * 1000 + Time.millitm + 500;
    FirstItem = nullptr;
    GapSize = ClusterEnd - ClusterStart;
    TotalItemsSize = 0;

    for (auto item = TreeFirst(data->item_tree_, Direction);
         item != nullptr;
         item = TreeNextPrev(item, Direction)) {
        /* If we have passed the top of the gap then.... */
        const uint64_t item_lcn = get_item_lcn(item);

        if (item_lcn == 0) continue;

        if (((Direction == 1) && (item_lcn < ClusterEnd)) ||
            ((Direction == 0) && (item_lcn > ClusterEnd))) {
            /* If we did not find an item that fits inside the gap then exit. */
            if (FirstItem == nullptr) break;

            /* Exit if the total size of all the items is less than the size of the gap.
            We know that we can never find a perfect fit. */
            if (TotalItemsSize < ClusterEnd - ClusterStart) {
                jkGui->show_debug(
                    DebugLevel::DetailedGapFilling, nullptr,
                    L"No perfect fit found, the total size of all the items above the gap is less than the size of the gap.");

                return (nullptr);
            }

            /* Exit if the running time is more than 0.5 seconds. */
            _ftime64_s(&Time);

            if (Time.time * 1000 + Time.millitm > MaxTime) {
                jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No perfect fit found, out of time.");

                return (nullptr);
            }

            /* Rewind and try again. The item that we have found previously fits in the
            gap, but it does not combine with other items to perfectly fill the gap. */
            item = FirstItem;
            FirstItem = nullptr;
            GapSize = ClusterEnd - ClusterStart;
            TotalItemsSize = 0;

            continue;
        }

        /* Ignore all unsuitable items. */
        if (item->is_unmovable_ == true) continue;
        if (item->is_excluded_ == true) continue;

        if (Zone != 3) {
            FileZone = 1;

            if (item->is_hog_ == true) FileZone = 2;
            if (item->is_dir_ == true) FileZone = 0;
            if (Zone != FileZone) continue;
        }

        if (item->Clusters < ClusterEnd - ClusterStart) {
            TotalItemsSize = TotalItemsSize + item->Clusters;
        }

        if (item->Clusters > GapSize) continue;

        /* Exit if this item perfectly fills the gap, or if we have found a combination
        with a previous item that perfectly fills the gap. */
        if (item->Clusters == GapSize) {
            jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Perfect fit found.");

            if (FirstItem != nullptr) return (FirstItem);

            return (item);
        }

        /* We have found an item that fit's inside the gap, but does not perfectly fill
        the gap. We are now looking to fill a smaller gap. */
        GapSize = GapSize - item->Clusters;

        /* Remember the first item that fits inside the gap. */
        if (FirstItem == nullptr) FirstItem = item;
    }

    jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                     L"No perfect fit found, all items above the gap are bigger than the gap.");

    return (nullptr);
}

/* Update some numbers in the DefragData. */
void DefragLib::call_show_status(struct DefragDataStruct* Data, int Phase, int Zone) {
    struct ItemStruct* Item;

    STARTING_LCN_INPUT_BUFFER BitmapParam;

    struct {
        uint64_t StartingLcn;
        uint64_t BitmapSize;

        BYTE Buffer[65536]; /* Most efficient if binary multiple. */
    } BitmapData;

    uint64_t Lcn;
    uint64_t ClusterStart;

    int Index;
    int IndexMax;

    BYTE Mask;

    int InUse;
    int PrevInUse;

    uint32_t ErrorCode;

    int64_t Count;
    int64_t Factor;
    int64_t Sum;

    DWORD w;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Count the number of free gaps on the disk. */
    Data->count_gaps_ = 0;
    Data->count_free_clusters_ = 0;
    Data->biggest_gap_ = 0;
    Data->count_gaps_less16_ = 0;
    Data->count_clusters_less16_ = 0;

    Lcn = 0;
    ClusterStart = 0;
    PrevInUse = 1;

    do {
        /* Fetch a block of cluster data. */
        BitmapParam.StartingLcn.QuadPart = Lcn;
        ErrorCode = DeviceIoControl(Data->disk_.volume_handle_,FSCTL_GET_VOLUME_BITMAP,
                                    &BitmapParam, sizeof(BitmapParam), &BitmapData, sizeof(BitmapData), &w, nullptr);

        if (ErrorCode != 0) {
            ErrorCode = NO_ERROR;
        }
        else {
            ErrorCode = GetLastError();
        }

        if ((ErrorCode != NO_ERROR) && (ErrorCode != ERROR_MORE_DATA)) break;

        Lcn = BitmapData.StartingLcn;
        Index = 0;
        Mask = 1;

        IndexMax = sizeof(BitmapData.Buffer);

        if (BitmapData.BitmapSize / 8 < IndexMax) IndexMax = (int)(BitmapData.BitmapSize / 8);

        while (Index < IndexMax) {
            InUse = (BitmapData.Buffer[Index] & Mask);

            if (((Lcn >= Data->mft_excludes_[0].Start) && (Lcn < Data->mft_excludes_[0].End)) ||
                ((Lcn >= Data->mft_excludes_[1].Start) && (Lcn < Data->mft_excludes_[1].End)) ||
                ((Lcn >= Data->mft_excludes_[2].Start) && (Lcn < Data->mft_excludes_[2].End))) {
                InUse = 1;
            }

            if ((PrevInUse == 0) && (InUse != 0)) {
                Data->count_gaps_ = Data->count_gaps_ + 1;
                Data->count_free_clusters_ = Data->count_free_clusters_ + Lcn - ClusterStart;
                if (Data->biggest_gap_ < Lcn - ClusterStart) Data->biggest_gap_ = Lcn - ClusterStart;

                if (Lcn - ClusterStart < 16) {
                    Data->count_gaps_less16_ = Data->count_gaps_less16_ + 1;
                    Data->count_clusters_less16_ = Data->count_clusters_less16_ + Lcn - ClusterStart;
                }
            }

            if ((PrevInUse != 0) && (InUse == 0)) ClusterStart = Lcn;

            PrevInUse = InUse;

            if (Mask == 128) {
                Mask = 1;
                Index = Index + 1;
            }
            else {
                Mask = Mask << 1;
            }

            Lcn = Lcn + 1;
        }
    }
    while ((ErrorCode == ERROR_MORE_DATA) && (Lcn < BitmapData.StartingLcn + BitmapData.BitmapSize));

    if (PrevInUse == 0) {
        Data->count_gaps_ = Data->count_gaps_ + 1;
        Data->count_free_clusters_ = Data->count_free_clusters_ + Lcn - ClusterStart;

        if (Data->biggest_gap_ < Lcn - ClusterStart) Data->biggest_gap_ = Lcn - ClusterStart;

        if (Lcn - ClusterStart < 16) {
            Data->count_gaps_less16_ = Data->count_gaps_less16_ + 1;
            Data->count_clusters_less16_ = Data->count_clusters_less16_ + Lcn - ClusterStart;
        }
    }

    /* Walk through all files and update the counters. */
    Data->count_directories_ = 0;
    Data->count_all_files_ = 0;
    Data->count_fragmented_items_ = 0;
    Data->count_all_bytes_ = 0;
    Data->count_fragmented_bytes_ = 0;
    Data->count_all_clusters_ = 0;
    Data->count_fragmented_clusters_ = 0;

    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
        if ((Item->LongFilename != nullptr) &&
            ((_wcsicmp(Item->LongFilename, L"$BadClus") == 0) ||
                (_wcsicmp(Item->LongFilename, L"$BadClus:$Bad:$DATA") == 0))) {
            continue;
        }

        Data->count_all_bytes_ = Data->count_all_bytes_ + Item->Bytes;
        Data->count_all_clusters_ = Data->count_all_clusters_ + Item->Clusters;

        if (Item->is_dir_ == true) {
            Data->count_directories_ = Data->count_directories_ + 1;
        }
        else {
            Data->count_all_files_ = Data->count_all_files_ + 1;
        }

        if (FragmentCount(Item) > 1) {
            Data->count_fragmented_items_ = Data->count_fragmented_items_ + 1;
            Data->count_fragmented_bytes_ = Data->count_fragmented_bytes_ + Item->Bytes;
            Data->count_fragmented_clusters_ = Data->count_fragmented_clusters_ + Item->Clusters;
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
    Count = 0;

    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
        if ((Item->LongFilename != nullptr) &&
            ((_wcsicmp(Item->LongFilename, L"$BadClus") == 0) ||
                (_wcsicmp(Item->LongFilename, L"$BadClus:$Bad:$DATA") == 0))) {
            continue;
        }

        if (Item->Clusters == 0) continue;

        Count = Count + 1;
    }

    if (Count > 1) {
        Factor = 1 - Count;
        Sum = 0;

        for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
            if ((Item->LongFilename != nullptr) &&
                ((_wcsicmp(Item->LongFilename, L"$BadClus") == 0) ||
                    (_wcsicmp(Item->LongFilename, L"$BadClus:$Bad:$DATA") == 0))) {
                continue;
            }

            if (Item->Clusters == 0) continue;

            Sum = Sum + Factor * (get_item_lcn(Item) * 2 + Item->Clusters);

            Factor = Factor + 2;
        }

        Data->average_distance_ = Sum / (double)(Count * (Count - 1));
    }
    else {
        Data->average_distance_ = 0;
    }

    Data->phase_ = Phase;
    Data->zone_ = Zone;
    Data->PhaseDone = 0;
    Data->PhaseTodo = 0;

    jkGui->show_status(Data);
}

/* For debugging only: compare the data with the output from the
FSCTL_GET_RETRIEVAL_POINTERS function call.
Note: Reparse points will usually be flagged as different. A reparse point is
a symbolic link. The CreateFile call will resolve the symbolic link and retrieve
the info from the real item, but the MFT contains the info from the symbolic
link. */
void DefragLib::CompareItems(struct DefragDataStruct* Data, struct ItemStruct* Item) {
    HANDLE FileHandle;

    uint64_t Clusters; /* Total number of clusters. */

    STARTING_VCN_INPUT_BUFFER RetrieveParam;

    struct {
        uint32_t ExtentCount;

        uint64_t StartingVcn;

        struct {
            uint64_t NextVcn;
            uint64_t Lcn;
        } Extents[1000];
    } ExtentData;

    BY_HANDLE_FILE_INFORMATION FileInformation;

    uint64_t Vcn;

    struct FragmentListStruct* Fragment;
    struct FragmentListStruct* LastFragment;

    uint32_t ErrorCode;

    WCHAR ErrorString[BUFSIZ];

    int MaxLoop;

    ULARGE_INTEGER u;

    uint32_t i;
    DWORD w;

    DefragGui* jkGui = DefragGui::get_instance();

    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"%I64u %s", get_item_lcn(Item), Item->LongFilename);

    if (Item->is_dir_ == false) {
        FileHandle = CreateFileW(Item->LongPath,FILE_READ_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr,OPEN_EXISTING,FILE_FLAG_NO_BUFFERING, nullptr);
    }
    else {
        FileHandle = CreateFileW(Item->LongPath,GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    }

    if (FileHandle == INVALID_HANDLE_VALUE) {
        system_error_str(GetLastError(), ErrorString,BUFSIZ);

        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Could not open: %s", ErrorString);

        return;
    }

    /* Fetch the date/times of the file. */
    if (GetFileInformationByHandle(FileHandle, &FileInformation) != 0) {
        u.LowPart = FileInformation.ftCreationTime.dwLowDateTime;
        u.HighPart = FileInformation.ftCreationTime.dwHighDateTime;

        if (Item->CreationTime != u.QuadPart) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different CreationTime %I64u <> %I64u = %I64u",
                             Item->CreationTime, u.QuadPart, Item->CreationTime - u.QuadPart);
        }

        u.LowPart = FileInformation.ftLastAccessTime.dwLowDateTime;
        u.HighPart = FileInformation.ftLastAccessTime.dwHighDateTime;

        if (Item->LastAccessTime != u.QuadPart) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different LastAccessTime %I64u <> %I64u = %I64u",
                             Item->LastAccessTime, u.QuadPart, Item->LastAccessTime - u.QuadPart);
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
    Fragment = Item->Fragments;
    Clusters = 0;
    Vcn = 0;
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
        RetrieveParam.StartingVcn.QuadPart = Vcn;

        ErrorCode = DeviceIoControl(FileHandle,FSCTL_GET_RETRIEVAL_POINTERS,
                                    &RetrieveParam, sizeof(RetrieveParam), &ExtentData, sizeof(ExtentData), &w,
                                    nullptr);

        if (ErrorCode != 0) {
            ErrorCode = NO_ERROR;
        }
        else {
            ErrorCode = GetLastError();
        }

        if ((ErrorCode != NO_ERROR) && (ErrorCode != ERROR_MORE_DATA)) break;

        /* Walk through the clustermap, count the total number of clusters, and
        save all fragments in memory. */
        for (i = 0; i < ExtentData.ExtentCount; i++) {
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
            if (ExtentData.Extents[i].Lcn != VIRTUALFRAGMENT) {
                Clusters = Clusters + ExtentData.Extents[i].NextVcn - Vcn;
            }

            /* Compare the fragment. */
            if (Fragment == nullptr) {
                jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Extra fragment in FSCTL_GET_RETRIEVAL_POINTERS");
            }
            else {
                if (Fragment->lcn_ != ExtentData.Extents[i].Lcn) {
                    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different LCN in fragment: %I64u <> %I64u",
                                     Fragment->lcn_, ExtentData.Extents[i].Lcn);
                }

                if (Fragment->next_vcn_ != ExtentData.Extents[i].NextVcn) {
                    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different NextVcn in fragment: %I64u <> %I64u",
                                     Fragment->next_vcn_, ExtentData.Extents[i].NextVcn);
                }

                Fragment = Fragment->next_;
            }

            /* The Vcn of the next fragment is the NextVcn field in this record. */
            Vcn = ExtentData.Extents[i].NextVcn;
        }

        /* Loop until we have processed the entire clustermap of the file. */
    }
    while (ErrorCode == ERROR_MORE_DATA);

    /* If there was an error while reading the clustermap then return false. */
    if ((ErrorCode != NO_ERROR) && (ErrorCode != ERROR_HANDLE_EOF)) {
        system_error_str(ErrorCode, ErrorString,BUFSIZ);

        jkGui->show_debug(DebugLevel::Fatal, Item, L"  Error while processing clustermap: %s", ErrorString);

        return;
    }

    if (Fragment != nullptr) {
        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Extra fragment from MFT");
    }

    if (Item->Clusters != Clusters) {
        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different cluster count: %I64u <> %I64u",
                         Item->Clusters, Clusters);
    }
}

/* Scan all files in a directory and all it's subdirectories (recursive)
and store the information in a tree in memory for later use by the
optimizer. */
void DefragLib::ScanDir(struct DefragDataStruct* Data, WCHAR* Mask, struct ItemStruct* ParentDirectory) {
    struct ItemStruct* Item;

    struct FragmentListStruct* Fragment;

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
    jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, Data->DebugMsg[23], Mask);

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
            if (Item->ShortPath != nullptr) free(Item->ShortPath);
            if (Item->ShortFilename != nullptr) free(Item->ShortFilename);
            if (Item->LongPath != nullptr) free(Item->LongPath);
            if (Item->LongFilename != nullptr) free(Item->LongFilename);

            while (Item->Fragments != nullptr) {
                Fragment = Item->Fragments->next_;

                free(Item->Fragments);

                Item->Fragments = Fragment;
            }

            free(Item);

            Item = nullptr;
        }

        /* Create new item. */
        Item = (struct ItemStruct*)malloc(sizeof(struct ItemStruct));

        if (Item == nullptr) break;

        Item->ShortPath = nullptr;
        Item->ShortFilename = nullptr;
        Item->LongPath = nullptr;
        Item->LongFilename = nullptr;
        Item->Fragments = nullptr;

        Length = wcslen(RootPath) + wcslen(FindFileData.cFileName) + 2;

        Item->LongPath = (WCHAR*)malloc(sizeof(WCHAR) * Length);

        if (Item->LongPath == nullptr) break;

        swprintf_s(Item->LongPath, Length, L"%s\\%s", RootPath, FindFileData.cFileName);

        Item->LongFilename = _wcsdup(FindFileData.cFileName);

        if (Item->LongFilename == nullptr) break;

        Length = wcslen(RootPath) + wcslen(FindFileData.cAlternateFileName) + 2;

        Item->ShortPath = (WCHAR*)malloc(sizeof(WCHAR) * Length);

        if (Item->ShortPath == nullptr) break;

        swprintf_s(Item->ShortPath, Length, L"%s\\%s", RootPath, FindFileData.cAlternateFileName);

        Item->ShortFilename = _wcsdup(FindFileData.cAlternateFileName);

        if (Item->ShortFilename == nullptr) break;

        Item->Bytes = FindFileData.nFileSizeHigh * ((uint64_t)MAXDWORD + 1) +
                FindFileData.nFileSizeLow;

        Item->Clusters = 0;
        Item->CreationTime = 0;
        Item->LastAccessTime = 0;
        Item->MftChangeTime = 0;
        Item->ParentDirectory = ParentDirectory;
        Item->is_dir_ = false;

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            Item->is_dir_ = true;
        }
        Item->is_unmovable_ = false;
        Item->is_excluded_ = false;
        Item->is_hog_ = false;

        /* Analyze the item: Clusters and Fragments, and the CreationTime, LastAccessTime,
        and MftChangeTime. If the item could not be opened then ignore the item. */
        FileHandle = OpenItemHandle(Data, Item);

        if (FileHandle == nullptr) continue;

        Result = GetFragments(Data, Item, FileHandle);

        CloseHandle(FileHandle);

        if (Result == false) continue;

        /* Increment counters. */
        Data->count_all_files_ = Data->count_all_files_ + 1;
        Data->count_all_bytes_ = Data->count_all_bytes_ + Item->Bytes;
        Data->count_all_clusters_ = Data->count_all_clusters_ + Item->Clusters;

        if (IsFragmented(Item, 0, Item->Clusters) == true) {
            Data->count_fragmented_items_ = Data->count_fragmented_items_ + 1;
            Data->count_fragmented_bytes_ = Data->count_fragmented_bytes_ + Item->Bytes;
            Data->count_fragmented_clusters_ = Data->count_fragmented_clusters_ + Item->Clusters;
        }

        Data->PhaseDone = Data->PhaseDone + Item->Clusters;

        /* Show progress message. */
        jkGui->show_analyze(Data, Item);

        /* If it's a directory then iterate subdirectories. */
        if (Item->is_dir_ == true) {
            Data->count_directories_ = Data->count_directories_ + 1;

            Length = wcslen(RootPath) + wcslen(FindFileData.cFileName) + 4;

            TempPath = (WCHAR*)malloc(sizeof(WCHAR) * Length);

            if (TempPath != nullptr) {
                swprintf_s(TempPath, Length, L"%s\\%s\\*", RootPath, FindFileData.cFileName);
                ScanDir(Data, TempPath, Item);
                free(TempPath);
            }
        }

        /* Ignore the item if it has no clusters or no LCN. Very small
        files are stored in the MFT and are reported by Windows as
        having zero clusters and no fragments. */
        if ((Item->Clusters == 0) || (Item->Fragments == nullptr)) continue;

        /* Draw the item on the screen. */
        //		if (*Data->RedrawScreen == 0) {
        ColorizeItem(Data, Item, 0, 0, false);
        //		} else {
        //			m_jkGui->ShowDiskmap(Data);
        //		}

        /* Show debug info about the file. */
        /* Show debug message: "%I64d clusters at %I64d, %I64d bytes" */
        jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->DebugMsg[16], Item->Clusters, get_item_lcn(Item), Item->Bytes);

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0) {
            /* Show debug message: "Special file attribute: Compressed" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->DebugMsg[17]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0) {
            /* Show debug message: "Special file attribute: Encrypted" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->DebugMsg[18]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) != 0) {
            /* Show debug message: "Special file attribute: Offline" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->DebugMsg[19]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
            /* Show debug message: "Special file attribute: Read-only" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->DebugMsg[20]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0) {
            /* Show debug message: "Special file attribute: Sparse-file" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->DebugMsg[21]);
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0) {
            /* Show debug message: "Special file attribute: Temporary" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->DebugMsg[22]);
        }

        /* Save some memory if short and long filename are the same. */
        if ((Item->LongFilename != nullptr) &&
            (Item->ShortFilename != nullptr) &&
            (_wcsicmp(Item->LongFilename, Item->ShortFilename) == 0)) {
            free(Item->ShortFilename);
            Item->ShortFilename = Item->LongFilename;
        }

        if ((Item->LongFilename == nullptr) && (Item->ShortFilename != nullptr))
            Item->LongFilename = Item->
                    ShortFilename;
        if ((Item->LongFilename != nullptr) && (Item->ShortFilename == nullptr))
            Item->ShortFilename = Item->
                    LongFilename;

        if ((Item->LongPath != nullptr) &&
            (Item->ShortPath != nullptr) &&
            (_wcsicmp(Item->LongPath, Item->ShortPath) == 0)) {
            free(Item->ShortPath);
            Item->ShortPath = Item->LongPath;
        }

        if ((Item->LongPath == nullptr) && (Item->ShortPath != nullptr)) Item->LongPath = Item->ShortPath;
        if ((Item->LongPath != nullptr) && (Item->ShortPath == nullptr)) Item->ShortPath = Item->LongPath;

        /* Add the item to the ItemTree in memory. */
        TreeInsert(Data, Item);
        Item = nullptr;
    }
    while (FindNextFileW(FindHandle, &FindFileData) != 0);

    FindClose(FindHandle);

    /* Cleanup. */
    free(RootPath);

    if (Item != nullptr) {
        if (Item->ShortPath != nullptr) free(Item->ShortPath);
        if (Item->ShortFilename != nullptr) free(Item->ShortFilename);
        if (Item->LongPath != nullptr) free(Item->LongPath);
        if (Item->LongFilename != nullptr) free(Item->LongFilename);

        while (Item->Fragments != nullptr) {
            Fragment = Item->Fragments->next_;

            free(Item->Fragments);

            Item->Fragments = Fragment;
        }

        free(Item);
    }
}

/* Scan all files in a volume and store the information in a tree in
memory for later use by the optimizer. */
void DefragLib::AnalyzeVolume(struct DefragDataStruct* Data) {
    struct ItemStruct* Item;

    BOOL Result;

    uint64_t SystemTime;

    SYSTEMTIME Time1;

    FILETIME Time2;

    ULARGE_INTEGER Time3;

    int i;

    DefragGui* jkGui = DefragGui::get_instance();
    JKScanFat* jkScanFat = JKScanFat::getInstance();
    ScanNtfs* jkScanNtfs = ScanNtfs::getInstance();

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
    Result = jkScanNtfs->AnalyzeNtfsVolume(Data);

    /* Scan FAT disks. */
    if ((Result == FALSE) && (*Data->running_ == RunningState::RUNNING)) Result = jkScanFat->AnalyzeFatVolume(Data);

    /* Scan all other filesystems. */
    if ((Result == FALSE) && (*Data->running_ == RunningState::RUNNING)) {
        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"This is not a FAT or NTFS disk, using the slow scanner.");

        /* Setup the width of the progress bar. */
        Data->PhaseTodo = Data->total_clusters_ - Data->count_free_clusters_;

        for (i = 0; i < 3; i++) {
            Data->PhaseTodo = Data->PhaseTodo - (Data->mft_excludes_[i].End - Data->mft_excludes_[i].Start);
        }

        /* Scan all the files. */
        ScanDir(Data, Data->include_mask_, nullptr);
    }

    /* Update the diskmap with the colors. */
    Data->PhaseDone = Data->PhaseTodo;
    jkGui->draw_cluster(Data, 0, 0, 0);

    /* Setup the progress counter and the file/dir counters. */
    Data->PhaseDone = 0;
    Data->PhaseTodo = 0;

    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
        Data->PhaseTodo = Data->PhaseTodo + 1;
    }

    jkGui->show_analyze(nullptr, nullptr);

    /* Walk through all the items one by one. */
    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
        if (*Data->running_ != RunningState::RUNNING) break;

        /* If requested then redraw the diskmap. */
        //		if (*Data->RedrawScreen == 1) m_jkGui->ShowDiskmap(Data);

        /* Construct the full path's of the item. The MFT contains only the filename, plus
        a pointer to the directory. We have to construct the full paths's by joining
        all the names of the directories, and the name of the file. */
        if (Item->LongPath == nullptr) Item->LongPath = get_long_path(Data, Item);
        if (Item->ShortPath == nullptr) Item->ShortPath = get_short_path(Data, Item);

        /* Save some memory if the short and long paths are the same. */
        if ((Item->LongPath != nullptr) &&
            (Item->ShortPath != nullptr) &&
            (Item->LongPath != Item->ShortPath) &&
            (_wcsicmp(Item->LongPath, Item->ShortPath) == 0)) {
            free(Item->ShortPath);
            Item->ShortPath = Item->LongPath;
        }

        if ((Item->LongPath == nullptr) && (Item->ShortPath != nullptr)) Item->LongPath = Item->ShortPath;
        if ((Item->LongPath != nullptr) && (Item->ShortPath == nullptr)) Item->ShortPath = Item->LongPath;

        /* For debugging only: compare the data with the output from the
        FSCTL_GET_RETRIEVAL_POINTERS function call. */
        /*
        CompareItems(Data,Item);
        */

        /* Apply the Mask and set the Exclude flag of all items that do not match. */
        if ((match_mask(Item->LongPath, Data->include_mask_) == false) &&
            (match_mask(Item->ShortPath, Data->include_mask_) == false)) {
            Item->is_excluded_ = true;

            ColorizeItem(Data, Item, 0, 0, false);
        }

        /* Determine if the item is to be excluded by comparing it's name with the
        Exclude masks. */
        if ((Item->is_excluded_ == false) && (Data->excludes_ != nullptr)) {
            for (i = 0; Data->excludes_[i] != nullptr; i++) {
                if ((match_mask(Item->LongPath, Data->excludes_[i]) == true) ||
                    (match_mask(Item->ShortPath, Data->excludes_[i]) == true)) {
                    Item->is_excluded_ = true;

                    ColorizeItem(Data, Item, 0, 0, false);

                    break;
                }
            }
        }

        /* Exclude my own logfile. */
        if ((Item->is_excluded_ == false) &&
            (Item->LongFilename != nullptr) &&
            ((_wcsicmp(Item->LongFilename, L"jkdefrag.log") == 0) ||
                (_wcsicmp(Item->LongFilename, L"jkdefragcmd.log") == 0) ||
                (_wcsicmp(Item->LongFilename, L"jkdefragscreensaver.log") == 0))) {
            Item->is_excluded_ = true;

            ColorizeItem(Data, Item, 0, 0, false);
        }

        /* The item is a SpaceHog if it's larger than 50 megabytes, or last access time
        is more than 30 days ago, or if it's filename matches a SpaceHog mask. */
        if ((Item->is_excluded_ == false) && (Item->is_dir_ == false)) {
            if ((Data->use_default_space_hogs_ == true) && (Item->Bytes > 50 * 1024 * 1024)) {
                Item->is_hog_ = true;
            }
            else if ((Data->use_default_space_hogs_ == true) &&
                (Data->use_last_access_time_ == TRUE) &&
                (Item->LastAccessTime + (uint64_t)(30 * 24 * 60 * 60) * 10000000 < SystemTime)) {
                Item->is_hog_ = true;
            }
            else if (Data->space_hogs_ != nullptr) {
                for (i = 0; Data->space_hogs_[i] != nullptr; i++) {
                    if ((match_mask(Item->LongPath, Data->space_hogs_[i]) == true) ||
                        (match_mask(Item->ShortPath, Data->space_hogs_[i]) == true)) {
                        Item->is_hog_ = true;

                        break;
                    }
                }
            }

            if (Item->is_hog_ == true) ColorizeItem(Data, Item, 0, 0, false);
        }

        /* Special exception for "http://www.safeboot.com/". */
        if (match_mask(Item->LongPath, L"*\\safeboot.fs") == true) Item->is_unmovable_ = true;

        /* Special exception for Acronis OS Selector. */
        if (match_mask(Item->LongPath, L"?:\\bootwiz.sys") == true) Item->is_unmovable_ = true;
        if (match_mask(Item->LongPath, L"*\\BOOTWIZ\\*") == true) Item->is_unmovable_ = true;

        /* Special exception for DriveCrypt by "http://www.securstar.com/". */
        if (match_mask(Item->LongPath, L"?:\\BootAuth?.sys") == true) Item->is_unmovable_ = true;

        /* Special exception for Symantec GoBack. */
        if (match_mask(Item->LongPath, L"*\\Gobackio.bin") == true) Item->is_unmovable_ = true;

        /* The $BadClus file maps the entire disk and is always unmovable. */
        if ((Item->LongFilename != nullptr) &&
            ((_wcsicmp(Item->LongFilename, L"$BadClus") == 0) ||
                (_wcsicmp(Item->LongFilename, L"$BadClus:$Bad:$DATA") == 0))) {
            Item->is_unmovable_ = true;
        }

        /* Update the progress percentage. */
        Data->PhaseDone = Data->PhaseDone + 1;

        if (Data->PhaseDone % 10000 == 0) jkGui->draw_cluster(Data, 0, 0, 0);
    }

    /* Force the percentage to 100%. */
    Data->PhaseDone = Data->PhaseTodo;
    jkGui->draw_cluster(Data, 0, 0, 0);

    /* Calculate the begin of the zone's. */
    CalculateZones(Data);

    /* Call the ShowAnalyze() callback one last time. */
    jkGui->show_analyze(Data, nullptr);
}

/* Move items to their zone. This will:
- Defragment all fragmented files
- Move regular files out of the directory zone.
- Move SpaceHogs out of the directory- and regular zones.
- Move items out of the MFT reserved zones
*/
void DefragLib::fixup(struct DefragDataStruct* data) {
    struct ItemStruct* item;

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
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = TreeNext(item)) {
        if (item->is_unmovable_ == true) continue;
        if (item->is_excluded_ == true) continue;
        if (item->Clusters == 0) continue;

        data->PhaseTodo = data->PhaseTodo + item->Clusters;
    }

    [[maybe_unused]] uint64_t last_calc_time = system_time;

    /* Exit if nothing to do. */
    if (data->PhaseTodo == 0) return;

    /* Walk through all files and move the files that need to be moved. */
    for (file_zone = 0; file_zone < 3; file_zone++) {
        gap_begin[file_zone] = 0;
        gap_end[file_zone] = 0;
    }

    auto next_item = tree_smallest(data->item_tree_);

    while ((next_item != nullptr) && (*data->running_ == RunningState::RUNNING)) {
        /* The loop will change the position of the item in the tree, so we have
        to determine the next item before executing the loop. */
        item = next_item;

        next_item = TreeNext(item);

        /* Ignore items that are unmovable or excluded. */
        if (item->is_unmovable_ == true) continue;
        if (item->is_excluded_ == true) continue;
        if (item->Clusters == 0) continue;

        /* Ignore items that do not need to be moved. */
        file_zone = 1;

        if (item->is_hog_ == true) file_zone = 2;
        if (item->is_dir_ == true) file_zone = 0;

        const uint64_t item_lcn = get_item_lcn(item);

        int move_me = false;

        if (IsFragmented(item, 0, item->Clusters) == true) {
            /* "I am fragmented." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->DebugMsg[53]);

            move_me = true;
        }

        if ((move_me == false) &&
            (((item_lcn >= data->mft_excludes_[0].Start) && (item_lcn < data->mft_excludes_[0].End)) ||
                ((item_lcn >= data->mft_excludes_[1].Start) && (item_lcn < data->mft_excludes_[1].End)) ||
                ((item_lcn >= data->mft_excludes_[2].Start) && (item_lcn < data->mft_excludes_[2].End))) &&
            ((data->disk_.type_ != DiskType::NTFS) || (match_mask(item->LongPath, L"?:\\$MFT") != true))) {
            /* "I am in MFT reserved space." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->DebugMsg[54]);

            move_me = true;
        }

        if ((file_zone == 1) && (item_lcn < data->zones_[1]) && (move_me == false)) {
            /* "I am a regular file in zone 1." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->DebugMsg[55]);

            move_me = true;
        }

        if ((file_zone == 2) && (item_lcn < data->zones_[2]) && (move_me == false)) {
            /* "I am a spacehog in zone 1 or 2." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->DebugMsg[56]);

            move_me = true;
        }

        if (move_me == false) {
            data->PhaseDone = data->PhaseDone + item->Clusters;

            continue;
        }

        /* Ignore files that have been modified less than 15 minutes ago. */
        if (item->is_dir_ == false) {
            result = GetFileAttributesExW(item->LongPath, GetFileExInfoStandard, &attributes);

            if (result != 0) {
                u.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
                u.HighPart = attributes.ftLastWriteTime.dwHighDateTime;

                if (const uint64_t FileTime = u.QuadPart; FileTime + 15 * 60 * (uint64_t)10000000 > system_time) {
                    data->PhaseDone = data->PhaseDone + item->Clusters;

                    continue;
                }
            }
        }

        /* If the file does not fit in the current gap then find another gap. */
        if (item->Clusters > gap_end[file_zone] - gap_begin[file_zone]) {
            result = find_gap(data, data->zones_[file_zone], 0, item->Clusters, true, false, &gap_begin[file_zone],
                              &gap_end[file_zone], FALSE);

            if (result == false) {
                /* Show debug message: "Cannot move item away because no gap is big enough: %I64d[%lu]" */
                gui->show_debug(DebugLevel::Progress, item, data->DebugMsg[25], get_item_lcn(item), item->Clusters);

                gap_end[file_zone] = gap_begin[file_zone]; /* Force re-scan of gap. */

                data->PhaseDone = data->PhaseDone + item->Clusters;

                continue;
            }
        }

        /* Move the item. */
        result = MoveItem(data, item, gap_begin[file_zone], 0, item->Clusters, 0);

        if (result == true) {
            gap_begin[file_zone] = gap_begin[file_zone] + item->Clusters;
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
void DefragLib::Defragment(struct DefragDataStruct* Data) {
    struct ItemStruct* Item;
    struct ItemStruct* NextItem;

    uint64_t GapBegin;
    uint64_t GapEnd;
    uint64_t ClustersDone;
    uint64_t Clusters;

    struct FragmentListStruct* Fragment;

    uint64_t Vcn;
    uint64_t RealVcn;

    HANDLE FileHandle;

    int FileZone;
    int Result;

    DefragGui* jkGui = DefragGui::get_instance();

    call_show_status(Data, 2, -1); /* "Phase 2: Defragment" */

    /* Setup the width of the progress bar: the number of clusters in all
    fragmented files. */
    for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
        if (Item->is_unmovable_ == true) continue;
        if (Item->is_excluded_ == true) continue;
        if (Item->Clusters == 0) continue;

        if (IsFragmented(Item, 0, Item->Clusters) == false) continue;

        Data->PhaseTodo = Data->PhaseTodo + Item->Clusters;
    }

    /* Exit if nothing to do. */
    if (Data->PhaseTodo == 0) return;

    /* Walk through all files and defrag. */
    NextItem = tree_smallest(Data->item_tree_);

    while ((NextItem != nullptr) && (*Data->running_ == RunningState::RUNNING)) {
        /* The loop may change the position of the item in the tree, so we have
        to determine and remember the next item now. */
        Item = NextItem;

        NextItem = TreeNext(Item);

        /* Ignore if the Item cannot be moved, or is Excluded, or is not fragmented. */
        if (Item->is_unmovable_ == true) continue;
        if (Item->is_excluded_ == true) continue;
        if (Item->Clusters == 0) continue;

        if (IsFragmented(Item, 0, Item->Clusters) == false) continue;

        /* Find a gap that is large enough to hold the item, or the largest gap
        on the volume. If the disk is full then show a message and exit. */
        FileZone = 1;

        if (Item->is_hog_ == true) FileZone = 2;
        if (Item->is_dir_ == true) FileZone = 0;

        Result = find_gap(Data, Data->zones_[FileZone], 0, Item->Clusters, false, false, &GapBegin, &GapEnd, FALSE);

        if (Result == false) {
            /* Try finding a gap again, this time including the free area. */
            Result = find_gap(Data, 0, 0, Item->Clusters, false, false, &GapBegin, &GapEnd, FALSE);

            if (Result == false) {
                /* Show debug message: "Disk is full, cannot defragment." */
                jkGui->show_debug(DebugLevel::Progress, Item, Data->DebugMsg[44]);

                return;
            }
        }

        /* If the gap is big enough to hold the entire item then move the file
        in a single go, and loop. */
        if (GapEnd - GapBegin >= Item->Clusters) {
            MoveItem(Data, Item, GapBegin, 0, Item->Clusters, 0);

            continue;
        }

        /* Open a filehandle for the item. If error then set the Unmovable flag,
        colorize the item on the screen, and loop. */
        FileHandle = OpenItemHandle(Data, Item);

        if (FileHandle == nullptr) {
            Item->is_unmovable_ = true;

            ColorizeItem(Data, Item, 0, 0, false);

            continue;
        }

        /* Move the file in parts, each time selecting the biggest gap
        available. */
        ClustersDone = 0;

        do {
            Clusters = GapEnd - GapBegin;

            if (Clusters > Item->Clusters - ClustersDone) {
                Clusters = Item->Clusters - ClustersDone;
            }

            /* Make sure that the gap is bigger than the first fragment of the
            block that we're about to move. If not then the result would be
            more fragments, not less. */
            Vcn = 0;
            RealVcn = 0;

            for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (RealVcn >= ClustersDone) {
                        if (Clusters > Fragment->next_vcn_ - Vcn) break;

                        ClustersDone = RealVcn + Fragment->next_vcn_ - Vcn;

                        Data->PhaseDone = Data->PhaseDone + Fragment->next_vcn_ - Vcn;
                    }

                    RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
                }

                Vcn = Fragment->next_vcn_;
            }

            if (ClustersDone >= Item->Clusters) break;

            /* Move the segment. */
            Result = MoveItem4(Data, Item, FileHandle, GapBegin, ClustersDone, Clusters, 0);

            /* Next segment. */
            ClustersDone = ClustersDone + Clusters;

            /* Find a gap large enough to hold the remainder, or the largest gap
            on the volume. */
            if (ClustersDone < Item->Clusters) {
                Result = find_gap(Data, Data->zones_[FileZone], 0, Item->Clusters - ClustersDone,
                                  false, false, &GapBegin, &GapEnd, FALSE);

                if (Result == false) break;
            }
        }
        while ((ClustersDone < Item->Clusters) && (*Data->running_ == RunningState::RUNNING));

        /* Close the item. */
        FlushFileBuffers(FileHandle); /* Is this useful? Can't hurt. */
        CloseHandle(FileHandle);
    }
}

/* Fill all the gaps at the beginning of the disk with fragments from the files above. */
void DefragLib::ForcedFill(struct DefragDataStruct* Data) {
    uint64_t GapBegin;
    uint64_t GapEnd;

    struct ItemStruct* Item;
    struct FragmentListStruct* Fragment;
    struct ItemStruct* HighestItem;

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

        for (Item = TreeBiggest(Data->item_tree_); Item != nullptr; Item = TreePrev(Item)) {
            if (Item->is_unmovable_ == true) continue;
            if (Item->is_excluded_ == true) continue;
            if (Item->Clusters == 0) continue;

            Vcn = 0;
            RealVcn = 0;

            for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if ((Fragment->lcn_ > HighestLcn) && (Fragment->lcn_ < MaxLcn)) {
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

        Result = MoveItem(Data, HighestItem, GapBegin, HighestVcn + HighestSize - Clusters, Clusters, 0);

        GapBegin = GapBegin + Clusters;
        MaxLcn = HighestLcn + HighestSize - Clusters;
    }
}

/* Vacate an area by moving files upward. If there are unmovable files at the Lcn then
skip them. Then move files upward until the gap is bigger than Clusters, or when we
encounter an unmovable file. */
void DefragLib::Vacate(struct DefragDataStruct* Data, uint64_t Lcn, uint64_t Clusters, BOOL IgnoreMftExcludes) {
    uint64_t TestGapBegin;
    uint64_t TestGapEnd;
    uint64_t MoveGapBegin;
    uint64_t MoveGapEnd;

    struct ItemStruct* Item;
    struct FragmentListStruct* Fragment;

    uint64_t Vcn;
    uint64_t RealVcn;

    struct ItemStruct* BiggerItem;

    uint64_t BiggerBegin;
    uint64_t BiggerEnd;
    uint64_t BiggerRealVcn;
    uint64_t MoveTo;
    uint64_t DoneUntil;

    DefragGui* jkGui = DefragGui::get_instance();

    jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Vacating %I64u clusters starting at LCN=%I64u", Clusters, Lcn);

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

        for (Item = tree_smallest(Data->item_tree_); Item != nullptr; Item = TreeNext(Item)) {
            if ((Item->is_unmovable_ == true) || (Item->is_excluded_ == true) || (Item->Clusters == 0)) {
                continue;
            }

            Vcn = 0;
            RealVcn = 0;

            for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if ((Fragment->lcn_ >= DoneUntil) &&
                        ((BiggerBegin > Fragment->lcn_) || (BiggerItem == nullptr))) {
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

            if ((BiggerBegin != 0) && (BiggerBegin == Lcn)) break;
        }

        if (BiggerItem == nullptr) {
            jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No data found above LCN=%I64u", Lcn);

            return;
        }

        jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Data found at LCN=%I64u, %s", BiggerBegin, BiggerItem->LongPath);

        /* Find the first gap above the Lcn. */
        bool result = find_gap(Data, Lcn, 0, 0, true, false, &TestGapBegin, &TestGapEnd, IgnoreMftExcludes);

        if (result == false) {
            jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No gaps found above LCN=%I64u", Lcn);

            return;
        }

        /* Exit if the end of the first gap is below the first movable item, the gap cannot
        be enlarged. */
        if (TestGapEnd < BiggerBegin) {
            jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Cannot enlarge the gap from %I64u to %I64u (%I64u clusters) any further.",
                             TestGapBegin, TestGapEnd, TestGapEnd - TestGapBegin);

            return;
        }

        /* Exit if the first movable item is at the end of the gap and the gap is big enough,
        no need to enlarge any further. */
        if ((TestGapEnd == BiggerBegin) && (TestGapEnd - TestGapBegin >= Clusters)) {
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
            if ((MoveTo < Data->total_clusters_) && (MoveTo >= BiggerEnd)) {
                jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Finding gap above MoveTo=%I64u", MoveTo);

                result = find_gap(Data, MoveTo, 0, BiggerEnd - BiggerBegin, true, false, &MoveGapBegin, &MoveGapEnd,
                                  FALSE);
            }

            /* If no gap was found then try to find a gap as high on disk as possible, but
            above the item. */
            if (result == false) {
                jkGui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Finding gap from end of disk above BiggerEnd=%I64u", BiggerEnd);

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
        result = MoveItem(Data, BiggerItem, MoveGapBegin, BiggerRealVcn, BiggerEnd - BiggerBegin, 0);

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
int DefragLib::CompareItems(struct ItemStruct* Item1, struct ItemStruct* Item2, int SortField) {
    int Result;

    /* If one of the items is nullptr then the other item is bigger. */
    if (Item1 == nullptr) return (-1);
    if (Item2 == nullptr) return (1);

    /* Return zero if the items are exactly the same. */
    if (Item1 == Item2) return (0);

    /* Compare the SortField of the items and return 1 or -1 if they are not equal. */
    if (SortField == 0) {
        if ((Item1->LongPath == nullptr) && (Item2->LongPath == nullptr)) return (0);
        if (Item1->LongPath == nullptr) return (-1);
        if (Item2->LongPath == nullptr) return (1);

        Result = _wcsicmp(Item1->LongPath, Item2->LongPath);

        if (Result != 0) return (Result);
    }

    if (SortField == 1) {
        if (Item1->Bytes < Item2->Bytes) return (-1);
        if (Item1->Bytes > Item2->Bytes) return (1);
    }

    if (SortField == 2) {
        if (Item1->LastAccessTime > Item2->LastAccessTime) return (-1);
        if (Item1->LastAccessTime < Item2->LastAccessTime) return (1);
    }

    if (SortField == 3) {
        if (Item1->MftChangeTime < Item2->MftChangeTime) return (-1);
        if (Item1->MftChangeTime > Item2->MftChangeTime) return (1);
    }

    if (SortField == 4) {
        if (Item1->CreationTime < Item2->CreationTime) return (-1);
        if (Item1->CreationTime > Item2->CreationTime) return (1);
    }

    /* The SortField of the items is equal, so we must compare all the other fields
    to see if they are really equal. */
    if ((Item1->LongPath != nullptr) && (Item2->LongPath != nullptr)) {
        if (Item1->LongPath == nullptr) return (-1);
        if (Item2->LongPath == nullptr) return (1);

        Result = _wcsicmp(Item1->LongPath, Item2->LongPath);

        if (Result != 0) return (Result);
    }

    if (Item1->Bytes < Item2->Bytes) return (-1);
    if (Item1->Bytes > Item2->Bytes) return (1);
    if (Item1->LastAccessTime < Item2->LastAccessTime) return (-1);
    if (Item1->LastAccessTime > Item2->LastAccessTime) return (1);
    if (Item1->MftChangeTime < Item2->MftChangeTime) return (-1);
    if (Item1->MftChangeTime > Item2->MftChangeTime) return (1);
    if (Item1->CreationTime < Item2->CreationTime) return (-1);
    if (Item1->CreationTime > Item2->CreationTime) return (1);

    /* As a last resort compare the location on harddisk. */
    if (get_item_lcn(Item1) < get_item_lcn(Item2)) return (-1);
    if (get_item_lcn(Item1) > get_item_lcn(Item2)) return (1);

    return (0);
}

/* Optimize the volume by moving all the files into a sorted order.
SortField=0    Filename
SortField=1    Filesize
SortField=2    Date/Time LastAccess
SortField=3    Date/Time LastChange
SortField=4    Date/Time Creation
*/
void DefragLib::optimize_sort(struct DefragDataStruct* data, const int sort_field) {
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
        struct ItemStruct* previous_item = nullptr;

        uint64_t lcn = data->zones_[data->zone_];

        gap_begin = 0;
        gap_end = 0;

        while (*data->running_ == RunningState::RUNNING) {
            /* Find the next item that we want to place. */
            struct ItemStruct* item = nullptr;
            uint64_t phase_temp = 0;

            for (auto temp_item = tree_smallest(data->item_tree_); temp_item != nullptr; temp_item =
                 TreeNext(temp_item)) {
                if (temp_item->is_unmovable_ == true) continue;
                if (temp_item->is_excluded_ == true) continue;
                if (temp_item->Clusters == 0) continue;

                int file_zone = 1;

                if (temp_item->is_hog_ == true) file_zone = 2;
                if (temp_item->is_dir_ == true) file_zone = 0;
                if (file_zone != data->zone_) continue;

                if ((previous_item != nullptr) &&
                    (CompareItems(previous_item, temp_item, sort_field) >= 0)) {
                    continue;
                }

                phase_temp = phase_temp + temp_item->Clusters;

                if ((item != nullptr) && (CompareItems(temp_item, item, sort_field) >= 0)) continue;

                item = temp_item;
            }

            if (item == nullptr) {
                gui->show_debug(DebugLevel::Progress, nullptr, L"Finished sorting zone %u.", data->zone_ + 1);

                break;
            }

            previous_item = item;
            data->PhaseTodo = data->PhaseDone + phase_temp;

            /* If the item is already at the Lcn then skip. */
            if (get_item_lcn(item) == lcn) {
                lcn = lcn + item->Clusters;

                continue;
            }

            /* Move the item to the Lcn. If the gap at Lcn is not big enough then fragment
            the file into whatever gaps are available. */
            uint64_t clusters_done = 0;

            while ((*data->running_ == RunningState::RUNNING) &&
                (clusters_done < item->Clusters) &&
                (item->is_unmovable_ == false)) {
                if (clusters_done > 0) {
                    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Item partially placed, %I64u clusters more to do",
                                   item->Clusters - clusters_done);
                }

                /* Call the Vacate() function to make a gap at Lcn big enough to hold the item.
                The Vacate() function may not be able to move whatever is now at the Lcn, so
                after calling it we have to locate the first gap after the Lcn. */
                if (gap_begin + item->Clusters - clusters_done + 16 > gap_end) {
                    Vacate(data, lcn, item->Clusters - clusters_done + minimum_vacate,FALSE);

                    result = find_gap(data, lcn, 0, 0, true, false, &gap_begin, &gap_end, FALSE);

                    if (result == false) return; /* No gaps found, exit. */
                }

                /* If the gap is not big enough to hold the entire item then calculate how much
                of the item will fit in the gap. */
                uint64_t clusters = item->Clusters - clusters_done;

                if (clusters > gap_end - gap_begin) {
                    clusters = gap_end - gap_begin;

                    /* It looks like a partial move only succeeds if the number of clusters is a
                    multiple of 8. */
                    clusters = clusters - (clusters % 8);

                    if (clusters == 0) {
                        lcn = gap_end;
                        continue;
                    }
                }

                /* Move the item to the gap. */
                result = MoveItem(data, item, gap_begin, clusters_done, clusters, 0);

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
void DefragLib::move_mft_to_begin_of_disk(struct DefragDataStruct* data) {
    struct ItemStruct* item;

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
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Cannot move the MFT because this is not an NTFS disk.");

        return;
    }

    /* The Microsoft defragmentation api only supports moving the MFT on Vista. */
    ZeroMemory(&os_version, sizeof(OSVERSIONINFO));

    os_version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if ((GetVersionEx(&os_version) != 0) && (os_version.dwMajorVersion < 6)) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Cannot move the MFT because it is not supported by this version of Windows.");

        return;
    }

    /* Locate the Item for the MFT. If not found then exit. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = TreeNext(item)) {
        if (match_mask(item->LongPath, L"?:\\$MFT") == true) break;
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

    while ((*data->running_ == RunningState::RUNNING) && (clusters_done < item->Clusters)) {
        if (clusters_done > data->disk_.mft_locked_clusters_) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Partially placed, %I64u clusters more to do",
                           item->Clusters - clusters_done);
        }

        /* Call the Vacate() function to make a gap at Lcn big enough to hold the MFT.
        The Vacate() function may not be able to move whatever is now at the Lcn, so
        after calling it we have to locate the first gap after the Lcn. */
        if (gap_begin + item->Clusters - clusters_done + 16 > gap_end) {
            Vacate(data, lcn, item->Clusters - clusters_done,TRUE);

            result = find_gap(data, lcn, 0, 0, true, false, &gap_begin, &gap_end, TRUE);

            if (result == false) return; /* No gaps found, exit. */
        }

        /* If the gap is not big enough to hold the entire MFT then calculate how much
        will fit in the gap. */
        clusters = item->Clusters - clusters_done;

        if (clusters > gap_end - gap_begin) {
            clusters = gap_end - gap_begin;
            /* It looks like a partial move only succeeds if the number of clusters is a
            multiple of 8. */
            clusters = clusters - (clusters % 8);

            if (clusters == 0) {
                lcn = gap_end;

                continue;
            }
        }

        /* Move the MFT to the gap. */
        result = MoveItem(data, item, gap_begin, clusters_done, clusters, 0);

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

    ColorizeItem(data, item, 0, 0, false);
    CalculateZones(data);

    /* Note: The MftExcludes do not change by moving the MFT. */
}

/* Optimize the harddisk by filling gaps with files from above. */
void DefragLib::optimize_volume(struct DefragDataStruct* data) {
    struct ItemStruct* item;

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

            for (item = TreeBiggest(data->item_tree_); item != nullptr; item = TreePrev(item)) {
                if (get_item_lcn(item) < gap_end) break;
                if (item->is_unmovable_ == true) continue;
                if (item->is_excluded_ == true) continue;

                int file_zone = 1;

                if (item->is_hog_ == true) file_zone = 2;
                if (item->is_dir_ == true) file_zone = 0;
                if (file_zone != zone) continue;

                phase_temp = phase_temp + item->Clusters;
            }

            data->PhaseTodo = data->PhaseDone + phase_temp;
            if (phase_temp == 0) break;

            /* Loop until the gap is filled. First look for combinations of files that perfectly
            fill the gap. If no combination can be found, or if there are less files than
            the gap is big, then fill with the highest file(s) that fit in the gap. */
            bool perfect_fit = true;
            if (gap_end - gap_begin > phase_temp) perfect_fit = false;

            while ((gap_begin < gap_end) && (retry < 5) && (*data->running_ == RunningState::RUNNING)) {
                /* Find the Item that is the best fit for the gap. If nothing found (no files
                fit the gap) then exit the loop. */
                if (perfect_fit == true) {
                    item = FindBestItem(data, gap_begin, gap_end, 1, zone);

                    if (item == nullptr) {
                        perfect_fit = false;

                        item = FindHighestItem(data, gap_begin, gap_end, 1, zone);
                    }
                }
                else {
                    item = FindHighestItem(data, gap_begin, gap_end, 1, zone);
                }

                if (item == nullptr) break;

                /* Move the item. */
                result = MoveItem(data, item, gap_begin, 0, item->Clusters, 0);

                if (result == true) {
                    gap_begin = gap_begin + item->Clusters;
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
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, data->DebugMsg[28], gap_begin, gap_end - gap_begin);

                gap_begin = gap_end;
                retry = 0;
            }
        }
    }
}

/* Optimize the harddisk by moving the selected items up. */
void DefragLib::optimize_up(struct DefragDataStruct* data) {
    struct ItemStruct* item;

    uint64_t gap_begin;
    uint64_t gap_end;

    DefragGui* gui = DefragGui::get_instance();

    call_show_status(data, 6, -1); /* "Phase 3: Move Up" */

    /* Setup the progress counter: the total number of clusters in all files. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = TreeNext(item)) {
        data->PhaseTodo = data->PhaseTodo + item->Clusters;
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

        for (item = tree_smallest(data->item_tree_); item != nullptr; item = TreeNext(item)) {
            if (item->is_unmovable_ == true) continue;
            if (item->is_excluded_ == true) continue;
            if (get_item_lcn(item) >= gap_end) break;

            phase_temp = phase_temp + item->Clusters;
        }

        data->PhaseTodo = data->PhaseDone + phase_temp;
        if (phase_temp == 0) break;

        /* Loop until the gap is filled. First look for combinations of files that perfectly
        fill the gap. If no combination can be found, or if there are less files than
        the gap is big, then fill with the highest file(s) that fit in the gap. */
        bool perfect_fit = true;
        if (gap_end - gap_begin > phase_temp) perfect_fit = false;

        while ((gap_begin < gap_end) && (retry < 5) && (*data->running_ == RunningState::RUNNING)) {
            /* Find the Item that is the best fit for the gap. If nothing found (no files
            fit the gap) then exit the loop. */
            if (perfect_fit == true) {
                item = FindBestItem(data, gap_begin, gap_end, 0, 3);

                if (item == nullptr) {
                    perfect_fit = false;
                    item = FindHighestItem(data, gap_begin, gap_end, 0, 3);
                }
            }
            else {
                item = FindHighestItem(data, gap_begin, gap_end, 0, 3);
            }

            if (item == nullptr) break;

            /* Move the item. */
            result = MoveItem(data, item, gap_end - item->Clusters, 0, item->Clusters, 1);

            if (result == true) {
                gap_end = gap_end - item->Clusters;
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
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, data->DebugMsg[28], gap_begin, gap_end - gap_begin);

            gap_end = gap_begin;
            retry = 0;
        }
    }
}

/* Run the defragmenter. Input is the name of a disk, mountpoint, directory, or file,
and may contain wildcards '*' and '?'. */
void DefragLib::DefragOnePath(struct DefragDataStruct* data, WCHAR* path, OptimizeMode opt_mode) {
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

    struct __timeb64 Time;

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
    data->mft_excludes_[0].Start = 0;
    data->mft_excludes_[0].End = 0;
    data->mft_excludes_[1].Start = 0;
    data->mft_excludes_[1].End = 0;
    data->mft_excludes_[2].Start = 0;
    data->mft_excludes_[2].End = 0;
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
    data->PhaseTodo = 0;
    data->PhaseDone = 0;

    _ftime64_s(&Time);

    data->StartTime = Time.time * 1000 + Time.millitm;
    data->LastCheckpoint = data->StartTime;
    data->RunningTime = 0;

    /* Compare the item with the Exclude masks. If a mask matches then return,
    ignoring the item. */
    if (data->excludes_ != nullptr) {
        for (i = 0; data->excludes_[i] != nullptr; i++) {
            if (this->match_mask(path, data->excludes_[i]) == true) break;
            if ((wcschr(data->excludes_[i], L'*') == nullptr) &&
                (wcslen(data->excludes_[i]) <= 3) &&
                (lower_case(path[0]) == lower_case(data->excludes_[i][0])))
                break;
        }

        if (data->excludes_[i] != nullptr) {
            /* Show debug message: "Ignoring volume '%s' because of exclude mask '%s'." */
            jkGui->show_debug(DebugLevel::Fatal, nullptr, data->DebugMsg[47], path, data->excludes_[i]);
            return;
        }
    }

    /* Clear the screen and show "Processing '%s'" message. */
    jkGui->clear_screen(data->DebugMsg[14], path);

    /* Try to change our permissions so we can access special files and directories
    such as "C:\System Volume Information". If this does not succeed then quietly
    continue, we'll just have to do with whatever permissions we have.
    SE_BACKUP_NAME = Backup and Restore Privileges.
    */
    if ((OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &ProcessTokenHandle) != 0) &&
        (LookupPrivilegeValue(0,SE_BACKUP_NAME, &TakeOwnershipValue) != 0)) {
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

            jkGui->show_debug(DebugLevel::Fatal, nullptr, data->DebugMsg[40], data->disk_.mount_point_slash_, s1);

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

    if ((Result == 0) && (Fin != nullptr)) {
        w = 0;

        if ((fread(&w, 4, 1, Fin) == 1) && (w != 0)) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"Will not process this disk, it contains hybernated data.");

            free(data->disk_.mount_point_);
            free(data->disk_.mount_point_slash_);
            free(p1);

            return;
        }
    }

    free(p1);

    /* Show debug message: "Opening volume '%s' at mountpoint '%s'" */
    jkGui->show_debug(DebugLevel::Fatal, nullptr, data->DebugMsg[29], data->disk_.volume_name_,
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
                                &BitmapParam, sizeof(BitmapParam), &BitmapData, sizeof(BitmapData), &w, nullptr);

    if (ErrorCode != 0) {
        ErrorCode = NO_ERROR;
    }
    else {
        ErrorCode = GetLastError();
    }

    if ((ErrorCode != NO_ERROR) && (ErrorCode != ERROR_MORE_DATA)) {
        /* Show debug message: "Cannot defragment volume '%s' at mountpoint '%s'" */
        jkGui->show_debug(DebugLevel::Fatal, nullptr, data->DebugMsg[32], data->disk_.volume_name_,
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
                                nullptr, 0, &NtfsData, sizeof(NtfsData), &w, nullptr);

    if (ErrorCode != 0) {
        /* Note: NtfsData.TotalClusters.QuadPart should be exactly the same
        as the Data->TotalClusters that was determined in the previous block. */

        data->bytes_per_cluster_ = NtfsData.BytesPerCluster;

        data->mft_excludes_[0].Start = NtfsData.MftStartLcn.QuadPart;
        data->mft_excludes_[0].End = NtfsData.MftStartLcn.QuadPart +
                NtfsData.MftValidDataLength.QuadPart / NtfsData.BytesPerCluster;
        data->mft_excludes_[1].Start = NtfsData.MftZoneStart.QuadPart;
        data->mft_excludes_[1].End = NtfsData.MftZoneEnd.QuadPart;
        data->mft_excludes_[2].Start = NtfsData.Mft2StartLcn.QuadPart;
        data->mft_excludes_[2].End = NtfsData.Mft2StartLcn.QuadPart +
                NtfsData.MftValidDataLength.QuadPart / NtfsData.BytesPerCluster;

        /* Show debug message: "MftStartLcn=%I64d, MftZoneStart=%I64d, MftZoneEnd=%I64d, Mft2StartLcn=%I64d, MftValidDataLength=%I64d" */
        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, data->DebugMsg[33],
                         NtfsData.MftStartLcn.QuadPart, NtfsData.MftZoneStart.QuadPart,
                         NtfsData.MftZoneEnd.QuadPart, NtfsData.Mft2StartLcn.QuadPart,
                         NtfsData.MftValidDataLength.QuadPart / NtfsData.BytesPerCluster);

        /* Show debug message: "MftExcludes[%u].Start=%I64d, MftExcludes[%u].End=%I64d" */
        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, data->DebugMsg[34], 0, data->mft_excludes_[0].Start, 0,
                         data->mft_excludes_[0].End);
        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, data->DebugMsg[34], 1, data->mft_excludes_[1].Start, 1,
                         data->mft_excludes_[1].End);
        jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, data->DebugMsg[34], 2, data->mft_excludes_[2].Start, 2,
                         data->mft_excludes_[2].End);
    }

    /* Fixup the input mask.
    - If the length is 2 or 3 characters then rewrite into "c:\*".
    - If it does not contain a wildcard then append '*'.
    */
    Length = wcslen(path) + 3;

    data->include_mask_ = (WCHAR*)malloc(sizeof(WCHAR) * Length);

    if (data->include_mask_ == nullptr) return;

    wcscpy_s(data->include_mask_, Length, path);

    if ((wcslen(path) == 2) || (wcslen(path) == 3)) {
        swprintf_s(data->include_mask_, Length, L"%c:\\*", lower_case(path[0]));
    }
    else if (wcschr(path, L'*') == nullptr) {
        swprintf_s(data->include_mask_, Length, L"%s*", path);
    }

    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"Input mask: %s", data->include_mask_);

    /* Defragment and optimize. */
    jkGui->ShowDiskmap(data);

    if (*data->running_ == RunningState::RUNNING) AnalyzeVolume(data);

    if ((*data->running_ == RunningState::RUNNING) && (opt_mode.mode_ == 1)) {
        Defragment(data);
    }

    if ((*data->running_ == RunningState::RUNNING) && ((opt_mode.mode_ == 2) || (opt_mode.mode_ == 3))) {
        Defragment(data);

        if (*data->running_ == RunningState::RUNNING) fixup(data);
        if (*data->running_ == RunningState::RUNNING) optimize_volume(data);
        if (*data->running_ == RunningState::RUNNING) fixup(data); /* Again, in case of new zone startpoint. */
    }

    if ((*data->running_ == RunningState::RUNNING) && (opt_mode.mode_ == 4)) {
        ForcedFill(data);
    }

    if ((*data->running_ == RunningState::RUNNING) && (opt_mode.mode_ == 5)) {
        optimize_up(data);
    }

    if ((*data->running_ == RunningState::RUNNING) && (opt_mode.mode_ == 6)) {
        optimize_sort(data, 0); /* Filename */
    }

    if ((*data->running_ == RunningState::RUNNING) && (opt_mode.mode_ == 7)) {
        optimize_sort(data, 1); /* Filesize */
    }

    if ((*data->running_ == RunningState::RUNNING) && (opt_mode.mode_ == 8)) {
        optimize_sort(data, 2); /* Last access */
    }

    if ((*data->running_ == RunningState::RUNNING) && (opt_mode.mode_ == 9)) {
        optimize_sort(data, 3); /* Last change */
    }

    if ((*data->running_ == RunningState::RUNNING) && (opt_mode.mode_ == 10)) {
        optimize_sort(data, 4); /* Creation */
    }
    /*
    if ((*Data->Running == RUNNING) && (Mode == 11)) {
    MoveMftToBeginOfDisk(Data);
    }
    */

    call_show_status(data, 7, -1); /* "Finished." */

    /* Close the volume handles. */
    if ((data->disk_.volume_handle_ != nullptr) &&
        (data->disk_.volume_handle_ != INVALID_HANDLE_VALUE)) {
        CloseHandle(data->disk_.volume_handle_);
    }

    /* Cleanup. */
    DeleteItemTree(data->item_tree_);

    if (data->disk_.mount_point_ != nullptr) free(data->disk_.mount_point_);
    if (data->disk_.mount_point_slash_ != nullptr) free(data->disk_.mount_point_slash_);
}

/* Subfunction for DefragAllDisks(). It will ignore removable disks, and
will iterate for disks that are mounted on a subdirectory of another
disk (instead of being mounted on a drive). */
void DefragLib::DefragMountpoints(struct DefragDataStruct* Data, WCHAR* MountPoint, OptimizeMode opt_mode) {
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
    jkGui->clear_screen(Data->DebugMsg[37], MountPoint);

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
            jkGui->show_debug(DebugLevel::Fatal, nullptr, Data->DebugMsg[57], MountPoint);
        }
        else {
            /* "Cannot find volume name for mountpoint: %s" */
            system_error_str(ErrorCode, s1,BUFSIZ);

            jkGui->show_debug(DebugLevel::Fatal, nullptr, Data->DebugMsg[40], MountPoint, s1);
        }

        return;
    }

    /* Return if the disk is read-only. */
    GetVolumeInformationW(VolumeNameSlash, nullptr, 0, nullptr, nullptr, &FileSystemFlags, nullptr, 0);

    if ((FileSystemFlags & FILE_READ_ONLY_VOLUME) != 0) {
        /* Clear the screen and show message "Ignoring disk '%s' because it is read-only." */
        jkGui->clear_screen(Data->DebugMsg[36], MountPoint);

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
        jkGui->show_debug(DebugLevel::Fatal, nullptr, Data->DebugMsg[31], VolumeName, MountPoint);

        CloseHandle(VolumeHandle);

        return;
    }

    CloseHandle(VolumeHandle);

    /* Defrag the disk. */
    Length = wcslen(MountPoint) + 2;

    p1 = (WCHAR*)malloc(sizeof(WCHAR) * Length);

    if (p1 != nullptr) {
        swprintf_s(p1, Length, L"%s*", MountPoint);

        DefragOnePath(Data, p1, opt_mode);

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

            DefragMountpoints(Data, FullRootPath, opt_mode);

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
    struct DefragDataStruct data{};

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
    data.Speed = speed;
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

    if ((debug_msg == nullptr) || (debug_msg[0] == nullptr)) {
        data.DebugMsg = DefaultDebugMsg;
    }
    else {
        data.DebugMsg = debug_msg;
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
            Length = sizeof(NtfsDisableLastAccessUpdate);

            Result = RegQueryValueExW(Key, L"NtfsDisableLastAccessUpdate", nullptr, nullptr,
                                      (BYTE*)&NtfsDisableLastAccessUpdate, &Length);

            if ((Result == ERROR_SUCCESS) && (NtfsDisableLastAccessUpdate == 1)) {
                data.use_last_access_time_ = FALSE;
            }

            RegCloseKey(Key);
        }

        if (data.use_last_access_time_ == TRUE) {
            gui->show_debug(
                DebugLevel::Warning, nullptr, L"NtfsDisableLastAccessUpdate is inactive, using LastAccessTime for SpaceHogs.");
        }
        else {
            gui->show_debug(
                DebugLevel::Warning, nullptr, L"NtfsDisableLastAccessUpdate is active, ignoring LastAccessTime for SpaceHogs.");
        }
    }

    /* If a Path is specified then call DefragOnePath() for that path. Otherwise call
    DefragMountpoints() for every disk in the system. */
    if ((path != nullptr) && (*path != 0)) {
        DefragOnePath(&data, path, optimize_mode);
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

                gui->show_debug(DebugLevel::Warning, nullptr, data.DebugMsg[39], s1);
            }
            else {
                WCHAR* drive;
                drive = drives;

                while (*drive != '\0') {
                    DefragMountpoints(&data, drive, optimize_mode);
                    while (*drive != '\0') drive++;
                    drive++;
                }
            }

            free(drives);
        }

        gui->clear_screen(data.DebugMsg[38]);
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
