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

/**
 * \brief Load a directory from disk into a new memory buffer.
 * \return Return nullptr if error. Note: the caller owns the returned buffer.
 */
std::unique_ptr<uint8_t> ScanFAT::load_directory(
        const DefragState &data, const FatDiskInfoStruct *disk_info, const Clusters64 start_cluster, PARAM_OUT
        Bytes64 &out_length) {

    OVERLAPPED g_overlapped;
    DWORD bytes_read;
    DefragGui *gui = DefragGui::get_instance();

    // Reset the OutLength to zero, in case we exit for an error
    PARAM_OUT out_length = {};

    if (start_cluster.is_zero()) return nullptr;

    // Count the size of the directory
    Bytes64 buffer_length = {};
    Clusters64 cluster = start_cluster;
    Clusters64 max_iterate = {};

    for (; max_iterate < disk_info->countof_clusters_ + Clusters64(1); max_iterate++) {
        // Exit the loop when we have reached the end of the cluster list
        if (data.disk_.type_ == DiskType::FAT12 && cluster >= fat12_max_cluster) break;
        if (data.disk_.type_ == DiskType::FAT16 && cluster >= fat16_max_cluster) break;
        if (data.disk_.type_ == DiskType::FAT32 && cluster >= fat32_max_cluster) break;

        // Sanity check, test if the cluster is within the range of valid cluster numbers
        if (cluster < Clusters64(2)) return nullptr;
        if (cluster > disk_info->countof_clusters_ + Clusters64(1)) return nullptr;

        // Increment the BufferLength counter
        // 'sectors * 'bytes_per_sector = 'bytes; TODO: Unit whose type contains a fraction of bytes/sector
        buffer_length += Bytes64(disk_info->sectors_per_cluster_.value() * disk_info->bytes_per_sector_.value());

        // Get next cluster from FAT
        cluster = get_next_fat_cluster(data, disk_info, cluster);
    }

    // If too many iterations (infinite loop in FAT) then return nullptr
    if (max_iterate >= disk_info->countof_clusters_ + Clusters64(1)) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        L"Infinite loop in FAT detected, perhaps the disk is corrupted.");

        return nullptr;
    }

    // Allocate buffer
    if (buffer_length > Bytes64(UINT_MAX)) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Directory is too big, " NUM_FMT " bytes", buffer_length.value()));

        return nullptr;
    }

    std::unique_ptr<uint8_t> buffer = std::make_unique<uint8_t>(buffer_length.value());

    // Loop through the FAT cluster list and load all fragments from the disk into the buffer.
    Bytes64 buffer_offset = {};
    cluster = start_cluster;

    Clusters64 first_cluster = cluster;
    Clusters64 last_cluster = {};

    for (max_iterate = {}; max_iterate < disk_info->countof_clusters_ + Clusters64(1); max_iterate++) {
        // Exit the loop when we have reached the end of the cluster list
        if (data.disk_.type_ == DiskType::FAT12 && cluster >= fat12_max_cluster) break;
        if (data.disk_.type_ == DiskType::FAT16 && cluster >= fat16_max_cluster) break;
        if (data.disk_.type_ == DiskType::FAT32 && cluster >= fat32_max_cluster) break;

        // Sanity check, test if the cluster is within the range of valid cluster numbers
        if (cluster < Clusters64(2)) break;
        if (cluster > disk_info->countof_clusters_ + Clusters64(1)) break;

        // If this is a new fragment then load the previous fragment from disk. If not then add to the counters and continue
        if (cluster != last_cluster + Clusters64(1) && last_cluster) {
            // 'clusters * ('sectors / 'cluster') * ('bytes / 'sector') = 'bytes
            Bytes64 fragment_length = Bytes64((last_cluster - first_cluster + Clusters64(1)).value()
                                              * disk_info->sectors_per_cluster_.value()
                                              * disk_info->bytes_per_sector_.value());
            ULARGE_INTEGER trans;
            trans.QuadPart =
                    (disk_info->first_data_sector_.as<ULONGLONG>() +
                     (first_cluster.as<ULONGLONG>() - 2) * disk_info->sectors_per_cluster_.as<ULONGLONG>())
                    * disk_info->bytes_per_sector_.as<ULONGLONG>();

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"Reading directory fragment, " NUM_FMT " bytes at offset=" NUM_FMT,
                                        fragment_length.value(), trans.QuadPart));

            BOOL result = ReadFile(data.disk_.volume_handle_, buffer.get() + buffer_offset.value(),
                                   fragment_length.as<DWORD>(), &bytes_read,
                                   &g_overlapped);

            if (result == FALSE) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"Error: {}", Str::system_error(GetLastError())));
                return nullptr;
            }

            buffer_offset += fragment_length;
            first_cluster = cluster;
        }

        last_cluster = cluster;

        // Get next cluster from FAT
        cluster = get_next_fat_cluster(data, disk_info, cluster);
    }

    // Load the last fragment
    if (last_cluster) {
        Bytes64 fragment_length = Bytes64((last_cluster - first_cluster + Clusters64(1)).value()
                                          * disk_info->sectors_per_cluster_.value()
                                          * disk_info->bytes_per_sector_.value());
        ULARGE_INTEGER trans;
        trans.QuadPart =
                (disk_info->first_data_sector_.value() +
                 (first_cluster.value() - 2) * disk_info->sectors_per_cluster_.value())
                * disk_info->bytes_per_sector_.value();

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"reading directory fragment, " NUM_FMT " bytes at offset=" NUM_FMT,
                                    fragment_length.value(), trans.QuadPart));

        BOOL result = ReadFile(data.disk_.volume_handle_, buffer.get() + buffer_offset.value(),
                               fragment_length.as<DWORD>(),
                               &bytes_read, &g_overlapped);

        if (result == FALSE) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"Error: {}", Str::system_error(GetLastError())));
            buffer.reset();
            return buffer; // empty smart pointer = nullptr
        }
    }

    PARAM_OUT out_length = buffer_length;

    return buffer;
}
