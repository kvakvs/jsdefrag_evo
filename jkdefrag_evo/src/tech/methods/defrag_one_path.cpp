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

// Run the defragmenter. Input is the name of a disk, mountpoint, directory, or file,
// and may contain wildcards '*' and '?'
void DefragRunner::defrag_one_path(DefragState &defrag_state, const wchar_t *target_path, OptimizeMode opt_mode) {
    DefragGui *gui = DefragGui::get_instance();

    // Compare the item with the Exclude masks. If a mask matches then return, ignoring the item.
    for (const auto &each_exclude: defrag_state.excludes_) {
        if (Str::match_mask(target_path, each_exclude.c_str())) break;
        if (wcschr(each_exclude.c_str(), L'*') == nullptr
            && each_exclude.length() <= 3
            && std::towlower(target_path[0]) == std::towlower(each_exclude[0])) {
            break;
        }
    }

    // TODO: fix this?
//    if (data.excludes_.size() >= i) {
//        // Show debug message: "Ignoring volume '%s' because of exclude mask '%s'."
//        gui->show_debug(DebugLevel::Fatal, nullptr,
//                        std::format(L"Ignoring volume '{}' because of exclude mask '{}'.", path, data.excludes_[i]));
//        return;
//    }

    // Clear the screen and show "Processing '%s'" message
    gui->clear_screen(std::format(L"Processing {}", target_path));

    try_request_privileges();

    if (!defrag_one_path_mountpoint_setup(defrag_state, target_path)) return;

    defrag_one_path_count_clusters(defrag_state);

    // Determine the number of bytes per cluster.
    // Again I have to do this in a roundabout manner. As far as I know, there is no system call that returns the number
    // of bytes per cluster, so first I have to get the total size of the disk and then divide by the number of
    // clusters.
    uint64_t free_bytes_to_caller;
    uint64_t total_bytes;
    uint64_t free_bytes;
    auto error_code = GetDiskFreeSpaceExW(target_path, (PULARGE_INTEGER) &free_bytes_to_caller,
                                          (PULARGE_INTEGER) &total_bytes,
                                          (PULARGE_INTEGER) &free_bytes);

    if (error_code != 0) defrag_state.bytes_per_cluster_ = total_bytes / defrag_state.total_clusters();

    set_up_unusable_cluster_list(defrag_state);

    defrag_one_path_fixup_input_mask(defrag_state, target_path);

    // Defragment and optimize; Potentially long running, on large volumes
    StopWatch clock_clustermap(L"defrag_one_path: clustermap");
    gui->get_color_map().set_cluster_count(defrag_state.total_clusters());
    gui->show_diskmap(defrag_state);
    clock_clustermap.stop_and_log();

    defrag_one_path_stages(defrag_state, opt_mode);

    call_show_status(defrag_state, DefragPhase::Done, Zone::None); // "Finished."

    // Close the volume handles
    if (defrag_state.disk_.volume_handle_ != nullptr &&
        defrag_state.disk_.volume_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(defrag_state.disk_.volume_handle_);
    }

    // Cleanup
    Tree::delete_tree(defrag_state.item_tree_);

    defrag_state.disk_.mount_point_.clear();
    defrag_state.disk_.mount_point_slash_.clear();
}

void DefragRunner::try_request_privileges() {
    HANDLE process_token_handle;
    LUID take_ownership_value;
    TOKEN_PRIVILEGES token_privileges;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                         &process_token_handle) != 0 &&
        LookupPrivilegeValue(nullptr, SE_BACKUP_NAME, &take_ownership_value) != 0) {
        token_privileges.PrivilegeCount = 1;
        token_privileges.Privileges[0].Luid = take_ownership_value;
        token_privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (AdjustTokenPrivileges(process_token_handle, FALSE, &token_privileges,
                                  sizeof(TOKEN_PRIVILEGES), nullptr, nullptr) == FALSE) {
            return request_privileges_failed();
        }
    } else {
        return request_privileges_failed();
    }
}

void DefragRunner::request_privileges_failed() {
    DefragGui *gui = DefragGui::get_instance();
    gui->message_box_error(L"Info: could not elevate to SeBackupPrivilege.", L"Fatal", 1);
}

// Try finding the MountPoint by treating the input path as a path to something on the disk.
// If this does not succeed, then use the Path as a literal MountPoint name.
bool DefragRunner::defrag_one_path_mountpoint_setup(DefragState &defrag_state, const wchar_t *target_path) {
    DefragGui *gui = DefragGui::get_instance();

    defrag_state.disk_.mount_point_ = target_path;

    // Will write into mount_point_
    auto result = GetVolumePathNameW(target_path, defrag_state.disk_.mount_point_.data(),
                                     (uint32_t) defrag_state.disk_.mount_point_.length() + 1);

    if (result == FALSE) {
        defrag_state.disk_.mount_point_ = target_path;
    }

    // Make two versions of the MountPoint, one with a trailing backslash and one without
    // Kill the trailing backslash
    if (!defrag_state.disk_.mount_point_.empty() && defrag_state.disk_.mount_point_.back() == L'\\') {
        defrag_state.disk_.mount_point_.pop_back();
    }

    defrag_state.disk_.mount_point_slash_ = std::format(L"{}\\", defrag_state.disk_.mount_point_);

    // Determine the name of the volume (something like "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\").
    wchar_t vname[MAX_PATH];
    result = GetVolumeNameForVolumeMountPointW(defrag_state.disk_.mount_point_slash_.c_str(), vname, MAX_PATH);
    defrag_state.disk_.volume_name_slash_ = vname;

    if (result == FALSE) {
        // API puts a limit of 52 on length, but wstring has no length limit
        if (defrag_state.disk_.mount_point_slash_.length() > 52 - 1 - 4) {
            // "Cannot find volume name for mountpoint '%s': %s"
            gui->show_debug(DebugLevel::AlwaysLog, nullptr,
                            std::format(L"Cannot find volume name for mountpoint '{}': reason {}",
                                        defrag_state.disk_.mount_point_slash_, Str::system_error(GetLastError())));

            defrag_state.disk_.mount_point_.clear();
            defrag_state.disk_.mount_point_slash_.clear();

            return false;
        }

        defrag_state.disk_.volume_name_slash_ = std::format(L"\\\\.\\{}", defrag_state.disk_.mount_point_slash_);
    }

    // Make a copy of the VolumeName without the trailing backslash
    defrag_state.disk_.volume_name_ = defrag_state.disk_.volume_name_slash_;

    // Kill the trailing backslash
    if (!defrag_state.disk_.volume_name_.empty() && defrag_state.disk_.volume_name_.back() == L'\\') {
        defrag_state.disk_.volume_name_.pop_back();
    }

    // Exit if the disk is hybernated (if "?/hiberfil.sys" exists and does not begin with 4 zero bytes).
    // length = wcslen(data.disk_.mount_point_slash_.get()) + 14;
    auto hibernation_path = std::format(L"{}\\hiberfil.sys", defrag_state.disk_.mount_point_slash_);

    FILE *fin;
    result = _wfopen_s(&fin, hibernation_path.c_str(), L"rb");

    if (result == 0 && fin != nullptr) {
        DWORD w = 0;

        if (fread(&w, 4, 1, fin) == 1 && w != 0) {
            gui->show_always(L"Will not process this disk, it contains hybernated data.");

            defrag_state.disk_.mount_point_.clear();
            defrag_state.disk_.mount_point_slash_.clear();

            return false;
        }
    }

    // Show a debug message: "Opening volume '%s' at mountpoint '%s'"
    gui->show_debug(DebugLevel::AlwaysLog, nullptr,
                    std::format(L"Opening volume '{}' at mountpoint '{}'", defrag_state.disk_.volume_name_,
                                defrag_state.disk_.mount_point_));

    // Open the VolumeHandle. If error then leave.
    defrag_state.disk_.volume_handle_ = CreateFileW(defrag_state.disk_.volume_name_.c_str(), GENERIC_READ,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                                    OPEN_EXISTING, 0, nullptr);

    if (defrag_state.disk_.volume_handle_ == INVALID_HANDLE_VALUE) {
        const std::wstring &message = std::format(L"Cannot open volume '{}' at mountpoint '{}': reason {}",
                                                  defrag_state.disk_.volume_name_, defrag_state.disk_.mount_point_,
                                                  Str::system_error(GetLastError()));
        gui->message_box_error(message.c_str(), L"Error", std::nullopt);

        defrag_state.disk_.mount_point_.clear();
        defrag_state.disk_.mount_point_slash_.clear();
        return false;
    }
    return true;
}

// Determine the maximum LCN (maximum cluster number). A single call to FSCTL_GET_VOLUME_BITMAP is enough, we don't
// have to walk through the entire bitmap. It's a pity we have to do it in this roundabout manner, because
// there is no system call that reports the total number of clusters in a volume. GetDiskFreeSpace() does,
// but is limited to 2Gb volumes, GetDiskFreeSpaceEx() reports in bytes, not clusters, _getdiskfree()
// requires a drive letter so cannot be used on unmounted volumes or volumes that are mounted on a directory,
// and FSCTL_GET_NTFS_VOLUME_DATA only works for NTFS volumes.
bool DefragRunner::defrag_one_path_count_clusters(DefragState &defrag_state) {
    DefragGui *gui = DefragGui::get_instance();

    STARTING_LCN_INPUT_BUFFER bitmap_param;
    bitmap_param.StartingLcn.QuadPart = 0;
    DWORD w;
    struct {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        BYTE buffer_[8];
    } bitmap_data{};
    DWORD error_code = DeviceIoControl(defrag_state.disk_.volume_handle_, FSCTL_GET_VOLUME_BITMAP,
                                       &bitmap_param, sizeof bitmap_param, &bitmap_data,
                                       sizeof bitmap_data, &w, nullptr);

    if (error_code != 0) {
        error_code = NO_ERROR;
    } else {
        error_code = GetLastError();
    }

    if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) {
        // Show debug message: "Cannot defragment volume '%s' at mountpoint '%s'"
        gui->show_debug(DebugLevel::AlwaysLog, nullptr,
                        std::format(L"Cannot defragment volume '{}' at mountpoint '{}'", defrag_state.disk_.volume_name_,
                                    defrag_state.disk_.mount_point_));

        CloseHandle(defrag_state.disk_.volume_handle_);

        defrag_state.disk_.mount_point_.clear();
        defrag_state.disk_.mount_point_slash_.clear();

        return false;
    }

    defrag_state.set_total_clusters(bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_);
    return true;
}

// Fixup the input mask.
// - If the length is 2 or 3 characters then rewrite into "<DRIVE>:\*".
// - If it does not contain a wildcard then append '*'.
void DefragRunner::defrag_one_path_fixup_input_mask(DefragState &data, const wchar_t *target_path) {
    DefragGui *gui = DefragGui::get_instance();
    const size_t path_len = wcslen(target_path);

    data.include_mask_ = target_path;


    if (path_len == 2 || path_len == 3) {
        data.include_mask_ = std::format(L"{:c}:\\*", std::towlower(target_path[0]));
    } else if (wcschr(target_path, L'*') == nullptr) {
        data.include_mask_ = std::format(L"{}*", target_path);
    }

    gui->show_always(std::format(L"Input mask: {}", data.include_mask_));
}

void DefragRunner::defrag_one_path_stages(DefragState &data, OptimizeMode opt_mode) {
    if (data.is_still_running()) {
        StopWatch clock1(L"defrag_one_path: analyze");
        analyze_volume(data);
    }

    if (data.is_still_running() && opt_mode == OptimizeMode::AnalyzeFixup) {
        StopWatch clock1(L"defrag_one_path: defragment");
        defragment(data);
    }

    if (data.is_still_running()
        && (opt_mode == OptimizeMode::AnalyzeFixupFastopt
            || opt_mode == OptimizeMode::DeprecatedAnalyzeFixupFull)) {
        StopWatch clock1(L"defrag_one_path: Defr+F+Opt+F (defragment)");
        defragment(data);
        clock1.stop_and_log();

        if (data.is_still_running()) {
            StopWatch clock2(L"defrag_one_path: Defr+F+Opt+F (fixup 1)");
            fixup(data);
        }
        if (data.is_still_running()) {
            StopWatch clock3(L"defrag_one_path: Defr+F+Opt+F (optimize)");
            optimize_volume(data);
        }
        if (data.is_still_running()) {
            StopWatch clock4(L"defrag_one_path: Defr+F+Opt+F (fixup 2)");
            fixup(data);
        } // Again, in case of new zone startpoint
    }

    if (data.is_still_running() && opt_mode == OptimizeMode::AnalyzeGroup) {
        StopWatch clock1(L"defrag_one_path: forced_fill");
        forced_fill(data);
    }

    if (data.is_still_running() && opt_mode == OptimizeMode::AnalyzeMoveToEnd) {
        StopWatch clock1(L"defrag_one_path: opt_up");
        optimize_up(data);
    }

    if (data.is_still_running() && opt_mode == OptimizeMode::AnalyzeSortByName) {
        StopWatch clock1(L"defrag_one_path: opt_sort(filename)");
        optimize_sort(data, 0); // Filename
    }

    if (data.is_still_running() && opt_mode == OptimizeMode::AnalyzeSortBySize) {
        StopWatch clock1(L"defrag_one_path: opt_sort(size)");
        optimize_sort(data, 1); // Filesize
    }

    if (data.is_still_running() && opt_mode == OptimizeMode::AnalyzeSortByAccess) {
        StopWatch clock1(L"defrag_one_path: opt_sort(access)");
        optimize_sort(data, 2); // Last access
    }

    if (data.is_still_running() && opt_mode == OptimizeMode::AnalyzeSortByChanged) {
        StopWatch clock1(L"defrag_one_path: opt_sort(last_change)");
        optimize_sort(data, 3); // Last change
    }

    if (data.is_still_running() && opt_mode == OptimizeMode::AnalyzeSortByCreated) {
        StopWatch clock1(L"defrag_one_path: opt_sort(creation)");
        optimize_sort(data, 4); // Creation
    }
    // if ((*Data->Running == RUNNING) && (Mode == 11)) { MoveMftToBeginOfDisk(Data); }
}