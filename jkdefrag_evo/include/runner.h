/*

The JkDefragDll library.

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


// Include guard

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <tchar.h>

#include "tree.h"
#include "constants.h"
#include "types.h"
#include "defrag_state.h"
#include "file_node.h"

// The three running states.
enum class RunningState {
    RUNNING = 0,
    STOPPING = 1,
    STOPPED = 2
};

enum class DiskType {
    UnknownType = 0,
    NTFS = 1,
    FAT12 = 12,
    FAT16 = 16,
    FAT32 = 32
};

// Information about a disk volume

struct DiskStruct {
    HANDLE volume_handle_;

    // Example: "c:"
    std::wstring mount_point_;
    // Example: "c:\"
    std::wstring mount_point_slash_;
    std::wstring volume_name_; // Example: "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}"
    std::wstring volume_name_slash_; // Example: "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\"

    DiskType type_;

    uint64_t mft_locked_clusters_; // Number of clusters at begin of MFT that cannot be moved
};

// List of clusters used by the MFT
struct ExcludesStruct {
    uint64_t start_;
    uint64_t end_;
};

class DefragRunner {
public:
    DefragRunner();

    ~DefragRunner();

    // cppguidelines require these defined or =delete'd
    DefragRunner(const DefragRunner &other) = default;

    DefragRunner(DefragRunner &&other) noexcept = default;

    DefragRunner &operator=(const DefragRunner &other) = default;

    DefragRunner &operator=(DefragRunner &&other) noexcept = default;

    // Get a non-ownint pointer to unique instance
    static DefragRunner *get_instance();

    /// \brief Run the defragger/optimizer.
    /// \param path The name of a disk, mountpoint, directory, or file. It may contain wildcards '*' and '?'. If
    ///     Path is empty or nullptr then defrag all the mounted, writable, fixed disks on the computer. Some examples:
    ///     c:   c:\xyz   c:\xyz\*.txt   \\?\Volume{08439462-3004-11da-bbca-806d6172696f}
    /// \param free_space Percentage 0...100 of the total volume space that must be kept free after the MFT and directories.
    /// \param excludes Array of strings. Each string contains a mask, last string must be nullptr. If an item (disk,
    ///     file, directory) matches one of the strings in this array then it will be ignored (skipped).
    /// \param space_hogs Array of strings. Each string contains a mask, last string must be nullptr. If an item (file,
    ///     directory) matches one of the strings in this array then it will be marked as a space hog and moved to the
    ///     end of the disk. A build-in list of spacehogs will be added to this list, except if one of the strings in
    ///     the array is "DisableDefaults".
    /// \param run_state It is used by the stop_defrag() subroutine to stop_and_log the defragger. If the pointer is nullptr
    ///     then this feature is disabled.
    void start_defrag_sync(const wchar_t *path, OptimizeMode optimize_mode, int speed, double free_space,
                           const Wstrings &excludes, const Wstrings &space_hogs, RunningState *run_state);

    // Stop the defragger. Wait for a maximum of time_out milliseconds for the defragger to stop. If time_out is zero
    // then wait indefinitely. If time_out is negative then immediately return without waiting.
    // Note: The "Running" variable must be the same as what was given to the start_defrag_sync() subroutine.
    static void stop_defrag_sync(RunningState *run_state, SystemClock::duration time_out);

    static const wchar_t *stristr_w(const wchar_t *haystack, const wchar_t *needle);

    static void show_hex(struct DefragState &data, const BYTE *buffer, uint64_t count);

    // static wchar_t** add_array_string(wchar_t** array, const wchar_t* new_string);
    std::wstring get_short_path(const DefragState &data, const FileNode *item);

    std::wstring get_long_path(const DefragState &data, const FileNode *item);

    static void slow_down(DefragState &data);

    static int get_fragment_count(const FileNode *item);

    static bool is_fragmented(const FileNode *item, uint64_t offset, uint64_t size);

    void colorize_disk_item(DefragState &data, const FileNode *item,
                            uint64_t busy_offset, uint64_t busy_size, int un_draw) const;

    static void call_show_status(DefragState &data, DefragPhase phase, Zone zone);

private:
    static void try_request_privileges();

    bool defrag_one_path_mountpoint_setup(DefragState &data, const wchar_t *target_path);

    bool defrag_one_path_count_clusters(DefragState &data);

    void defrag_one_path_fixup_input_mask(DefragState &data, const wchar_t *target_path);

    void defrag_all_drives_sync(DefragState &data, OptimizeMode mode);

    void append_to_short_path(const FileNode *item, std::wstring &path);

    void append_to_long_path(const FileNode *item, std::wstring &path);

    static uint64_t find_fragment_begin(const FileNode *item, uint64_t lcn);

    [[maybe_unused]] static FileNode *find_item_at_lcn(const DefragState &data, uint64_t lcn);

    static HANDLE open_item_handle(const DefragState &data, const FileNode *item);

    static bool get_fragments(const DefragState &data, FileNode *item, HANDLE file_handle);

    static bool
    find_gap(const DefragState &data, const uint64_t minimum_lcn, uint64_t maximum_lcn,
             const uint64_t minimum_size,
             const int must_fit, const bool find_highest_gap, uint64_t *begin_lcn, uint64_t *end_lcn,
             const bool ignore_mft_excludes);

    static void calculate_zones(DefragState &data);

    DWORD
    move_item_whole(DefragState &data, HANDLE file_handle, const FileNode *item, uint64_t new_lcn,
                    const uint64_t offset, const uint64_t size) const;

    DWORD
    move_item_in_fragments(DefragState &data, HANDLE file_handle, const FileNode *item, uint64_t new_lcn,
                           const uint64_t offset,
                           const uint64_t size) const;

    bool move_item_with_strat(DefragState &data, FileNode *item, HANDLE file_handle, uint64_t new_lcn,
                              uint64_t offset, uint64_t size, MoveStrategy strategy) const;

    int move_item_try_strategies(DefragState &data, FileNode *item, HANDLE file_handle, uint64_t new_lcn,
                                 uint64_t offset, uint64_t size, MoveDirection direction) const;

    bool move_item(DefragState &data, FileNode *item, uint64_t new_lcn, uint64_t offset,
                   uint64_t size, MoveDirection direction) const;

    static FileNode *
    find_highest_item(const DefragState &data, uint64_t cluster_start, uint64_t cluster_end,
                      Tree::Direction direction, Zone zone);

    static FileNode *
    find_best_item(const DefragState &data, uint64_t cluster_start, uint64_t cluster_end,
                   Tree::Direction direction, Zone zone);

    [[maybe_unused]] void compare_items(DefragState &data, const FileNode *item) const;

    int compare_items(FileNode *item_1, FileNode *item_2, int sort_field);

    void scan_dir(DefragState &data, const wchar_t *mask, FileNode *parent_directory);

    void analyze_volume(DefragState &data);

    void analyze_volume_read_fs(DefragState &data);

    void fixup(DefragState &data);

    void defragment(DefragState &data);

    void forced_fill(DefragState &data);

    void vacate(DefragState &data, uint64_t lcn, uint64_t clusters, BOOL ignore_mft_excludes);

    [[maybe_unused]] void move_mft_to_begin_of_disk(DefragState &data);

    void optimize_volume(DefragState &data);

    void optimize_sort(DefragState &data, int sort_field);

    void optimize_up(DefragState &data);

    void defrag_one_path(DefragState &data, const wchar_t *target_path, OptimizeMode opt_mode);

    void defrag_mountpoints(DefragState &data, const wchar_t *mount_point, OptimizeMode opt_mode);

    static void set_up_unusable_cluster_list(DefragState &data);

private:
    // static member that is an instance of itself
    inline static std::unique_ptr<DefragRunner> instance_;
};
