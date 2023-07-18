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

DefragState::DefragState() {
    last_checkpoint_ = start_time_ = Clock::now();
}

void DefragState::add_default_space_hogs() {
    space_hogs_.emplace_back(L"?:\\$RECYCLE.BIN\\*"); // Vista
    space_hogs_.emplace_back(L"?:\\RECYCLED\\*"); // FAT on 2K/XP
    space_hogs_.emplace_back(L"?:\\RECYCLER\\*"); // NTFS on 2K/XP
    space_hogs_.emplace_back(L"?:\\WINDOWS\\$*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\Downloaded Installations\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\Ehome\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\Fonts\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\Help\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\I386\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\IME\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\Installer\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\ServicePackFiles\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\SoftwareDistribution\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\Speech\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\Symbols\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\ie7updates\\*");
    space_hogs_.emplace_back(L"?:\\WINDOWS\\system32\\dllcache\\*");
    space_hogs_.emplace_back(L"?:\\WINNT\\$*");
    space_hogs_.emplace_back(L"?:\\WINNT\\Downloaded Installations\\*");
    space_hogs_.emplace_back(L"?:\\WINNT\\I386\\*");
    space_hogs_.emplace_back(L"?:\\WINNT\\Installer\\*");
    space_hogs_.emplace_back(L"?:\\WINNT\\ServicePackFiles\\*");
    space_hogs_.emplace_back(L"?:\\WINNT\\SoftwareDistribution\\*");
    space_hogs_.emplace_back(L"?:\\WINNT\\ie7updates\\*");
    space_hogs_.emplace_back(L"?:\\*\\Installshield Installation Information\\*");
    space_hogs_.emplace_back(L"?:\\I386\\*");
    space_hogs_.emplace_back(L"?:\\System Volume Information\\*");
    space_hogs_.emplace_back(L"?:\\windows.old\\*");

    space_hogs_.emplace_back(L"*.bak");
    space_hogs_.emplace_back(L"*.bup"); // DVD
    space_hogs_.emplace_back(L"*.chm"); // Help files
    space_hogs_.emplace_back(L"*.dvr-ms");
    space_hogs_.emplace_back(L"*.ifo"); // DVD
    space_hogs_.emplace_back(L"*.iso"); // ISO CD disk image
    space_hogs_.emplace_back(L"*.lzh");
    space_hogs_.emplace_back(L"*.msi");
    space_hogs_.emplace_back(L"*.pdf");

    // Archives
    space_hogs_.emplace_back(L"*.7z");
    space_hogs_.emplace_back(L"*.arj");
    space_hogs_.emplace_back(L"*.bz2");
    space_hogs_.emplace_back(L"*.gz");
    space_hogs_.emplace_back(L"*.z");
    space_hogs_.emplace_back(L"*.zip");
    space_hogs_.emplace_back(L"*.cab");
    space_hogs_.emplace_back(L"*.rar");
    space_hogs_.emplace_back(L"*.rpm");
    space_hogs_.emplace_back(L"*.deb");
    space_hogs_.emplace_back(L"*.tar");

    // Video and sound media
    space_hogs_.emplace_back(L"*.avi");
    space_hogs_.emplace_back(L"*.mpg"); // MPEG2
    space_hogs_.emplace_back(L"*.mp3"); // MPEG3 sound
    space_hogs_.emplace_back(L"*.mp4"); // MPEG4 video
    space_hogs_.emplace_back(L"*.ogg"); // Ogg Vorbis sound
    space_hogs_.emplace_back(L"*.wmv"); // Windows media video
    space_hogs_.emplace_back(L"*.vob"); // DVD
    space_hogs_.emplace_back(L"*.ogg"); // Ogg Vorbis Video

    // Images
    space_hogs_.emplace_back(L"*.jpg");
    space_hogs_.emplace_back(L"*.bmp");
    space_hogs_.emplace_back(L"*.jpeg");
    space_hogs_.emplace_back(L"*.png");
    space_hogs_.emplace_back(L"*.tif");
    space_hogs_.emplace_back(L"*.tiff");
}

// If NTFS has last access time tracking on, reset use_last_access_time_ back to false
void DefragState::check_last_access_enabled() {
    DWORD key_disposition;
    HKEY key;
    LONG result = RegCreateKeyExW(
            HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", 0,
            nullptr, REG_OPTION_NON_VOLATILE, KEY_READ, nullptr, &key, &key_disposition);

    if (result == ERROR_SUCCESS) {
        uint32_t ntfs_disable_last_access_update;
        DWORD length = sizeof ntfs_disable_last_access_update;

        result = RegQueryValueExW(key, L"NtfsDisableLastAccessUpdate", nullptr, nullptr,
                                  (BYTE *) &ntfs_disable_last_access_update, &length);

        if (result == ERROR_SUCCESS && ntfs_disable_last_access_update == 1) {
            use_last_access_time_ = false;
        }

        RegCloseKey(key);
    }
}
