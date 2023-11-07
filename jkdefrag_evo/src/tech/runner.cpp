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
#include "defrag_state.h"
#include "defrag/volume_bitmap.h"

#undef min

#include <memory>
#include <optional>
#include <algorithm>

DefragRunner::DefragRunner() = default;

DefragRunner::~DefragRunner() = default;

DefragRunner *DefragRunner::get_instance() {
    if (instance_ == nullptr) {
        instance_ = std::make_unique<DefragRunner>();
    }

    return instance_.get();
}

// Search case-insensitive for a substring
const wchar_t *DefragRunner::stristr_w(const wchar_t *haystack, const wchar_t *needle) {
    if (haystack == nullptr || needle == nullptr) return nullptr;

    const wchar_t *p1 = haystack;
    const size_t i = wcslen(needle);

    while (*p1 != 0) {
        if (_wcsnicmp(p1, needle, i) == 0) return p1;

        p1++;
    }

    return nullptr;
}

// Dump a block of data to standard output, for debugging purposes
void DefragRunner::show_hex([[maybe_unused]] DefragState &data, const BYTE *buffer,
                            const uint64_t count) {
    DefragGui *gui = DefragGui::get_instance();

    int j;

    for (int i = 0; i < count; i = i + 16) {
        std::wstring s1;
        s1.reserve(BUFSIZ);
        s1 += std::format(NUM4_FMT L" {:4x}   ", i, i);

        for (j = 0; j < 16; j++) {
            if (j == 8) s1 += L" ";

            if (j + i >= count) {
                s1 += L"   ";
            } else {
                s1 += std::format(L"{:x} ", buffer[i + j]);
            }
        }

        s1 += L" ";

        for (j = 0; j < 16; j++) {
            if (j + i >= count) {
                s1 += L" ";
            } else {
                if (buffer[i + j] < 32) {
                    s1 += L".";
                } else {
                    s1 += std::format(L"{:c}", buffer[i + j]);
                }
            }
        }

        gui->show_debug(DebugLevel::Progress, nullptr, std::move(s1));
    }
}

// Subfunction of GetShortPath()
void DefragRunner::append_to_short_path(const FileNode *item, std::wstring &path) {
    if (item->parent_directory_ != nullptr) append_to_short_path(item->parent_directory_, path);

    path += L"\\";
    path += item->get_short_fn(); // will append short if not empty otherwise will append long
}

// Return a string with the full path of an item, constructed from the short names.
std::wstring DefragRunner::get_short_path(const DefragState &data, const FileNode *item) {
    // Sanity check
    if (item == nullptr) return {};

    // Count the size of all the ShortFilename's
//    size_t length = wcslen(data.disk_.mount_point_.get()) + 1;
//    for (auto temp_item = item; temp_item != nullptr; temp_item = temp_item->parent_directory_) {
//        length = length + wcslen(temp_item->get_short_fn()) + 1;
//    }

    // Append all the strings
    std::wstring path = data.disk_.mount_point_;

    append_to_short_path(item, path);

    return path;
}

// Subfunction of GetLongPath()
void DefragRunner::append_to_long_path(const FileNode *item, std::wstring &path) {
    if (item->parent_directory_ != nullptr) append_to_long_path(item->parent_directory_, path);

    path += L"\\";
    path += item->get_short_fn(); // will append long if not empty otherwise will append short
}

// Return a string with the full path of an item, constructed from the long names
std::wstring DefragRunner::get_long_path(const DefragState &data, const FileNode *item) {
    // Sanity check
    if (item == nullptr) return {};

    // Count the size of all the LongFilename's
//    size_t length = wcslen(data.disk_.mount_point_.get()) + 1;
//
//    for (auto temp_item = item; temp_item != nullptr; temp_item = temp_item->parent_directory_) {
//        length += wcslen(temp_item->get_long_fn()) + 1;
//    }

    // Append all the strings
    std::wstring path = data.disk_.mount_point_;

    append_to_long_path(item, path);

    return path;
}

// Slow the program down
void DefragRunner::slow_down(DefragState &data) {
    // Sanity check
    if (data.speed_ <= 0 || data.speed_ >= 100) return;

    // Calculate the time we have to sleep so that the wall time is 100% and the actual running time is the "-s" parameter percentage
    auto now = Clock::now();

    if (now > data.last_checkpoint_) {
        data.running_time_ += (now - data.last_checkpoint_);
    }

    if (now < data.start_time_) data.start_time_ = now; // Should never happen

    // Sleep
    if (data.running_time_ > Clock::duration::zero()) {
        Clock::duration delay = data.running_time_ * 100UL / data.speed_ - (now - data.start_time_);

        if (delay > std::chrono::milliseconds(200)) delay = std::chrono::milliseconds(200);
        if (delay > Clock::duration::zero()) {
            auto delay_ms = std::chrono::duration_cast<std::chrono::milliseconds>(delay);
            Sleep(delay_ms.count());
        }
    }

    // Save the current wall time, so next time we can calculate the time spent in	the program
    data.last_checkpoint_ = Clock::now();
}

// Open the item as a file or as a directory. If the item could not be opened then show an error message and return nullptr.
HANDLE DefragRunner::open_item_handle(const DefragState &data, const FileNode *item) {
    HANDLE file_handle;
    auto path = std::format(L"\\\\?\\{}", item->get_long_path());

    if (item->is_dir_) {
        file_handle = CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    } else {
        file_handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
    }

    if (file_handle != INVALID_HANDLE_VALUE) return file_handle;

    // Show error message: "Could not open '%s': %s"
    auto error_string = Str::system_error(GetLastError());
    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedFileInfo, nullptr,
                    std::format(L"Could not open '{}': reason {}", item->get_long_path(), error_string));

    return nullptr;
}

/*

Analyze an item (file, directory) and update it's Clusters and Fragments
in memory. If there was an error then return false, otherwise return true.
Note: Very small files are stored by Windows in the MFT and have no
clusters (zero) and no fragments (nullptr).

*/
bool DefragRunner::get_fragments(const DefragState &data, FileNode *item, HANDLE file_handle) {
    STARTING_VCN_INPUT_BUFFER RetrieveParam;

    struct {
        uint32_t extent_count_;
        uint64_t starting_vcn_;

        // TODO: Use std::array or vector, and modify the loading code to allocate only as needed
        FileFragment extents_[1000];
    } extent_data{};

    BY_HANDLE_FILE_INFORMATION file_information;
    FileFragment *last_fragment;
    uint32_t error_code;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    // Initialize. If the item has an old list of fragments then delete it
    item->clusters_count_ = 0;
    item->fragments_.clear();

    // Fetch the date/times of the file
    if (item->creation_time_.count() == 0 &&
        item->last_access_time_.count() == 0 &&
        item->mft_change_time_.count() == 0 &&
        GetFileInformationByHandle(file_handle, &file_information) != 0) {

        item->creation_time_ = from_FILETIME(file_information.ftCreationTime);
        item->last_access_time_ = from_FILETIME(file_information.ftLastAccessTime);
        item->mft_change_time_ = from_FILETIME(file_information.ftLastWriteTime);
    }

    // Show debug message: "Getting cluster bitmap: %s"
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
                                     &RetrieveParam, sizeof RetrieveParam,
                                     &extent_data, sizeof extent_data, &w, nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        } else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) break;

        /* Walk through the clustermap, count the total number of clusters, and
        save all fragments in memory. */
        for (uint32_t i = 0; i < extent_data.extent_count_; i++) {
            // Show debug message
            if (!extent_data.extents_[i].is_virtual()) {
                // "Extent: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u"
                gui->show_debug(
                        DebugLevel::DetailedFileInfo, nullptr,
                        std::format(EXTENT_FMT, extent_data.extents_[i].lcn_, vcn, extent_data.extents_[i].next_vcn_));
            } else {
                // "Extent (virtual): Vcn=%I64u, NextVcn=%I64u"
                gui->show_debug(
                        DebugLevel::DetailedFileInfo, nullptr,
                        std::format(VEXTENT_FMT, vcn, extent_data.extents_[i].next_vcn_));
            }

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */
            if (!extent_data.extents_[i].is_virtual()) {
                item->clusters_count_ = item->clusters_count_ + extent_data.extents_[i].next_vcn_ - vcn;
            }

            // Add the fragment to the Fragments
            FileFragment new_fragment = {
                    .lcn_ = extent_data.extents_[i].lcn_,
                    .next_vcn_ = extent_data.extents_[i].next_vcn_,
            };

            item->fragments_.push_back(new_fragment);

            // The Vcn of the next fragment is the NextVcn field in this record
            vcn = extent_data.extents_[i].next_vcn_;
        }

        // Loop until we have processed the entire clustermap of the file
    } while (error_code == ERROR_MORE_DATA);

    // If there was an error while reading the clustermap then return false
    if (error_code != NO_ERROR && error_code != ERROR_HANDLE_EOF) {
        // Show debug message: "Cannot process clustermap of '%s': %s"
        gui->show_debug(
                DebugLevel::DetailedProgress, nullptr,
                std::format(L"Cannot process clustermap of '{}': {}", item->get_long_path(),
                            Str::system_error(error_code)));

        return false;
    }

    return true;
}

// Return the number of fragments in the item
int DefragRunner::get_fragment_count(const FileNode *item) {
    int fragments = 0;
    uint64_t vcn = 0;
    uint64_t next_lcn = 0;

    for (auto &fragment: item->fragments_) {
        if (!fragment.is_virtual()) {
            if (next_lcn != 0 && fragment.lcn_ != next_lcn) fragments++;

            next_lcn = fragment.lcn_ + fragment.next_vcn_ - vcn;
        }

        vcn = fragment.next_vcn_;
    }

    if (next_lcn != 0) fragments++;

    return fragments;
}

/// Return true if the block in the item starting at Offset with Size clusters is fragmented, otherwise return false.
/// Note: this function does not ask Windows for a fresh list of fragments, it only looks at cached information in memory.
bool DefragRunner::is_fragmented(const FileNode *item, const uint64_t offset, const uint64_t size) {
    // Walk through all fragments. If a fragment is found where either the begin or the end of the fragment is inside
    // the block then the file is fragmented and return true.
    uint64_t fragment_begin = 0;
    uint64_t fragment_end = 0;
    uint64_t vcn = 0;
    uint64_t next_lcn = 0;
    auto fragment = item->fragments_.begin();

    while (fragment != item->fragments_.end()) {
        // Virtual fragments do not occupy space on disk and do not count as fragments
        if (!fragment->is_virtual()) {

            // Treat aligned fragments as a single fragment.
            // Windows will frequently split files in fragments even though they are perfectly aligned on disk,
            // especially system files and very large files. The defragger treats these files as unfragmented.
            if (next_lcn != 0 && fragment->lcn_ != next_lcn) {
                // If the fragment is above the block then return false, the block is not fragmented and we don't
                // have to scan any further.
                if (fragment_begin >= offset + size) return false;

                // If the first cluster of the fragment is above the first cluster of the block,
                // or the last cluster of the fragment is before the last cluster of the block,
                // then the block is fragmented, return true.
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

        // Next fragment
        vcn = fragment->next_vcn_;
        fragment++;
    }

    // Handle the last fragment
    if (fragment_begin >= offset + size) return false;

    if (fragment_begin > offset ||
        (fragment_end - 1 >= offset &&
         fragment_end - 1 < offset + size - 1)) {
        return true;
    }

    // Return false, the item is not fragmented inside the block
    return false;
}

void DefragRunner::colorize_disk_item(DefragState &data, const FileNode *item, const vcn64_t busy_offset,
                                      const cluster_count64_t busy_size, const bool erase_from_screen) const {
    DefragGui *gui = DefragGui::get_instance();

    // Determine if the item is fragmented.
    const bool is_fragmented = this->is_fragmented(item, 0, item->clusters_count_);

    // Walk through all the fragments of the file.
    uint64_t vcn = 0;
    uint64_t real_vcn = 0;

    auto fragment = item->fragments_.begin();

    while (fragment != item->fragments_.end()) {
        // Ignore virtual fragments. They do not occupy space on disk and do not require colorization.
        if (fragment->is_virtual()) {
            vcn = fragment->next_vcn_;
            fragment++;
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
            if (erase_from_screen == false) {
                if (item->is_excluded_ || item->is_unmovable_) color = DrawColor::Unmovable;
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

                for (auto &mft_exclude: data.mft_excludes_) {
                    if (fragment->lcn_ + segment_begin - real_vcn < mft_exclude.begin() &&
                        fragment->lcn_ + segment_end - real_vcn > mft_exclude.begin()) {
                        segment_end = real_vcn + mft_exclude.begin() - fragment->lcn_;
                    }

                    if (fragment->lcn_ + segment_begin - real_vcn >= mft_exclude.begin()
                        && fragment->lcn_ + segment_begin - real_vcn < mft_exclude.end()) {

                        if (fragment->lcn_ + segment_end - real_vcn > mft_exclude.end()) {
                            segment_end = real_vcn + mft_exclude.end() - fragment->lcn_;
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
        fragment++;
    }
}

/// Update some numbers in the DefragState
void DefragRunner::call_show_status(DefragState &defrag_state, const DefragPhase phase, const Zone zone) {
//    VolumeBitmapFragment volume_bitmap;
    DWORD error_code;
    DefragGui *gui = DefragGui::get_instance();

    // Count the number of free gaps on the disk
    defrag_state.count_gaps_ = 0;
    defrag_state.count_free_clusters_ = 0;
    defrag_state.biggest_gap_ = 0;
    defrag_state.count_gaps_less16_ = 0;
    defrag_state.count_clusters_less16_ = 0;

    lcn64_t lcn = 0;
    lcn64_t cluster_start = 0;
    int prev_in_use = 1;
    auto volume_end_lcn = defrag_state.bitmap_.volume_end_lcn();

    do {
        // Fetch a block of cluster data
        error_code = defrag_state.bitmap_.ensure_lcn_loaded(defrag_state.disk_.volume_handle_, lcn);
        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) break;

//        if (volume_bitmap.bitmap_size() / 8 < index_max) {
//            index_max = (int) (volume_bitmap.bitmap_size() / 8);
//        }
        auto next_fragment_lcn = std::min(volume_end_lcn, VolumeBitmap::get_next_fragment_start(lcn));

        while (lcn < next_fragment_lcn) {
            auto in_use = defrag_state.bitmap_.in_use(lcn);

            if (std::any_of(std::begin(defrag_state.mft_excludes_),
                            std::end(defrag_state.mft_excludes_),
                            [=](const lcn_extent_t &ex) { return ex.contains(lcn); })) {
                in_use = true;
            }

            if (prev_in_use == 0 && in_use != 0) {
                defrag_state.count_gaps_ = defrag_state.count_gaps_ + 1;
                defrag_state.count_free_clusters_ = defrag_state.count_free_clusters_ + lcn - cluster_start;
                if (defrag_state.biggest_gap_ < lcn - cluster_start) defrag_state.biggest_gap_ = lcn - cluster_start;

                if (lcn - cluster_start < 16) {
                    defrag_state.count_gaps_less16_ = defrag_state.count_gaps_less16_ + 1;
                    defrag_state.count_clusters_less16_ = defrag_state.count_clusters_less16_ + lcn - cluster_start;
                }
            }

            if (prev_in_use != 0 && in_use == 0) cluster_start = lcn;

            prev_in_use = in_use;
            lcn++;
        }
    } while (lcn < volume_end_lcn);

    if (prev_in_use == 0) {
        defrag_state.count_gaps_ += 1;
        defrag_state.count_free_clusters_ += lcn - cluster_start;

        if (defrag_state.biggest_gap_ < lcn - cluster_start) defrag_state.biggest_gap_ = lcn - cluster_start;

        if (lcn - cluster_start < 16) {
            defrag_state.count_gaps_less16_ += 1;
            defrag_state.count_clusters_less16_ += lcn - cluster_start;
        }
    }

    // Walk through all files and update the counters
    defrag_state.count_directories_ = 0;
    defrag_state.count_all_files_ = 0;
    defrag_state.count_fragmented_items_ = 0;
    defrag_state.count_all_bytes_ = 0;
    defrag_state.count_fragmented_bytes_ = 0;
    defrag_state.count_all_clusters_ = 0;
    defrag_state.count_fragmented_clusters_ = 0;

    for (auto item = Tree::smallest(defrag_state.item_tree_); item != nullptr; item = Tree::next(item)) {
        if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
             _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
            continue;
        }

        defrag_state.count_all_bytes_ += item->bytes_;
        defrag_state.count_all_clusters_ += item->clusters_count_;

        if (item->is_dir_) {
            defrag_state.count_directories_ += 1;
        } else {
            defrag_state.count_all_files_ += 1;
        }

        if (get_fragment_count(item) > 1) {
            defrag_state.count_fragmented_items_ += 1;
            defrag_state.count_fragmented_bytes_ += item->bytes_;
            defrag_state.count_fragmented_clusters_ += item->clusters_count_;
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

    for (auto item = Tree::smallest(defrag_state.item_tree_); item != nullptr; item = Tree::next(item)) {
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

        for (auto item = Tree::smallest(defrag_state.item_tree_); item != nullptr; item = Tree::next(item)) {
            if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
                 _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
                continue;
            }

            if (item->clusters_count_ == 0) continue;

            sum += factor * (item->get_item_lcn() * 2 + item->clusters_count_);
            factor += 2;
        }

        defrag_state.average_distance_ = sum / (double) (count * (count - 1));
    } else {
        defrag_state.average_distance_ = 0;
    }

    defrag_state.phase_ = phase;
    defrag_state.zone_ = zone;
    defrag_state.clusters_done_ = 0;
    defrag_state.phase_todo_ = 0;

    gui->show_status(defrag_state);
}

void DefragRunner::defrag_all_drives_sync(DefragState &data, OptimizeMode mode) {
// Enumerate all drives, and defrag each
    uint32_t drives_size;
    drives_size = GetLogicalDriveStringsW(0, nullptr);

    auto drives = std::make_unique<wchar_t[]>(drives_size + 1);

    drives_size = GetLogicalDriveStringsW(drives_size, drives.get());

    if (drives_size == 0) {
        // "Could not get list of volumes: %s"
        DefragGui *gui = DefragGui::get_instance();
        gui->show_debug(DebugLevel::Warning, nullptr,
                        std::format(L"Could not get list of volumes: {}",
                                    Str::system_error(GetLastError())));
    } else {
        wchar_t *drive = drives.get();

        while (*drive != '\0') {
            // Long running call, in a loop
            DefragRunner::defrag_mountpoints(data, drive, mode);
            while (*drive != '\0') {
                drive++;
            }
            drive++;
        }

    }
}

// Run the defragger/optimizer. See the .h file for a full explanation
void DefragRunner::start_defrag_sync(const wchar_t *path, OptimizeMode optimize_mode, int speed, double free_space,
                                     const Wstrings &excludes, const Wstrings &space_hogs, RunningState *run_state) {
    DefragGui *gui = DefragGui::get_instance();

    gui->log_detailed_progress(L"Defrag starting…");

    // Copy the input values to the data struct
    DefragState data{};
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
        data.add_default_space_hogs();
    }

    // If the NtfsDisableLastAccessUpdate setting in the registry is 1, then disable the LastAccessTime check
    // for the spacehogs.
    data.use_last_access_time_ = true;

    if (data.use_default_space_hogs_) {
        data.check_last_access_enabled();

        if (data.use_last_access_time_) {
            gui->show_debug(
                    DebugLevel::Warning, nullptr,
                    L"NtfsDisableLastAccessUpdate is inactive, using LastAccessTime for SpaceHogs.");
        } else {
            gui->show_debug(
                    DebugLevel::Warning, nullptr,
                    L"NtfsDisableLastAccessUpdate is active, ignoring LastAccessTime for SpaceHogs.");
        }
    }

    // If a Path is specified, then call DefragOnePath() for that path.
    // Otherwise, call DefragMountpoints() for every disk in the system
    if (path != nullptr && *path != 0) {
        // Long-running call
        defrag_one_path(data, path, optimize_mode);
    } else {
        // Long-running call, in a loop
        defrag_all_drives_sync(data, optimize_mode);

        gui->log_detailed_progress(L"Defrag run finished");
    }

    // Cleanup
    *data.running_ = RunningState::STOPPED;
}

// Stop the defragger. The "Running" variable must be the same as what was given to the RunJkDefrag() subroutine. Wait
// for a maximum of time_out milliseconds for the defragger to stop. If time_out is zero then wait indefinitely. If
// time_out is negative, then immediately return without waiting.
void DefragRunner::stop_defrag_sync(RunningState *run_state, SystemClock::duration time_out) {
    // Sanity check
    if (run_state == nullptr) return;

    // All loops in the library check if the Running variable is set to RUNNING. If not then the loop will exit.
    // In effect, this will stop the defragger
    if (*run_state == RunningState::RUNNING) {
        *run_state = RunningState::STOPPING;
    }

    // Wait for a maximum of time_out milliseconds for the defragger to stop.
    // If time_out is zero, then wait indefinitely. If time_out is negative then immediately return without waiting
    SystemClock::duration time_waited{};
    const auto WAIT_MS = 100;

    while (time_waited <= time_out) {
        if (*run_state == RunningState::STOPPED) {
            break;
        }

        Sleep(WAIT_MS);

        if (time_out > SystemClock::duration::zero()) {
            time_waited = time_waited + std::chrono::milliseconds(WAIT_MS);
        }
    }
}

