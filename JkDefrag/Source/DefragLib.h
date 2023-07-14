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
    struct FragmentListStruct* next_;
};

/* List in memory of all the files on disk, sorted by LCN (Logical Cluster Number). */

struct ItemStruct {
    struct ItemStruct* Parent; /* Parent item. */
    struct ItemStruct* Smaller; /* Next smaller item. */
    struct ItemStruct* Bigger; /* Next bigger item. */

    WCHAR* LongFilename; /* Long filename. */
    WCHAR* LongPath; /* Full path on disk, long filenames. */
    WCHAR* ShortFilename; /* Short filename (8.3 DOS). */
    WCHAR* ShortPath; /* Full path on disk, short filenames. */

    uint64_t Bytes; /* Total number of bytes. */
    uint64_t Clusters; /* Total number of clusters. */
    uint64_t CreationTime; /* 1 second = 10000000 */
    uint64_t MftChangeTime;
    uint64_t LastAccessTime;

    struct FragmentListStruct* Fragments; /* List of fragments. */

    uint64_t ParentInode; /* The Inode number of the parent directory. */

    struct ItemStruct* ParentDirectory;

    bool is_dir_; /* true: it's a directory. */
    bool is_unmovable_; /* true: file can't/couldn't be moved. */
    bool is_excluded_; /* true: file is not to be defragged/optimized. */
    bool is_hog_; /* true: file to be moved to end of disk. */
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

    WCHAR* mount_point_; /* Example: "c:" */
    WCHAR* mount_point_slash_; /* Example: "c:\" */
    WCHAR volume_name_[52]; /* Example: "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}" */
    WCHAR volume_name_slash_[52]; /* Example: "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\" */

    DiskType type_;

    uint64_t mft_locked_clusters_; /* Number of clusters at begin of MFT that cannot be moved. */
};

/* List of clusters used by the MFT. */

struct ExcludesStruct {
    uint64_t Start;
    uint64_t End;
};

/* The big data struct that holds all the defragger's variables for a single thread. */

struct DefragDataStruct {
    int phase_; /* The current Phase (1...3). */
    int zone_; /* The current Zone (0..2) for Phase 3. */
    RunningState* running_; /* If not RUNNING then stop defragging. */
    //	int *RedrawScreen;                     /* 0:no, 1:request, 2: busy. */
    bool use_last_access_time_; /* If TRUE then use LastAccessTime for SpaceHogs. */
    int cannot_move_dirs_; /* If bigger than 20 then do not move dirs. */

    WCHAR* include_mask_; /* Example: "c:\t1\*" */
    struct DiskStruct disk_;

    double free_space_; /* Percentage of total disk size 0..100. */

    /* Tree in memory with information about all the files. */

    struct ItemStruct* item_tree_;
    int balance_count_;
    WCHAR** excludes_; /* Array with exclude masks. */
    bool use_default_space_hogs_; /* TRUE: use the built-in SpaceHogs. */
    WCHAR** space_hogs_; /* Array with SpaceHog masks. */
    uint64_t zones_[4]; /* Begin (LCN) of the zones. */

    struct ExcludesStruct mft_excludes_[3]; /* List of clusters reserved for the MFT. */

    /*
     * Counters filled before Phase 1.
     */

    // Size of the volume, in clusters. 
    uint64_t total_clusters_;
    // Number of bytes per cluster.
    uint64_t bytes_per_cluster_;

    /*
     * Counters updated before/after every Phase.
     */

    uint64_t count_free_clusters_; /* Number of free clusters. */
    uint64_t count_gaps_; /* Number of gaps. */
    uint64_t biggest_gap_; /* Size of biggest gap, in clusters. */
    uint64_t count_gaps_less16_; /* Number of gaps smaller than 16 clusters. */
    uint64_t count_clusters_less16_; /* Number of clusters in gaps that are smaller than 16 clusters. */

    /*
     * Counters updated after every Phase, but not before Phase 1 (analyze).
     */

    uint64_t count_directories_; /* Number of analysed subdirectories. */
    uint64_t count_all_files_; /* Number of analysed files. */
    uint64_t count_fragmented_items_; /* Number of fragmented files. */
    uint64_t count_all_bytes_; /* Bytes in analysed files. */
    uint64_t count_fragmented_bytes_; /* Bytes in fragmented files. */
    uint64_t count_all_clusters_; /* Clusters in analysed files. */
    uint64_t count_fragmented_clusters_; /* Clusters in fragmented files. */
    double average_distance_; /* Between end and begin of files. */

    /* Counters used to calculate the percentage of work done. */

    uint64_t PhaseTodo; /* Number of items to do in this Phase. */
    uint64_t PhaseDone; /* Number of items already done in this Phase. */

    /* Variables used to throttle the speed. */

    int Speed; /* Speed as a percentage 1..100. */
    int64_t StartTime;
    int64_t RunningTime;
    int64_t LastCheckpoint;

    /* The array with error messages. */
    WCHAR** DebugMsg;
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

    static DefragLib* getInstance();

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

    __declspec(dllexport) void run_jk_defrag(WCHAR* path, OptimizeMode optimize_mode, int speed, double free_space,
                                           WCHAR** excludes,
                                           WCHAR** space_hogs, RunningState* run_state,
                                           /*int *RedrawScreen, */WCHAR** debug_msg);

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
    static __declspec(dllexport) WCHAR* stristr_w(WCHAR* haystack, const WCHAR* needle);

    __declspec(dllexport) void system_error_str(uint32_t error_code, WCHAR* out, size_t width) const;
    __declspec(dllexport) void show_hex(struct DefragDataStruct* data, const BYTE* buffer, uint64_t count) const;

    static __declspec(dllexport) bool match_mask(WCHAR* string, WCHAR* mask);

    static __declspec(dllexport) WCHAR** add_array_string(WCHAR** array, const WCHAR* new_string);
    __declspec(dllexport) WCHAR* get_short_path(const struct DefragDataStruct* data, struct ItemStruct* item);
    __declspec(dllexport) WCHAR* get_long_path(const struct DefragDataStruct* data, struct ItemStruct* item);

    static __declspec(dllexport) void slow_down(struct DefragDataStruct* Data);

    static __declspec(dllexport) uint64_t get_item_lcn(const struct ItemStruct* item);

    static __declspec(dllexport) struct ItemStruct* tree_smallest(struct ItemStruct* top);
    __declspec(dllexport) struct ItemStruct* TreeBiggest(struct ItemStruct* Top);
    __declspec(dllexport) struct ItemStruct* TreeFirst(struct ItemStruct* Top, int Direction);
    __declspec(dllexport) struct ItemStruct* TreePrev(struct ItemStruct* Here);
    __declspec(dllexport) struct ItemStruct* TreeNext(struct ItemStruct* Here);
    __declspec(dllexport) struct ItemStruct* TreeNextPrev(struct ItemStruct* Here, int Direction);

    __declspec(dllexport) void TreeInsert(struct DefragDataStruct* Data, struct ItemStruct* New);
    __declspec(dllexport) void TreeDetach(struct DefragDataStruct* Data, struct ItemStruct* Item);
    __declspec(dllexport) void DeleteItemTree(struct ItemStruct* Top);
    __declspec(dllexport) int FragmentCount(struct ItemStruct* Item);
    __declspec(dllexport) bool IsFragmented(struct ItemStruct* Item, uint64_t Offset, uint64_t Size);
    __declspec(dllexport) void ColorizeItem(struct DefragDataStruct* Data, struct ItemStruct* Item, uint64_t BusyOffset,
                                            uint64_t BusySize, int UnDraw);
    /*
    __declspec(dllexport) void               ShowDiskmap(struct DefragDataStruct *Data);
    */
    __declspec(dllexport) void call_show_status(struct DefragDataStruct* Data, int Phase, int Zone);

private:
    static WCHAR lower_case(WCHAR c);

    void append_to_short_path(const struct ItemStruct* item, WCHAR* path, size_t length);
    void append_to_long_path(const struct ItemStruct* item, WCHAR* path, size_t length);
    uint64_t FindFragmentBegin(struct ItemStruct* Item, uint64_t Lcn);

    struct ItemStruct* FindItemAtLcn(struct DefragDataStruct* Data, uint64_t Lcn);
    HANDLE OpenItemHandle(struct DefragDataStruct* Data, struct ItemStruct* Item);
    int GetFragments(struct DefragDataStruct* Data, struct ItemStruct* Item, HANDLE FileHandle);

    bool find_gap(struct DefragDataStruct* data,
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

    void CalculateZones(struct DefragDataStruct* Data);

    uint32_t MoveItem1(struct DefragDataStruct* Data,
                       HANDLE FileHandle,
                       struct ItemStruct* Item,
                       uint64_t NewLcn, /* Where to move to. */
                       uint64_t Offset, /* Number of first cluster to be moved. */
                       uint64_t Size); /* Number of clusters to be moved. */

    uint32_t MoveItem2(struct DefragDataStruct* Data,
                       HANDLE FileHandle,
                       struct ItemStruct* Item,
                       uint64_t NewLcn, /* Where to move to. */
                       uint64_t Offset, /* Number of first cluster to be moved. */
                       uint64_t Size); /* Number of clusters to be moved. */

    int MoveItem3(struct DefragDataStruct* Data,
                  struct ItemStruct* Item,
                  HANDLE FileHandle,
                  uint64_t NewLcn, /* Where to move to. */
                  uint64_t Offset, /* Number of first cluster to be moved. */
                  uint64_t Size, /* Number of clusters to be moved. */
                  int Strategy); /* 0: move in one part, 1: move individual fragments. */

    int MoveItem4(struct DefragDataStruct* Data,
                  struct ItemStruct* Item,
                  HANDLE FileHandle,
                  uint64_t NewLcn, /* Where to move to. */
                  uint64_t Offset, /* Number of first cluster to be moved. */
                  uint64_t Size, /* Number of clusters to be moved. */
                  int Direction); /* 0: move up, 1: move down. */

    int MoveItem(struct DefragDataStruct* Data,
                 struct ItemStruct* Item,
                 uint64_t NewLcn, /* Where to move to. */
                 uint64_t Offset, /* Number of first cluster to be moved. */
                 uint64_t Size, /* Number of clusters to be moved. */
                 int Direction); /* 0: move up, 1: move down. */

    struct ItemStruct* FindHighestItem(const struct DefragDataStruct* data,
                                       uint64_t ClusterStart,
                                       uint64_t ClusterEnd,
                                       int Direction,
                                       int Zone);

    struct ItemStruct* FindBestItem(const struct DefragDataStruct* data,
                                    uint64_t ClusterStart,
                                    uint64_t ClusterEnd,
                                    int Direction,
                                    int Zone);

    void CompareItems(struct DefragDataStruct* Data, struct ItemStruct* Item);

    int CompareItems(struct ItemStruct* Item1, struct ItemStruct* Item2, int SortField);

    void ScanDir(struct DefragDataStruct* Data, WCHAR* Mask, struct ItemStruct* ParentDirectory);

    void AnalyzeVolume(struct DefragDataStruct* Data);

    void fixup(struct DefragDataStruct* data);

    void Defragment(struct DefragDataStruct* Data);

    void ForcedFill(struct DefragDataStruct* Data);

    void Vacate(struct DefragDataStruct* Data, uint64_t Lcn, uint64_t Clusters, BOOL IgnoreMftExcludes);

    void move_mft_to_begin_of_disk(struct DefragDataStruct* data);

    void optimize_volume(struct DefragDataStruct* data);

    void optimize_sort(struct DefragDataStruct* data, int sort_field);

    void optimize_up(struct DefragDataStruct* data);

    void DefragOnePath(struct DefragDataStruct* data, WCHAR* path, OptimizeMode opt_mode);

    void DefragMountpoints(struct DefragDataStruct* Data, WCHAR* MountPoint, OptimizeMode opt_mode);
    /*
        JKScanFat   *m_jkScanFat;
        JKScanNtfs  *m_jkScanNtfs;
    */

    // static member that is an instance of itself
    static DefragLib* instance_;
};
