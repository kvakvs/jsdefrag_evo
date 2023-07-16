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

#include "precompiled_header.h"

#include <memory>
#include <optional>
#include <cwctype>

#include "defrag_data_struct.h"
#include "defrag_lib.h"


DefragLib::DefragLib() = default;

DefragLib::~DefragLib() = default;

DefragLib *DefragLib::get_instance() {
    if (instance_ == nullptr) {
        instance_ = std::make_unique<DefragLib>();
    }

    return instance_.get();
}

///
//
//All the text strings used by the defragger library.
//Note: The RunJkDefrag() function call has a parameter where you can specify
//a different array. Do not change this default array, simply create a new
//array in your program and specify it as a parameter.
//
///
//const wchar_t *DEPRECATED_default_debug_msg[] =
//        {
//                /*  0 */ L"",
//                /*  1 */ L"",
//                /*  2 */ L"",
//                /*  3 */ L"",
//                /*  4 */ L"",
//                /*  5 */ L"",
//                /*  6 */ L"",
//                /*  7 */ L"",
//                /*  8 */ L"",
//                /*  9 */ L"",
////                /* 10 */ L"Getting cluster bitmap: %s",
////                /* 11 */ L"Extent: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u",
//                /* 12 */ L"ERROR: could not get volume bitmap: %s",
////                /* 13 */ L"Gap found: LCN=%I64d, Size=%I64d",
//                /* 14 */ L"Processing '%s'",
////                /* 15 */ L"Could not open '%s': %s",
////                /* 16 */ L"%I64d clusters at %I64d, %I64d bytes",
////                /* 17 */ L"Special file attribute: Compressed",
////                /* 18 */ L"Special file attribute: Encrypted",
////                /* 19 */ L"Special file attribute: Offline",
////                /* 20 */ L"Special file attribute: Read-only",
////                /* 21 */ L"Special file attribute: Sparse-file",
////                /* 22 */ L"Special file attribute: Temporary",
////                /* 23 */ L"Analyzing: %s",
//                /* 24 */ L"",
////                /* 25 */ L"Cannot move file away because no gap is big enough: %I64d[%I64d]",
//                /* 26 */ L"Don't know which file is at the end of the gap: %I64d[%I64d]",
//                /* 27 */ L"Enlarging gap %I64d[%I64d] by moving %I64d[%I64d]",
////                /* 28 */ L"Skipping gap, cannot fill: %I64d[%I64d]",
////                /* 29 */ L"Opening volume '%s' at mountpoint '%s'",
//                /* 30 */ L"",
////                /* 31 */ L"Volume '%s' at mountpoint '%s' is not mounted.",
////                /* 32 */ L"Cannot defragment volume '%s' at mountpoint '%s'",
////                /* 33 */ L"MftStartLcn=%I64d, MftZoneStart=%I64d, MftZoneEnd=%I64d, Mft2StartLcn=%I64d, MftValidDataLength=%I64d",
////                /* 34 */ L"MftExcludes[%u].Start=%I64d, MftExcludes[%u].End=%I64d",
//                /* 35 */ L"",
////                /* 36 */ L"Ignoring volume '%s' because it is read-only.",
//                /* 37 */ L"Analyzing volume '%s'",
////                /* 38 */ L"Finished.",
////                /* 39 */ L"Could not get list of volumes: %s",
////                /* 40 */ L"Cannot find volume name for mountpoint '%s': %s",
//                /* 41 */ L"Cannot enlarge gap at %I64d[%I64d] because of unmovable data.",
//                /* 42 */ L"Windows could not move the file, trying alternative method.",
////                /* 43 */ L"Cannot process clustermap of '%s': %s",
////                /* 44 */ L"Disk is full, cannot defragment.",
//                /* 45 */ L"Alternative method failed, leaving file where it is.",
////                /* 46 */ L"Extent (virtual): Vcn=%I64u, NextVcn=%I64u",
////                /* 47 */ L"Ignoring volume '%s' because of exclude mask '%s'.",
//                /* 48 */ L"Vacating %I64u clusters starting at LCN=%I64u",
//                /* 49 */ L"Vacated %I64u clusters (until %I64u) from LCN=%I64u",
//                /* 50 */ L"Finished vacating %I64u clusters, until LCN=%I64u",
//                /* 51 */ L"",
//                /* 52 */ L"",
//                /* 53 */ L"I am fragmented.",
////                /* 54 */ L"I am in MFT reserved space.",
////                /* 55 */ L"I am a regular file in zone 1.",
////                /* 56 */ L"I am a spacehog in zone 1 or 2.",
////                /* 57 */ L"Ignoring volume '%s' because it is not a harddisk."
//        };

// Search case-insensitive for a substring
const wchar_t *DefragLib::stristr_w(const wchar_t *haystack, const wchar_t *needle) {
    if (haystack == nullptr || needle == nullptr) return nullptr;

    const wchar_t *p1 = haystack;
    const size_t i = wcslen(needle);

    while (*p1 != 0) {
        if (_wcsnicmp(p1, needle, i) == 0) return p1;

        p1++;
    }

    return nullptr;
}

/* Return a string with the error message for GetLastError(). */
void DefragLib::system_error_str(const uint32_t error_code, wchar_t *out, const size_t width) {
    wchar_t buffer[BUFSIZ];

    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                   nullptr, error_code, 0, buffer, BUFSIZ, nullptr);

    /* Strip trailing whitespace. */
    wchar_t *p1 = wcschr(buffer, '\0');

    while (p1 != buffer) {
        p1--;
        if (!std::iswspace(*p1)) break;
        *p1 = '\0';
    }

    // Add error number. 
    swprintf_s(out, width, L"[%lu] %s", error_code, buffer);
}

/* Translate character to lowercase. */
wchar_t DefragLib::lower_case(const wchar_t c) {
    if (std::iswupper(c)) return c - 'A' + 'a';

    return c;
}

/* Dump a block of data to standard output, for debugging purposes. */
void DefragLib::show_hex([[maybe_unused]] DefragDataStruct *data, const BYTE *buffer,
                         const uint64_t count) {
    DefragGui *gui = DefragGui::get_instance();

    int j;

    for (int i = 0; i < count; i = i + 16) {
        wchar_t s2[BUFSIZ];
        wchar_t s1[BUFSIZ];
        swprintf_s(s1, BUFSIZ, L"%4u %4X   ", i, i);

        for (j = 0; j < 16; j++) {
            if (j == 8) wcscat_s(s1, BUFSIZ, L" ");

            if (j + i >= count) {
                wcscat_s(s1, BUFSIZ, L"   ");
            } else {
                swprintf_s(s2, BUFSIZ, L"%02X ", buffer[i + j]);
                wcscat_s(s1, BUFSIZ, s2);
            }
        }

        wcscat_s(s1, BUFSIZ, L" ");

        for (j = 0; j < 16; j++) {
            if (j + i >= count) {
                wcscat_s(s1, BUFSIZ, L" ");
            } else {
                if (buffer[i + j] < 32) {
                    wcscat_s(s1, BUFSIZ, L".");
                } else {
                    swprintf_s(s2, BUFSIZ, L"%c", buffer[i + j]);
                    wcscat_s(s1, BUFSIZ, s2);
                }
            }
        }

        gui->show_debug(DebugLevel::Progress, nullptr, s1);
    }
}

/*

Compare a string with a mask, case-insensitive. If it matches then return
true, otherwise false. The mask may contain wildcard characters '?' (any
character) '*' (any characters).

*/
bool DefragLib::match_mask(const wchar_t *string, const wchar_t *mask) {
    if (string == nullptr) return false; /* Just to speed up things. */
    if (mask == nullptr) return false;
    if (wcscmp(mask, L"*") == 0) return true;

    auto m = mask;
    auto s = string;

    while (*m != '\0' && *s != '\0') {
        if (lower_case(*m) != lower_case(*s) && *m != '?') {
            if (*m != '*') return false;

            m++;

            if (*m == '\0') return true;

            while (*s != '\0') {
                if (match_mask(s, m)) return true;
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


/* Subfunction of GetShortPath(). */
void DefragLib::append_to_short_path(const ItemStruct *item, std::wstring &path) {
    if (item->parent_directory_ != nullptr) append_to_short_path(item->parent_directory_, path);

    path += L"\\";
    path += item->get_short_fn(); // will append short if not empty otherwise will append long
}

/*
Return a string with the full path of an item, constructed from the short names.
*/
std::wstring DefragLib::get_short_path(const DefragDataStruct *data, const ItemStruct *item) {
    /* Sanity check. */
    if (item == nullptr) return {};

    /* Count the size of all the ShortFilename's. */
    size_t length = wcslen(data->disk_.mount_point_.get()) + 1;

    for (auto temp_item = item; temp_item != nullptr; temp_item = temp_item->parent_directory_) {
        length = length + wcslen(temp_item->get_short_fn()) + 1;
    }

    // Allocate new string
    std::wstring path = data->disk_.mount_point_.get();

    // Append all the strings
    append_to_short_path(item, path);

    return path;
}

/* Subfunction of GetLongPath(). */
void DefragLib::append_to_long_path(const ItemStruct *item, std::wstring &path) {
    if (item->parent_directory_ != nullptr) append_to_long_path(item->parent_directory_, path);

    path += L"\\";
    path += item->get_short_fn(); // will append long if not empty otherwise will append short
}

/*
Return a string with the full path of an item, constructed from the long names.
*/
std::wstring DefragLib::get_long_path(const DefragDataStruct *data, const ItemStruct *item) {
    // Sanity check
    if (item == nullptr) return {};

    // Count the size of all the LongFilename's
    size_t length = wcslen(data->disk_.mount_point_.get()) + 1;

    for (auto temp_item = item; temp_item != nullptr; temp_item = temp_item->parent_directory_) {
        length += wcslen(temp_item->get_long_fn()) + 1;
    }

    std::wstring path = data->disk_.mount_point_.get();

    // Append all the strings
    append_to_long_path(item, path);
    return path;
}

/* Slow the program down. */
void DefragLib::slow_down(DefragDataStruct *data) {
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
        int64_t delay = data->running_time_ * (int64_t) 100 / (int64_t) data->speed_ - (now - data->start_time_);

        if (delay > 30000) delay = 30000;
        if (delay > 0) Sleep((uint32_t) delay);
    }

    /* Save the current wall time, so next time we can calculate the time spent in	the program. */
    _ftime64_s(&t);

    data->last_checkpoint_ = t.time * 1000 + t.millitm;
}

/* Return the location on disk (LCN, Logical Cluster Number) of an item. */
uint64_t DefragLib::get_item_lcn(const ItemStruct *item) {
    // Sanity check
    if (item == nullptr) return 0;

    const FragmentListStruct *fragment = item->fragments_;

    while (fragment != nullptr && fragment->lcn_ == VIRTUALFRAGMENT) {
        fragment = fragment->next_;
    }
    return fragment == nullptr ? 0 : fragment->lcn_;
}


/*
Open the item as a file or as a directory. If the item could not be
opened then show an error message and return nullptr.
*/
HANDLE DefragLib::open_item_handle(const DefragDataStruct *data, const ItemStruct *item) {
    HANDLE file_handle;
    wchar_t error_string[BUFSIZ];
    const size_t length = wcslen(item->get_long_path()) + 5;
    auto path = std::make_unique<wchar_t[]>(length);

    swprintf_s(path.get(), length, L"\\\\?\\%s", item->get_long_path());

    if (item->is_dir_) {
        file_handle = CreateFileW(path.get(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    } else {
        file_handle = CreateFileW(path.get(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
    }

    if (file_handle != INVALID_HANDLE_VALUE) return file_handle;

    /* Show error message: "Could not open '%s': %s" */
    system_error_str(GetLastError(), error_string, BUFSIZ);

    DefragGui *gui = DefragGui::get_instance();
    gui->show_debug(DebugLevel::DetailedFileInfo, nullptr,
                    std::format(L"Could not open '{}': {}", item->get_long_path(), error_string));

    return nullptr;
}

/*

Analyze an item (file, directory) and update it's Clusters and Fragments
in memory. If there was an error then return false, otherwise return true.
Note: Very small files are stored by Windows in the MFT and have no
clusters (zero) and no fragments (nullptr).

*/
bool DefragLib::get_fragments(const DefragDataStruct *data, ItemStruct *item, HANDLE file_handle) {
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
    FragmentListStruct *last_fragment;
    uint32_t error_code;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    /* Initialize. If the item has an old list of fragments then delete it. */
    item->clusters_count_ = 0;

    while (item->fragments_ != nullptr) {
        last_fragment = item->fragments_->next_;
        delete item->fragments_;
        item->fragments_ = last_fragment;
    }

    // Fetch the date/times of the file
    if (item->creation_time_.count() == 0 &&
        item->last_access_time_.count() == 0 &&
        item->mft_change_time_.count() == 0 &&
        GetFileInformationByHandle(file_handle, &file_information) != 0) {
        ULARGE_INTEGER u;
        u.LowPart = file_information.ftCreationTime.dwLowDateTime;
        u.HighPart = file_information.ftCreationTime.dwHighDateTime;

        item->creation_time_ = std::chrono::microseconds(u.QuadPart);

        u.LowPart = file_information.ftLastAccessTime.dwLowDateTime;
        u.HighPart = file_information.ftLastAccessTime.dwHighDateTime;

        item->last_access_time_ = std::chrono::microseconds(u.QuadPart);

        u.LowPart = file_information.ftLastWriteTime.dwLowDateTime;
        u.HighPart = file_information.ftLastWriteTime.dwHighDateTime;

        item->mft_change_time_ = std::chrono::microseconds(u.QuadPart);
    }

    /* Show debug message: "Getting cluster bitmap: %s" */
    gui->show_debug(DebugLevel::DetailedFileInfo, nullptr,
                    std::format(L"Getting cluster bitmap: {}", item->get_long_path()));

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

        error_code = DeviceIoControl(file_handle, FSCTL_GET_RETRIEVAL_POINTERS,
                                     &RetrieveParam, sizeof RetrieveParam, &extent_data, sizeof extent_data, &w,
                                     nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        } else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) break;

        /* Walk through the clustermap, count the total number of clusters, and
        save all fragments in memory. */
        for (uint32_t i = 0; i < extent_data.extent_count_; i++) {
            /* Show debug message. */
            if (extent_data.extents_[i].lcn_ != VIRTUALFRAGMENT) {
                /* "Extent: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u" */
                gui->show_debug(
                        DebugLevel::DetailedFileInfo, nullptr,
                        std::format(EXTENT_FMT, extent_data.extents_[i].lcn_, vcn, extent_data.extents_[i].next_vcn_));
            } else {
                /* "Extent (virtual): Vcn=%I64u, NextVcn=%I64u" */
                gui->show_debug(
                        DebugLevel::DetailedFileInfo, nullptr,
                        std::format(VEXTENT_FMT, vcn, extent_data.extents_[i].next_vcn_));
            }

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */
            if (extent_data.extents_[i].lcn_ != VIRTUALFRAGMENT) {
                item->clusters_count_ = item->clusters_count_ + extent_data.extents_[i].next_vcn_ - vcn;
            }

            /* Add the fragment to the Fragments. */

            auto new_fragment = new FragmentListStruct();
            new_fragment->lcn_ = extent_data.extents_[i].lcn_;
            new_fragment->next_vcn_ = extent_data.extents_[i].next_vcn_;
            new_fragment->next_ = nullptr;

            if (item->fragments_ == nullptr) {
                item->fragments_ = new_fragment;
            } else {
                if (last_fragment != nullptr) last_fragment->next_ = new_fragment;
            }

            last_fragment = new_fragment;

            /* The Vcn of the next fragment is the NextVcn field in this record. */
            vcn = extent_data.extents_[i].next_vcn_;
        }

        /* Loop until we have processed the entire clustermap of the file. */
    } while (error_code == ERROR_MORE_DATA);

    /* If there was an error while reading the clustermap then return false. */
    if (error_code != NO_ERROR && error_code != ERROR_HANDLE_EOF) {
        wchar_t error_string[BUFSIZ];
        // Show debug message: "Cannot process clustermap of '%s': %s"
        system_error_str(error_code, error_string, BUFSIZ);

        gui->show_debug(
                DebugLevel::DetailedProgress, nullptr,
                std::format(L"Cannot process clustermap of '{}': {}", item->get_long_path(), error_string));

        return false;
    }

    return true;
}

/* Return the number of fragments in the item. */
int DefragLib::get_fragment_count(const ItemStruct *item) {
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
bool DefragLib::is_fragmented(const ItemStruct *item, const uint64_t offset, const uint64_t size) {
    /* Walk through all fragments. If a fragment is found where either the
    begin or the end of the fragment is inside the block then the file is
    fragmented and return true. */
    uint64_t fragment_begin = 0;
    uint64_t fragment_end = 0;
    uint64_t vcn = 0;
    uint64_t next_lcn = 0;
    const FragmentListStruct *fragment = item->fragments_;

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
void DefragLib::colorize_disk_item(DefragDataStruct *data, const ItemStruct *item, const uint64_t busy_offset,
                                   const uint64_t busy_size, const int un_draw) const {
    DefragGui *gui = DefragGui::get_instance();

    // Determine if the item is fragmented.
    const bool is_fragmented = this->is_fragmented(item, 0, item->clusters_count_);

    // Walk through all the fragments of the file.
    uint64_t vcn = 0;
    uint64_t real_vcn = 0;

    const FragmentListStruct *fragment = item->fragments_;

    while (fragment != nullptr) {
        // Ignore virtual fragments. They do not occupy space on disk and do not require colorization.
        if (fragment->lcn_ == VIRTUALFRAGMENT) {
            vcn = fragment->next_vcn_;
            fragment = fragment->next_;

            continue;
        }

        // Walk through all the segments of the file. A segment is usually the same as a fragment, but if a fragment spans across a boundary
        // then we must determine the color of the left and right parts individually. So we pretend the fragment is divided into segments
        // at the various possible boundaries.
        uint64_t segment_begin = real_vcn;

        while (segment_begin < real_vcn + fragment->next_vcn_ - vcn) {
            uint64_t segment_end = real_vcn + fragment->next_vcn_ - vcn;
            DrawColor color;

            // Determine the color with which to draw this segment.
            if (un_draw == false) {
                if (item->is_excluded_) color = DrawColor::Unmovable;
                else if (item->is_unmovable_) color = DrawColor::Unmovable;
                else if (is_fragmented) color = DrawColor::Fragmented;
                else if (item->is_hog_) color = DrawColor::SpaceHog;
                else color = DrawColor::Unfragmented;

                if (vcn + segment_begin - real_vcn < busy_offset &&
                    vcn + segment_end - real_vcn > busy_offset) {
                    segment_end = real_vcn + busy_offset - vcn;
                }

                if (vcn + segment_begin - real_vcn >= busy_offset &&
                    vcn + segment_begin - real_vcn < busy_offset + busy_size) {
                    if (vcn + segment_end - real_vcn > busy_offset + busy_size) {
                        segment_end = real_vcn + busy_offset + busy_size - vcn;
                    }

                    color = DrawColor::Busy;
                }
            } else {
                color = DrawColor::Empty;

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

                        color = DrawColor::Mft;
                    }
                }
            }

            // Colorize the segment
            gui->draw_cluster(data,
                              fragment->lcn_ + segment_begin - real_vcn,
                              fragment->lcn_ + segment_end - real_vcn,
                              color);

            // Next segment
            segment_begin = segment_end;
        }

        // Next fragment
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
							m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::Unmovable);
					} else if (PrevInUse == 0) {
						m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::Empty);
					} else {
						m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::Allocated);
					}
					InUse = 1;
					PrevInUse = 1;
					ClusterStart = Lcn;
			}
			if ((PrevInUse == 0) && (InUse != 0)) {          / * Free * /
				m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::Empty);
				ClusterStart = Lcn;
			}
			if ((PrevInUse != 0) && (InUse == 0)) {          / * In use * /
				m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::Allocated);
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
			m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::Empty);
		}
		if (PrevInUse != 0) {          / * In use * /
			m_jkGui->DrawCluster(Data,ClusterStart,Lcn,JKDefragStruct::Allocated);
		}
	}

	/ * Show the MFT zones. * /
	for (i = 0; i < 3; i++) {
		if (*Data->RedrawScreen != 2) break;
		if (Data->MftExcludes[i].Start <= 0) continue;
		m_jkGui->DrawCluster(Data,Data->MftExcludes[i].Start,Data->MftExcludes[i].End,JKDefragStruct::Mft);
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


/* Update some numbers in the DefragData. */
void DefragLib::call_show_status(DefragDataStruct *data, const int phase, const int zone) {
    ItemStruct *item;
    STARTING_LCN_INPUT_BUFFER bitmap_param;

    struct {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        // Most efficient if power of 2 
        BYTE buffer_[65536];
    } bitmap_data{};

    uint32_t error_code;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

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
        error_code = DeviceIoControl(data->disk_.volume_handle_, FSCTL_GET_VOLUME_BITMAP,
                                     &bitmap_param, sizeof bitmap_param, &bitmap_data, sizeof bitmap_data, &w, nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        } else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) break;

        lcn = bitmap_data.starting_lcn_;
        int index = 0;
        BYTE mask = 1;
        int index_max = sizeof bitmap_data.buffer_;

        if (bitmap_data.bitmap_size_ / 8 < index_max) index_max = (int) (bitmap_data.bitmap_size_ / 8);

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
            } else {
                mask = mask << 1;
            }

            lcn = lcn + 1;
        }
    } while (error_code == ERROR_MORE_DATA && lcn < bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_);

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
        if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
             _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
            continue;
        }

        data->count_all_bytes_ = data->count_all_bytes_ + item->bytes_;
        data->count_all_clusters_ = data->count_all_clusters_ + item->clusters_count_;

        if (item->is_dir_) {
            data->count_directories_ = data->count_directories_ + 1;
        } else {
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
        if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
             _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
            continue;
        }

        if (item->clusters_count_ == 0) continue;

        count = count + 1;
    }

    if (count > 1) {
        int64_t factor = 1 - count;
        int64_t sum = 0;

        for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
            if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
                 _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
                continue;
            }

            if (item->clusters_count_ == 0) continue;

            sum = sum + factor * (get_item_lcn(item) * 2 + item->clusters_count_);

            factor = factor + 2;
        }

        data->average_distance_ = sum / (double) (count * (count - 1));
    } else {
        data->average_distance_ = 0;
    }

    data->phase_ = phase;
    data->zone_ = zone;
    data->phase_done_ = 0;
    data->phase_todo_ = 0;

    gui->show_status(data);
}

/* Run the defragger/optimizer. See the .h file for a full explanation. */
void DefragLib::run_jk_defrag(wchar_t *path, OptimizeMode optimize_mode, int speed, double free_space,
                              const Wstrings &excludes, const Wstrings &space_hogs, RunningState *run_state) {
    DefragDataStruct data{};
    uint32_t ntfs_disable_last_access_update;
    DWORD key_disposition;
    DWORD length;
    DefragGui *gui = DefragGui::get_instance();

    /* Copy the input values to the data struct. */
    data.speed_ = speed;
    data.free_space_ = free_space;
    data.excludes_ = excludes;

    RunningState default_running;
    if (run_state == nullptr) {
        data.running_ = &default_running;
    } else {
        data.running_ = run_state;
    }

    *data.running_ = RunningState::RUNNING;

    // Make a copy of the SpaceHogs array
    data.space_hogs_.clear();
    data.use_default_space_hogs_ = true;

    for (const auto &sh: space_hogs) {
        if (_wcsicmp(sh.c_str(), L"DisableDefaults") == 0) {
            data.use_default_space_hogs_ = false;
        } else {
            data.space_hogs_.push_back(sh);
        }
    }

    if (data.use_default_space_hogs_) {
        data.space_hogs_.emplace_back(L"?:\\$RECYCLE.BIN\\*"); /* Vista */
        data.space_hogs_.emplace_back(L"?:\\RECYCLED\\*"); /* FAT on 2K/XP */
        data.space_hogs_.emplace_back(L"?:\\RECYCLER\\*"); /* NTFS on 2K/XP */
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\$*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\Downloaded Installations\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\Ehome\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\Fonts\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\Help\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\I386\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\IME\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\Installer\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\ServicePackFiles\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\SoftwareDistribution\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\Speech\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\Symbols\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\ie7updates\\*");
        data.space_hogs_.emplace_back(L"?:\\WINDOWS\\system32\\dllcache\\*");
        data.space_hogs_.emplace_back(L"?:\\WINNT\\$*");
        data.space_hogs_.emplace_back(L"?:\\WINNT\\Downloaded Installations\\*");
        data.space_hogs_.emplace_back(L"?:\\WINNT\\I386\\*");
        data.space_hogs_.emplace_back(L"?:\\WINNT\\Installer\\*");
        data.space_hogs_.emplace_back(L"?:\\WINNT\\ServicePackFiles\\*");
        data.space_hogs_.emplace_back(L"?:\\WINNT\\SoftwareDistribution\\*");
        data.space_hogs_.emplace_back(L"?:\\WINNT\\ie7updates\\*");
        data.space_hogs_.emplace_back(L"?:\\*\\Installshield Installation Information\\*");
        data.space_hogs_.emplace_back(L"?:\\I386\\*");
        data.space_hogs_.emplace_back(L"?:\\System Volume Information\\*");
        data.space_hogs_.emplace_back(L"?:\\windows.old\\*");

        data.space_hogs_.emplace_back(L"*.7z");
        data.space_hogs_.emplace_back(L"*.arj");
        data.space_hogs_.emplace_back(L"*.bz2");
        data.space_hogs_.emplace_back(L"*.gz");
        data.space_hogs_.emplace_back(L"*.z");
        data.space_hogs_.emplace_back(L"*.zip");

        data.space_hogs_.emplace_back(L"*.bak");
        data.space_hogs_.emplace_back(L"*.bup"); /* DVD */
        data.space_hogs_.emplace_back(L"*.cab");
        data.space_hogs_.emplace_back(L"*.chm"); /* Help files */
        data.space_hogs_.emplace_back(L"*.dvr-ms");
        data.space_hogs_.emplace_back(L"*.ifo"); /* DVD */
        data.space_hogs_.emplace_back(L"*.log");
        data.space_hogs_.emplace_back(L"*.lzh");
        data.space_hogs_.emplace_back(L"*.msi");
        data.space_hogs_.emplace_back(L"*.old");
        data.space_hogs_.emplace_back(L"*.pdf");
        data.space_hogs_.emplace_back(L"*.rar");
        data.space_hogs_.emplace_back(L"*.rpm");
        data.space_hogs_.emplace_back(L"*.tar");

        data.space_hogs_.emplace_back(L"*.avi");
        data.space_hogs_.emplace_back(L"*.mpg"); // MPEG2
        data.space_hogs_.emplace_back(L"*.mp3"); // MPEG3 sound
        data.space_hogs_.emplace_back(L"*.mp4"); // MPEG4 video
        data.space_hogs_.emplace_back(L"*.ogg"); // Ogg Vorbis sound
        data.space_hogs_.emplace_back(L"*.wmv"); // Windows media video
        data.space_hogs_.emplace_back(L"*.vob"); // DVD 
        data.space_hogs_.emplace_back(L"*.ogg"); // Ogg Vorbis Video 
    }

    /* If the NtfsDisableLastAccessUpdate setting in the registry is 1, then disable
    the LastAccessTime check for the spacehogs. */
    data.use_last_access_time_ = TRUE;

    if (data.use_default_space_hogs_ == TRUE) {
        LONG result;
        HKEY key;
        result = RegCreateKeyExW(
                HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", 0,
                nullptr, REG_OPTION_NON_VOLATILE, KEY_READ, nullptr, &key, &key_disposition);

        if (result == ERROR_SUCCESS) {
            length = sizeof ntfs_disable_last_access_update;

            result = RegQueryValueExW(key, L"NtfsDisableLastAccessUpdate", nullptr, nullptr,
                                      (BYTE *) &ntfs_disable_last_access_update, &length);

            if (result == ERROR_SUCCESS && ntfs_disable_last_access_update == 1) {
                data.use_last_access_time_ = FALSE;
            }

            RegCloseKey(key);
        }

        if (data.use_last_access_time_ == TRUE) {
            gui->show_debug(
                    DebugLevel::Warning, nullptr,
                    L"NtfsDisableLastAccessUpdate is inactive, using LastAccessTime for SpaceHogs.");
        } else {
            gui->show_debug(
                    DebugLevel::Warning, nullptr,
                    L"NtfsDisableLastAccessUpdate is active, ignoring LastAccessTime for SpaceHogs.");
        }
    }

    /* If a Path is specified then call DefragOnePath() for that path. Otherwise call
    DefragMountpoints() for every disk in the system. */
    if (path != nullptr && *path != 0) {
        defrag_one_path(&data, path, optimize_mode);
    } else {
        wchar_t *drives;
        uint32_t drives_size;
        drives_size = GetLogicalDriveStringsW(0, nullptr);

        drives = new wchar_t[drives_size + 1];

        if (drives != nullptr) {
            drives_size = GetLogicalDriveStringsW(drives_size, drives);

            if (drives_size == 0) {
                wchar_t s1[BUFSIZ];
                /* "Could not get list of volumes: %s" */
                system_error_str(GetLastError(), s1, BUFSIZ);

                gui->show_debug(DebugLevel::Warning, nullptr,
                                std::format(L"Could not get list of volumes: {}", s1));
            } else {
                wchar_t *drive;
                drive = drives;

                while (*drive != '\0') {
                    defrag_mountpoints(&data, drive, optimize_mode);
                    while (*drive != '\0') drive++;
                    drive++;
                }
            }

            delete drives;
        }

        gui->clear_screen(L"Finished.");
    }

    // Cleanup
    *data.running_ = RunningState::STOPPED;
}

/*
Stop the defragger. The "Running" variable must be the same as what was given to
the RunJkDefrag() subroutine. Wait for a maximum of time_out milliseconds for the
defragger to stop. If time_out is zero then wait indefinitely. If time_out is
negative then immediately return without waiting.
*/
void DefragLib::stop_jk_defrag(RunningState *run_state, int time_out) {
    // Sanity check
    if (run_state == nullptr) return;

    // All loops in the library check if the Running variable is set to RUNNING. If not then the loop will exit.
    // In effect this will stop the defragger
    if (*run_state == RunningState::RUNNING) {
        *run_state = RunningState::STOPPING;
    }

    // Wait for a maximum of time_out milliseconds for the defragger to stop.
    // If time_out is zero then wait indefinitely. If time_out is negative then immediately return without waiting
    int time_waited = 0;

    while (time_waited <= time_out) {
        if (*run_state == RunningState::STOPPED) break;

        Sleep(100);

        if (time_out > 0) time_waited = time_waited + 100;
    }
}
