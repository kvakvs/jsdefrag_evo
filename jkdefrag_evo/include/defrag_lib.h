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

#include "constants.h"
#include "types.h"
#include "defrag_state.h"

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

    // Example: "c:"; TODO: use std::wstring
    std::unique_ptr<wchar_t[]> mount_point_;
    // Example: "c:\"; TODO: use std::wstring
    std::unique_ptr<wchar_t[]> mount_point_slash_;
    wchar_t volume_name_[52]; // Example: "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}"
    wchar_t volume_name_slash_[52]; // Example: "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\"

    DiskType type_;

    uint64_t mft_locked_clusters_; // Number of clusters at begin of MFT that cannot be moved
};

// List of clusters used by the MFT
struct ExcludesStruct {
    uint64_t start_;
    uint64_t end_;
};

class DefragLib {
public:
    DefragLib();

    ~DefragLib();

    // cppguidelines require these defined or =delete'd
    DefragLib(const DefragLib &other) = default;

    DefragLib(DefragLib &&other) noexcept = default;

    DefragLib &operator=(const DefragLib &other) = default;

    DefragLib &operator=(DefragLib &&other) noexcept = default;

    // Get a non-ownint pointer to unique instance
    static DefragLib *get_instance();

    /* Run the defragger/optimizer.
    
    The parameters:
    
    Path:
        The name of a disk, mountpoint, directory, or file. It may contain
        wildcards '*' and '?'. If Path is empty or nullptr then defrag all the
        mounted, writable, fixed disks on the computer. Some examples:
    
        c:
        c:\xyz
        c:\xyz\*.txt
        \\?\Volume{08439462-3004-11da-bbca-806d6172696f}
    
    Mode:
        0 = Analyze only, do not defragment and do not optimize.
        1 = Analyze and fixup, do not optimize.
        2 = Analyze, fixup, and fast optimization (default).
        3 = Deprecated. Analyze, fixup, and full optimization.
        4 = Analyze and force together.
        5 = Analyze and move to end of disk.
        6 = Analyze and sort files by name.
        7 = Analyze and sort files by size (smallest first).
        8 = Analyze and sort files by last access (newest first).
        9 = Analyze and sort files by last change (oldest first).
        10 = Analyze and sort files by creation time (oldest first).
    
    Speed:
        Percentage 0...100 of the normal speed. The defragger will slow down
        by inserting sleep periods so that the wall time is 100% and the
        actual processing time is this percentage. Specify 100 (or zero) to
        run at maximum speed.
    
    FreeSpace:
        Percentage 0...100 of the total volume space that must be kept
        free after the MFT and directories.
    
    Excludes:
        Array of strings. Each string contains a mask, last string must be
        nullptr. If an item (disk, file, directory) matches one of the strings
        in this array then it will be ignored (skipped). Specify nullptr to
        disable this feature.
    
    SpaceHogs:
        Array of strings. Each string contains a mask, last string must be
        nullptr. If an item (file, directory) matches one of the strings in
        this array then it will be marked as a space hog and moved to the end
        of the disk. A build-in list of spacehogs will be added to this list,
        except if one of the strings in the array is "DisableDefaults".
    
    Running:
        Pointer to an integer. It is used by the stop_jk_defrag() subroutine
        to stop the defragger. If the pointer is nullptr then this feature is
        disabled.
    
    RedrawScreen:
        Pointer to an integer. It can be used by other threads to signal the
        defragger that it must redraw the screen, for example when the window
        is resized. If the pointer is nullptr then this feature is disabled.
    
    ShowStatus:
        Callback subroutine. Is called just before the defragger starts a
        new Phase, and when it finishes a volume. Specify nullptr if the callback
        is not needed.
    
    ShowMove:
        Callback subroutine. Is called whenever an item (file, directory) is
        moved on disk. Specify nullptr if the callback is not needed.
    
    ShowAnalyze:
        Callback subroutine. Is called for every file during analysis.
        This subroutine is called one last time with Item=nullptr when analysis
        has finished. Specify nullptr if the callback is not needed.
    
    ShowDebug:
        Callback subroutine. Is called for every message to show. Specify nullptr
        if the callback is not needed.
    
    DrawCluster:
        Callback subroutine. Is called to paint a fragment on the screen in
        a color. There are 7 colors, see the .h file. Specify nullptr if the
        callback is not needed.
    
    ClearScreen:
        Callback subroutine. Is called when the defragger wants to clear the
        diskmap on the screen. Specify nullptr if the callback is not needed.
    
    DebugMsg:
        Array of textmessages, used when the defragger wants to show a debug
        message. Specify nullptr to use the internal default array of english text
        messages.
    */
    void run_jk_defrag(wchar_t *path, OptimizeMode optimize_mode, int speed, double free_space,
                       const Wstrings &excludes, const Wstrings &space_hogs, RunningState *run_state);

    /*
    
    Stop the defragger. Wait for a maximum of time_out milliseconds for the
    defragger to stop. If time_out is zero then wait indefinitely. If time_out is
    negative then immediately return without waiting.
    Note: The "Running" variable must be the same as what was given to the
    RunJkDefrag() subroutine.
    
    */
    static void stop_jk_defrag(RunningState *run_state, int time_out);

    static const wchar_t *stristr_w(const wchar_t *haystack, const wchar_t *needle);

    [[nodiscard]] static std::wstring system_error_str(DWORD error_code);

    static void show_hex(struct DefragState *data, const BYTE *buffer, uint64_t count);

    static bool match_mask(const wchar_t *string, const wchar_t *mask);

    // static wchar_t** add_array_string(wchar_t** array, const wchar_t* new_string);
    std::wstring get_short_path(const DefragState *data, const ItemStruct *item);

    std::wstring get_long_path(const DefragState *data, const ItemStruct *item);

    static void slow_down(DefragState *data);

    static int get_fragment_count(const ItemStruct *item);

    static bool is_fragmented(const ItemStruct *item, uint64_t offset, uint64_t size);

    void colorize_disk_item(DefragState *data, const ItemStruct *item,
                            uint64_t busy_offset, uint64_t busy_size, int un_draw) const;

    static void call_show_status(DefragState *data, DefragPhase phase, Zone zone);

private:
    static wchar_t lower_case(wchar_t c);

    void append_to_short_path(const ItemStruct *item, std::wstring &path);

    void append_to_long_path(const ItemStruct *item, std::wstring &path);

    static uint64_t find_fragment_begin(const ItemStruct *item, uint64_t lcn);

    [[maybe_unused]] static ItemStruct *find_item_at_lcn(const DefragState *data, uint64_t lcn);

    static HANDLE open_item_handle(const DefragState *data, const ItemStruct *item);

    static bool get_fragments(const DefragState *data, ItemStruct *item, HANDLE file_handle);

    static bool
    find_gap(const DefragState *data, const uint64_t minimum_lcn, uint64_t maximum_lcn,
             const uint64_t minimum_size,
             const int must_fit, const bool find_highest_gap, uint64_t *begin_lcn, uint64_t *end_lcn,
             const bool ignore_mft_excludes);

    static void calculate_zones(DefragState *data);

    DWORD
    move_item_whole(DefragState *data, HANDLE file_handle, const ItemStruct *item, uint64_t new_lcn,
                    const uint64_t offset, const uint64_t size) const;

    DWORD
    move_item_in_fragments(DefragState *data, HANDLE file_handle, const ItemStruct *item, uint64_t new_lcn,
                           const uint64_t offset,
                           const uint64_t size) const;

    bool move_item_with_strat(DefragState *data, ItemStruct *item, HANDLE file_handle, uint64_t new_lcn,
                              uint64_t offset, uint64_t size, MoveStrategy strategy) const;

    int move_item_try_strategies(DefragState *data, ItemStruct *item, HANDLE file_handle, uint64_t new_lcn,
                                 uint64_t offset, uint64_t size, MoveDirection direction) const;

    bool move_item(DefragState *data, ItemStruct *item, uint64_t new_lcn, uint64_t offset,
                   uint64_t size, MoveDirection direction) const;

    static ItemStruct *
    find_highest_item(const DefragState *data, uint64_t cluster_start, uint64_t cluster_end,
                      Tree::Direction direction, Zone zone);

    static ItemStruct *
    find_best_item(const DefragState *data, uint64_t cluster_start, uint64_t cluster_end,
                   Tree::Direction direction, Zone zone);

    [[maybe_unused]] void compare_items(DefragState *data, const ItemStruct *item) const;

    int compare_items(ItemStruct *item_1, ItemStruct *item_2, int sort_field);

    void scan_dir(DefragState *data, const wchar_t *mask, ItemStruct *parent_directory);

    void analyze_volume(DefragState *data);

    void fixup(DefragState *data);

    void defragment(DefragState *data);

    void forced_fill(DefragState *data);

    void vacate(DefragState *data, uint64_t lcn, uint64_t clusters, BOOL ignore_mft_excludes);

    [[maybe_unused]] void move_mft_to_begin_of_disk(DefragState *data);

    void optimize_volume(DefragState *data);

    void optimize_sort(DefragState *data, int sort_field);

    void optimize_up(DefragState *data);

    void defrag_one_path(DefragState *data, const wchar_t *path, OptimizeMode opt_mode);

    void defrag_mountpoints(DefragState *data, const wchar_t *mount_point, OptimizeMode opt_mode);

    // static member that is an instance of itself
    inline static std::unique_ptr<DefragLib> instance_;
};
