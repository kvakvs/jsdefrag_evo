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

// Subfunction for DefragAllDisks(). It will ignore removable disks, and
// will iterate for disks that are mounted on a subdirectory of another
// disk (instead of being mounted on a drive).
void DefragRunner::defrag_mountpoints(DefragState &data, const wchar_t *mount_point, const OptimizeMode opt_mode) {
    DefragGui *gui = DefragGui::get_instance();

    if (*data.running_ != RunningState::RUNNING) return;

    // Clear the text messages and show message "Analyzing volume '%s'"
    gui->clear_screen(std::format(L"Analyzing volume '{}'", mount_point));

    // Return if this is not a fixed disk
    if (const int drive_type = GetDriveTypeW(mount_point); drive_type != DRIVE_FIXED) {
        if (drive_type == DRIVE_UNKNOWN) {
            gui->clear_screen(
                    std::format(L"Ignoring volume '{}' because the drive type cannot be determined.", mount_point));
        }

        if (drive_type == DRIVE_NO_ROOT_DIR) {
            gui->clear_screen(std::format(L"Ignoring volume '{}' because there is no volume mounted.", mount_point));
        }

        if (drive_type == DRIVE_REMOVABLE) {
            gui->clear_screen(std::format(L"Ignoring volume '{}' because it has removable media.", mount_point));
        }

        if (drive_type == DRIVE_REMOTE) {
            gui->clear_screen(
                    std::format(L"Ignoring volume '{}' because it is a remote (network) drive.", mount_point));
        }

        if (drive_type == DRIVE_CDROM) {
            gui->clear_screen(std::format(L"Ignoring volume '{}' because it is a CD-ROM drive.", mount_point));
        }

        if (drive_type == DRIVE_RAMDISK) {
            gui->clear_screen(std::format(L"Ignoring volume '{}' because it is a RAM disk.", mount_point));
        }

        return;
    }

    // Determine the name of the volume, something like "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\".
    wchar_t volume_name_slash[BUFSIZ];
    BOOL result = GetVolumeNameForVolumeMountPointW(mount_point, volume_name_slash, BUFSIZ);

    if (result == FALSE) {
        DWORD error_code = GetLastError();

        if (error_code == 3) {
            // "Ignoring volume '%s' because it is not a harddisk."
            gui->show_debug(DebugLevel::AlwaysLog, nullptr,
                            std::format(L"Ignoring volume '{}' because it is not a harddisk.", mount_point));
        } else {
            // "Cannot find volume name for mountpoint: %s"
            gui->show_debug(DebugLevel::AlwaysLog, nullptr,
                            std::format(L"Cannot find volume name for mountpoint '{}': reason {}", mount_point,
                                        Str::system_error(error_code)));
        }
        return;
    }

    // Return if the disk is read-only
    DWORD file_system_flags;
    GetVolumeInformationW(volume_name_slash, nullptr, 0,
                          nullptr, nullptr, &file_system_flags,
                          nullptr, 0);

    if ((file_system_flags & FILE_READ_ONLY_VOLUME) != 0) {
        // Clear the screen and show message "Ignoring disk '%s' because it is read-only."
        gui->clear_screen(std::format(L"Ignoring volume '{}' because it is read-only.", mount_point));

        return;
    }

    /* If the volume is not mounted then leave. Unmounted volumes can be
    defragmented, but the system administrator probably has unmounted
    the volume because he wants it untouched. */
    wchar_t volume_name[BUFSIZ];
    wcscpy_s(volume_name, BUFSIZ, volume_name_slash);

    wchar_t *p1 = wcschr(volume_name, 0);

    if (p1 != volume_name) {
        p1--;
        if (*p1 == '\\') *p1 = 0;
    }

    HANDLE volume_handle = CreateFileW(
            volume_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);

    if (volume_handle == INVALID_HANDLE_VALUE) {
        gui->show_debug(DebugLevel::Warning, nullptr,
                        std::format(L"Cannot open volume '{}' at mountpoint '{}': reason {}",
                                    volume_name, mount_point, Str::system_error(GetLastError())));
        return;
    }

    DWORD w;
    if (DeviceIoControl(volume_handle, FSCTL_IS_VOLUME_MOUNTED, nullptr, 0,
                        nullptr, 0, &w, nullptr) == 0) {
        // Show debug message: "Volume '%s' at mountpoint '%s' is not mounted."
        gui->show_debug(DebugLevel::AlwaysLog, nullptr,
                        std::format(L"Volume '{}' at mountpoint '{}' is not mounted.", volume_name, mount_point));
        CloseHandle(volume_handle);
        return;
    }

    CloseHandle(volume_handle);

    // Defrag the disk.
    auto p_star = std::format(L"{}*", mount_point);
    defrag_one_path(data, p_star.c_str(), opt_mode);

    // According to Microsoft I should check here if the disk has support for reparse points:
    // if ((file_system_flags & FILE_SUPPORTS_REPARSE_POINTS) == 0) return;
    // However, I have found this test will frequently cause a false return on Windows 2000. So I've removed it,
    // everything seems to be working nicely without it.

    // Iterate for all the mountpoints on the disk.
    wchar_t root_path[MAX_PATH + BUFSIZ];
    HANDLE find_mountpoint_handle = FindFirstVolumeMountPointW(volume_name_slash, root_path, MAX_PATH + BUFSIZ);

    if (find_mountpoint_handle == INVALID_HANDLE_VALUE) return;

    do {
        auto full_root = std::format(L"{}{}", mount_point, root_path);
        defrag_mountpoints(data, full_root.c_str(), opt_mode);
    } while (FindNextVolumeMountPointW(find_mountpoint_handle, root_path, MAX_PATH + BUFSIZ) != 0);

    FindVolumeMountPointClose(find_mountpoint_handle);
}
