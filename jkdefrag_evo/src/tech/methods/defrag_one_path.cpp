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
void DefragRunner::defrag_one_path(DefragState &data, const wchar_t *target_path, OptimizeMode opt_mode) {
    struct {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        BYTE buffer_[8];
    } bitmap_data{};

    DefragGui *gui = DefragGui::get_instance();

    // Compare the item with the Exclude masks. If a mask matches then return, ignoring the item.
    for (const auto &each_exclude: data.excludes_) {
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

    // Try finding the MountPoint by treating the input path as a path to something on the disk.
    // If this does not succeed, then use the Path as a literal MountPoint name.
    data.disk_.mount_point_ = target_path;

    // Will write into mount_point_
    auto result = GetVolumePathNameW(target_path, data.disk_.mount_point_.data(),
                                     (uint32_t) data.disk_.mount_point_.length() + 1);

    if (result == FALSE) {
        data.disk_.mount_point_ = target_path;
    }

    // Make two versions of the MountPoint, one with a trailing backslash and one without
    // Kill the trailing backslash
    if (!data.disk_.mount_point_.empty() && data.disk_.mount_point_.back() == L'\\') {
        data.disk_.mount_point_.pop_back();
    }

    data.disk_.mount_point_slash_ = std::format(L"{}\\", data.disk_.mount_point_);

    // Determine the name of the volume (something like "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\").
    wchar_t vname[MAX_PATH];
    result = GetVolumeNameForVolumeMountPointW(data.disk_.mount_point_slash_.c_str(), vname, MAX_PATH);
    data.disk_.volume_name_slash_ = vname;

    if (result == FALSE) {
        // API puts a limit of 52 on length, but wstring has no length limit
        if (data.disk_.mount_point_slash_.length() > 52 - 1 - 4) {
            // "Cannot find volume name for mountpoint '%s': %s"
            gui->show_debug(DebugLevel::AlwaysLog, nullptr,
                            std::format(L"Cannot find volume name for mountpoint '{}': reason {}",
                                        data.disk_.mount_point_slash_, Str::system_error(GetLastError())));

            data.disk_.mount_point_.clear();
            data.disk_.mount_point_slash_.clear();

            return;
        }

        data.disk_.volume_name_slash_ = std::format(L"\\\\.\\{}", data.disk_.mount_point_slash_);
    }

    // Make a copy of the VolumeName without the trailing backslash
    data.disk_.volume_name_ = data.disk_.volume_name_slash_;

    // Kill the trailing backslash
    if (!data.disk_.volume_name_.empty() && data.disk_.volume_name_.back() == L'\\') {
        data.disk_.volume_name_.pop_back();
    }

    // Exit if the disk is hybernated (if "?/hiberfil.sys" exists and does not begin with 4 zero bytes).
    // length = wcslen(data.disk_.mount_point_slash_.get()) + 14;
    auto hibernation_path = std::format(L"{}\\hiberfil.sys", data.disk_.mount_point_slash_);

    FILE *fin;
    result = _wfopen_s(&fin, hibernation_path.c_str(), L"rb");

    if (result == 0 && fin != nullptr) {
        DWORD w = 0;

        if (fread(&w, 4, 1, fin) == 1 && w != 0) {
            gui->show_always(L"Will not process this disk, it contains hybernated data.");

            data.disk_.mount_point_.clear();
            data.disk_.mount_point_slash_.clear();

            return;
        }
    }

    // Show a debug message: "Opening volume '%s' at mountpoint '%s'"
    gui->show_debug(DebugLevel::AlwaysLog, nullptr,
                    std::format(L"Opening volume '{}' at mountpoint '{}'", data.disk_.volume_name_,
                                data.disk_.mount_point_));

    // Open the VolumeHandle. If error then leave.
    data.disk_.volume_handle_ = CreateFileW(data.disk_.volume_name_.c_str(), GENERIC_READ,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                            OPEN_EXISTING, 0, nullptr);

    if (data.disk_.volume_handle_ == INVALID_HANDLE_VALUE) {
        gui->show_debug(DebugLevel::Warning, nullptr,
                        std::format(L"Cannot open volume '{}' at mountpoint '{}': reason {}",
                                    data.disk_.volume_name_, data.disk_.mount_point_,
                                    Str::system_error(GetLastError())));

        data.disk_.mount_point_.clear();
        data.disk_.mount_point_slash_.clear();
        return;
    }

    // Determine the maximum LCN (maximum cluster number). A single call to FSCTL_GET_VOLUME_BITMAP is enough, we don't
    // have to walk through the entire bitmap. It's a pity we have to do it in this roundabout manner, because
    // there is no system call that reports the total number of clusters in a volume. GetDiskFreeSpace() does,
    // but is limited to 2Gb volumes, GetDiskFreeSpaceEx() reports in bytes, not clusters, _getdiskfree()
    // requires a drive letter so cannot be used on unmounted volumes or volumes that are mounted on a directory,
    // and FSCTL_GET_NTFS_VOLUME_DATA only works for NTFS volumes.
    STARTING_LCN_INPUT_BUFFER bitmap_param;
    bitmap_param.StartingLcn.QuadPart = 0;

    DWORD w;
    DWORD error_code = DeviceIoControl(data.disk_.volume_handle_, FSCTL_GET_VOLUME_BITMAP,
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
                        std::format(L"Cannot defragment volume '{}' at mountpoint '{}'", data.disk_.volume_name_,
                                    data.disk_.mount_point_));

        CloseHandle(data.disk_.volume_handle_);

        data.disk_.mount_point_.clear();
        data.disk_.mount_point_slash_.clear();

        return;
    }

    data.total_clusters_ = bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_;

    // Determine the number of bytes per cluster.
    // Again I have to do this in a roundabout manner. As far as I know, there is no system call that returns the number
    // of bytes per cluster, so first I have to get the total size of the disk and then divide by the number of
    // clusters.
    uint64_t free_bytes_to_caller;
    uint64_t total_bytes;
    uint64_t free_bytes;
    error_code = GetDiskFreeSpaceExW(target_path, (PULARGE_INTEGER) &free_bytes_to_caller,
                                     (PULARGE_INTEGER) &total_bytes,
                                     (PULARGE_INTEGER) &free_bytes);

    if (error_code != 0) data.bytes_per_cluster_ = total_bytes / data.total_clusters_;

    set_up_unusable_cluster_list(data);

    // Fixup the input mask.
    // - If the length is 2 or 3 characters then rewrite into "<DRIVE>:\*".
    // - If it does not contain a wildcard then append '*'.
    data.include_mask_ = target_path;

    const size_t path_len = wcslen(target_path);

    if (path_len == 2 || path_len == 3) {
        data.include_mask_ = std::format(L"{:c}:\\*", std::towlower(target_path[0]));
    } else if (wcschr(target_path, L'*') == nullptr) {
        data.include_mask_ = std::format(L"{}*", target_path);
    }

    gui->show_always(std::format(L"Input mask: {}", data.include_mask_));

    // Defragment and optimize; Potentially long running, on large volumes
    gui->get_color_map().set_cluster_count(data.total_clusters_);
    gui->show_diskmap(data);

    if (*data.running_ == RunningState::RUNNING) {
        StopWatch clock1(L"defrag_one_path: analyze");
        analyze_volume(data);
    }

    if (*data.running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeFixup) {
        StopWatch clock1(L"defrag_one_path: defragment");
        defragment(data);
    }

    if (*data.running_ == RunningState::RUNNING
        && (opt_mode == OptimizeMode::AnalyzeFixupFastopt
            || opt_mode == OptimizeMode::DeprecatedAnalyzeFixupFull)) {
        StopWatch clock1(L"defrag_one_path: Defr+F+Opt+F (defragment)");
        defragment(data);
        clock1.stop_and_log();

        if (*data.running_ == RunningState::RUNNING) {
            StopWatch clock2(L"defrag_one_path: Defr+F+Opt+F (fixup 1)");
            fixup(data);
        }
        if (*data.running_ == RunningState::RUNNING) {
            StopWatch clock3(L"defrag_one_path: Defr+F+Opt+F (optimize)");
            optimize_volume(data);
        }
        if (*data.running_ == RunningState::RUNNING) {
            StopWatch clock4(L"defrag_one_path: Defr+F+Opt+F (fixup 2)");
            fixup(data);
        } // Again, in case of new zone startpoint
    }

    if (*data.running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeGroup) {
        StopWatch clock1(L"defrag_one_path: forced_fill");
        forced_fill(data);
    }

    if (*data.running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeMoveToEnd) {
        StopWatch clock1(L"defrag_one_path: opt_up");
        optimize_up(data);
    }

    if (*data.running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortByName) {
        StopWatch clock1(L"defrag_one_path: opt_sort(filename)");
        optimize_sort(data, 0); // Filename
    }

    if (*data.running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortBySize) {
        StopWatch clock1(L"defrag_one_path: opt_sort(size)");
        optimize_sort(data, 1); // Filesize
    }

    if (*data.running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortByAccess) {
        StopWatch clock1(L"defrag_one_path: opt_sort(access)");
        optimize_sort(data, 2); // Last access
    }

    if (*data.running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortByChanged) {
        StopWatch clock1(L"defrag_one_path: opt_sort(last_change)");
        optimize_sort(data, 3); // Last change
    }

    if (*data.running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortByCreated) {
        StopWatch clock1(L"defrag_one_path: opt_sort(creation)");
        optimize_sort(data, 4); // Creation
    }
    // if ((*Data->Running == RUNNING) && (Mode == 11)) { MoveMftToBeginOfDisk(Data); }

    call_show_status(data, DefragPhase::Done, Zone::None); // "Finished."

    // Close the volume handles
    if (data.disk_.volume_handle_ != nullptr &&
        data.disk_.volume_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(data.disk_.volume_handle_);
    }

    // Cleanup
    Tree::delete_tree(data.item_tree_);

    data.disk_.mount_point_.clear();
    data.disk_.mount_point_slash_.clear();
}

// Try to change our permissions, so we can access special files and directories
// such as "C:\System Volume Information". If this does not succeed then quietly
// continue, we'll just have to do with whatever permissions we have.
// SE_BACKUP_NAME = Backup and Restore Privileges.
void DefragRunner::try_request_privileges() {
    HANDLE process_token_handle;
    LUID take_ownership_value;
    TOKEN_PRIVILEGES token_privileges;
    DefragGui *gui = DefragGui::get_instance();

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                         &process_token_handle) != 0 &&
        LookupPrivilegeValue(nullptr, SE_BACKUP_NAME, &take_ownership_value) != 0) {
        token_privileges.PrivilegeCount = 1;
        token_privileges.Privileges[0].Luid = take_ownership_value;
        token_privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (AdjustTokenPrivileges(process_token_handle, FALSE, &token_privileges,
                                  sizeof(TOKEN_PRIVILEGES), nullptr, nullptr) == FALSE) {
            gui->show_debug(DebugLevel::DetailedProgress, nullptr, L"Info: could not elevate to SeBackupPrivilege.");
        }
    } else {
        gui->show_debug(DebugLevel::DetailedProgress, nullptr, L"Info: could not elevate to SeBackupPrivilege.");
    }
}
