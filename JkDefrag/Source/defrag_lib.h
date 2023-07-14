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


/* Include guard */

#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <tchar.h>

#include "types.h"

constexpr uint64_t VIRTUALFRAGMENT = 18446744073709551615UL; /* _UI64_MAX - 1 */

// The three running states.
enum class RunningState {
    RUNNING = 0,
    STOPPING = 1,
    STOPPED = 2
};

struct OptimizeMode {
    enum OptimizeModeEnum {
        // Analyze only, do not defragment and do not optimize.
        AnalyzeOnly = 0,
        // Analyze and fixup, do not optimize.
        AnalyzeFixup = 1,
        // Analyze, fixup, and fast optimization(default).
        AnalyzeFixupFastopt = 2,
        // Deprecated.Analyze, fixup, and full optimization.
        DeprecatedAnalyzeFixupFull = 3,
        // Analyze and force together.
        AnalyzeGroup = 4,
        // Analyze and move to end of disk.
        AnalyzeMoveToEnd = 5,
        // Analyze and sort files by name.
        AnalyzeSortByName = 6,
        // Analyze and sort files by size(smallest first).
        AnalyzeSortBySize = 7,
        // Analyze and sort files by last access(newest first).
        AnalyzeSortByAccess = 8,
        // Analyze and sort files by last change(oldest first).
        AnalyzeSortByChanged = 9,
        // Analyze and sort files by creation time(oldest first).
        AnalyzeSortByCreated = 10,
        // 11 (unused?) - move to beginning of disk
        Max
    };

    OptimizeModeEnum mode_;
};

/* List in memory of the fragments of a file. */

struct FragmentListStruct {
    uint64_t lcn_; /* Logical cluster number, location on disk. */
    uint64_t next_vcn_; /* Virtual cluster number of next fragment. */
    FragmentListStruct* next_;
};

/**
 * \brief List in memory of all the files on disk, sorted by LCN (Logical Cluster Number)
 */
struct ItemStruct {
    ItemStruct* parent_;
    // Next smaller item
    ItemStruct* smaller_;
    // Next bigger item
    ItemStruct* bigger_;

    wchar_t* long_filename_;
    // Full path on disk, long filenames.
    wchar_t* long_path_;
    // Short filename(8.3 DOS)
    wchar_t* short_filename_;
    // Full path on disk, short filenames
    wchar_t* short_path_;

    uint64_t bytes_;
    uint64_t clusters_count_;
    // Unit: Microseconds
    uint64_t creation_time_;
    uint64_t mft_change_time_;
    uint64_t last_access_time_;

    // List of fragments
    FragmentListStruct* fragments_;

    // The Inode number of the parent directory
    uint64_t parent_inode_;

    ItemStruct* parent_directory_;

    bool is_dir_;
    bool is_unmovable_;
    bool is_excluded_;
    // file to be moved to the end of disk
    bool is_hog_;
};

enum class DiskType {
    UnknownType = 0,
    NTFS = 1,
    FAT12 = 12,
    FAT16 = 16,
    FAT32 = 32
};

/* Information about a disk volume. */

struct DiskStruct {
    HANDLE volume_handle_;

    wchar_t* mount_point_; /* Example: "c:" */
    wchar_t* mount_point_slash_; /* Example: "c:\" */
    wchar_t volume_name_[52]; /* Example: "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}" */
    wchar_t volume_name_slash_[52]; /* Example: "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\" */

    DiskType type_;

    uint64_t mft_locked_clusters_; /* Number of clusters at begin of MFT that cannot be moved. */
};

/* List of clusters used by the MFT. */

struct ExcludesStruct {
    uint64_t start_;
    uint64_t end_;
};

class DefragLib {
public:
    DefragLib();
    ~DefragLib();

    // cppguidelines require these defined or =delete'd
    DefragLib(const DefragLib& other) = default;
    DefragLib(DefragLib&& other) noexcept = default;
    DefragLib& operator=(const DefragLib& other) = default;
    DefragLib& operator=(DefragLib&& other) noexcept = default;

    // Get a non-ownint pointer to unique instance
    static DefragLib* get_instance();

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
        Pointer to an integer. It is used by the StopJkDefrag() subroutine
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

    __declspec(dllexport) void run_jk_defrag(wchar_t* path, OptimizeMode optimize_mode, int speed, double free_space,
                                             const Wstrings& excludes, const Wstrings& space_hogs, RunningState* run_state,
                                             std::optional<Wstrings> debug_msg);

    /*
    
    Stop the defragger. Wait for a maximum of TimeOut milliseconds for the
    defragger to stop. If TimeOut is zero then wait indefinitely. If TimeOut is
    negative then immediately return without waiting.
    Note: The "Running" variable must be the same as what was given to the
    RunJkDefrag() subroutine.
    
    */
    __declspec(dllexport) void StopJkDefrag(RunningState* run_state, int TimeOut);

    /* Other exported functions that might be useful in programs that use JkDefrag. */

    static __declspec(dllexport) char* stristr(char* haystack, const char* needle);
    static __declspec(dllexport) wchar_t* stristr_w(wchar_t* haystack, const wchar_t* needle);

    __declspec(dllexport) void system_error_str(uint32_t error_code, wchar_t* out, size_t width) const;
    __declspec(dllexport) void show_hex(struct DefragDataStruct* data, const BYTE* buffer, uint64_t count) const;

    static __declspec(dllexport) bool match_mask(const wchar_t* string, const wchar_t* mask);

    // static __declspec(dllexport) wchar_t** add_array_string(wchar_t** array, const wchar_t* new_string);
    __declspec(dllexport) wchar_t* get_short_path(const DefragDataStruct* data, const ItemStruct* item);
    __declspec(dllexport) wchar_t* get_long_path(const DefragDataStruct* data, const ItemStruct* item);

    static __declspec(dllexport) void slow_down(DefragDataStruct* data);

    static __declspec(dllexport) uint64_t get_item_lcn(const ItemStruct* item);

    static __declspec(dllexport) ItemStruct* tree_smallest(ItemStruct* top);
    static __declspec(dllexport) ItemStruct* tree_biggest(ItemStruct* top);
    static __declspec(dllexport) ItemStruct* tree_first(ItemStruct* top, int direction);
    static __declspec(dllexport) ItemStruct* tree_prev(ItemStruct* here);
    static __declspec(dllexport) ItemStruct* tree_next(ItemStruct* here);
    static __declspec(dllexport) ItemStruct* tree_next_prev(ItemStruct* here, const bool reverse);

    static __declspec(dllexport) void tree_insert(DefragDataStruct* data, ItemStruct* new_item);
    static __declspec(dllexport) void tree_detach(DefragDataStruct* data, const ItemStruct* item);
    static __declspec(dllexport) void delete_item_tree(ItemStruct* top);
    static __declspec(dllexport) int get_fragment_count(const ItemStruct* item);
    static __declspec(dllexport) bool is_fragmented(const ItemStruct* item, uint64_t offset, uint64_t size);
    __declspec(dllexport) void colorize_item(DefragDataStruct* data, const ItemStruct* item,
                                             uint64_t busy_offset, uint64_t busy_size, int un_draw) const;
    static __declspec(dllexport) void call_show_status(DefragDataStruct* data, int phase, int zone);

private:
    static wchar_t lower_case(wchar_t c);

    void append_to_short_path(const ItemStruct* item, wchar_t* path, size_t length);
    void append_to_long_path(const ItemStruct* item, wchar_t* path, size_t length);
    static uint64_t find_fragment_begin(const ItemStruct* item, uint64_t lcn);

    static ItemStruct* find_item_at_lcn(const DefragDataStruct* data, uint64_t lcn);
    HANDLE open_item_handle(const DefragDataStruct* data, const ItemStruct* item) const;
    int get_fragments(const DefragDataStruct* data, ItemStruct* item, HANDLE file_handle) const;

    bool find_gap(const DefragDataStruct* data,
                  uint64_t minimum_lcn,
                  /* Gap must be at or above this LCN. */
                  uint64_t maximum_lcn,
                  /* Gap must be below this LCN. */
                  uint64_t minimum_size,
                  /* Gap must be at least this big. */
                  int must_fit,
                  /* true: gap must be at least MinimumSize. */
                  bool find_highest_gap,
                  /* true: return the last gap that fits. */
                  uint64_t* begin_lcn,
                  /* Result, LCN of begin of cluster. */
                  uint64_t* end_lcn,
                  /* Result, LCN of end of cluster. */
                  BOOL ignore_mft_excludes) const;

    static void calculate_zones(DefragDataStruct* data);

    uint32_t move_item1(DefragDataStruct* data,
                        HANDLE file_handle,
                        const ItemStruct* item,
                        uint64_t new_lcn, /* Where to move to. */
                        uint64_t offset, /* Number of first cluster to be moved. */
                        uint64_t size) const; /* Number of clusters to be moved. */

    uint32_t move_item2(DefragDataStruct* data,
                        HANDLE file_handle,
                        const ItemStruct* item,
                        uint64_t new_lcn, /* Where to move to. */
                        uint64_t offset, /* Number of first cluster to be moved. */
                        uint64_t size) const; /* Number of clusters to be moved. */

    int move_item3(DefragDataStruct* data,
                   ItemStruct* item,
                   HANDLE file_handle,
                   uint64_t new_lcn, /* Where to move to. */
                   uint64_t offset, /* Number of first cluster to be moved. */
                   uint64_t size, /* Number of clusters to be moved. */
                   int strategy) const; /* 0: move in one part, 1: move individual fragments. */

    int move_item4(DefragDataStruct* data,
                   ItemStruct* item,
                   HANDLE file_handle,
                   uint64_t new_lcn, /* Where to move to. */
                   uint64_t offset, /* Number of first cluster to be moved. */
                   uint64_t size, /* Number of clusters to be moved. */
                   int direction) const; /* 0: move up, 1: move down. */

    int move_item(DefragDataStruct* data,
                  ItemStruct* item,
                  uint64_t new_lcn, /* Where to move to. */
                  uint64_t offset, /* Number of first cluster to be moved. */
                  uint64_t size, /* Number of clusters to be moved. */
                  int direction) const; /* 0: move up, 1: move down. */

    static ItemStruct* find_highest_item(const DefragDataStruct* data,
                                         uint64_t cluster_start,
                                         uint64_t cluster_end,
                                         int direction,
                                         int zone);

    static ItemStruct* find_best_item(const DefragDataStruct* data,
                                      uint64_t cluster_start,
                                      uint64_t cluster_end,
                                      int direction,
                                      int zone);

    void compare_items(DefragDataStruct* data, const ItemStruct* item) const;

    int compare_items(ItemStruct* Item1, ItemStruct* Item2, int SortField);

    void scan_dir(DefragDataStruct* Data, wchar_t* Mask, ItemStruct* ParentDirectory);

    void analyze_volume(DefragDataStruct* data);

    void fixup(DefragDataStruct* data);

    void defragment(DefragDataStruct* Data);

    void forced_fill(DefragDataStruct* Data);

    void vacate(DefragDataStruct* Data, uint64_t Lcn, uint64_t Clusters, BOOL IgnoreMftExcludes);

    void move_mft_to_begin_of_disk(DefragDataStruct* data);

    void optimize_volume(DefragDataStruct* data);

    void optimize_sort(DefragDataStruct* data, int sort_field);

    void optimize_up(DefragDataStruct* data);

    void defrag_one_path(DefragDataStruct* data, const wchar_t* path, OptimizeMode opt_mode);

    void defrag_mountpoints(DefragDataStruct* data, const wchar_t* mount_point, OptimizeMode opt_mode);

    // static member that is an instance of itself
    inline static std::unique_ptr<DefragLib> instance_;
};
