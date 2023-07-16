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
BYTE *
ScanFAT::load_directory(const DefragState *data, const FatDiskInfoStruct *disk_info, const uint64_t start_cluster,
                        uint64_t *out_length) {
    std::unique_ptr<BYTE[]> buffer;
    uint64_t fragment_length;
    OVERLAPPED g_overlapped;
    DWORD bytes_read;
    int max_iterate;
    wchar_t s1[BUFSIZ];
    DefragGui *gui = DefragGui::get_instance();

    // Reset the OutLength to zero, in case we exit for an error
    if (out_length != nullptr) *out_length = 0;

    // If cluster is zero then return nullptr
    if (start_cluster == 0) return nullptr;

    // Count the size of the directory
    uint64_t buffer_length = 0;
    uint64_t cluster = start_cluster;

    for (max_iterate = 0; max_iterate < disk_info->countof_clusters_ + 1; max_iterate++) {
        // Exit the loop when we have reached the end of the cluster list
        if (data->disk_.type_ == DiskType::FAT12 && cluster >= 0xFF8) break;
        if (data->disk_.type_ == DiskType::FAT16 && cluster >= 0xFFF8) break;
        if (data->disk_.type_ == DiskType::FAT32 && cluster >= 0xFFFFFF8) break;

        // Sanity check, test if the cluster is within the range of valid cluster numbers
        if (cluster < 2) return nullptr;
        if (cluster > disk_info->countof_clusters_ + 1) return nullptr;

        // Increment the BufferLength counter
        buffer_length = buffer_length + disk_info->sectors_per_cluster_ * disk_info->bytes_per_sector_;

        // Get next cluster from FAT
        switch (data->disk_.type_) {
            case DiskType::FAT12:
                if ((cluster & 1) == 1) {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] >> 4;
                } else {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] & 0xFFF;
                }
                break;

            case DiskType::FAT16:
                cluster = disk_info->fat_data_.fat16[cluster];
                break;
            case DiskType::FAT32:
                cluster = disk_info->fat_data_.fat32[cluster] & 0xFFFFFFF;
                break;
        }
    }

    // If too many iterations (infinite loop in FAT) then return nullptr
    if (max_iterate >= disk_info->countof_clusters_ + 1) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        L"Infinite loop in FAT detected, perhaps the disk is corrupted.");

        return nullptr;
    }

    // Allocate buffer
    if (buffer_length > UINT_MAX) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Directory is too big, " NUM_FMT " bytes", buffer_length));

        return nullptr;
    }

    buffer = std::make_unique<BYTE[]>(buffer_length);

    // Loop through the FAT cluster list and load all fragments from disk into the buffer.
    uint64_t buffer_offset = 0;
    cluster = start_cluster;

    uint64_t first_cluster = cluster;
    uint64_t last_cluster = 0;

    for (max_iterate = 0; max_iterate < disk_info->countof_clusters_ + 1; max_iterate++) {
        // Exit the loop when we have reached the end of the cluster list
        if (data->disk_.type_ == DiskType::FAT12 && cluster >= 0xFF8) break;
        if (data->disk_.type_ == DiskType::FAT16 && cluster >= 0xFFF8) break;
        if (data->disk_.type_ == DiskType::FAT32 && cluster >= 0xFFFFFF8) break;

        // Sanity check, test if the cluster is within the range of valid cluster numbers
        if (cluster < 2) break;
        if (cluster > disk_info->countof_clusters_ + 1) break;

        /* If this is a new fragment then load the previous fragment from disk. If not then
        add to the counters and continue. */
        if (cluster != last_cluster + 1 && last_cluster != 0) {
            fragment_length =
                    (last_cluster - first_cluster + 1) * disk_info->sectors_per_cluster_ * disk_info->bytes_per_sector_;
            ULARGE_INTEGER trans;
            trans.QuadPart =
                    (disk_info->first_data_sector_ + (first_cluster - 2) * disk_info->sectors_per_cluster_) *
                    disk_info->bytes_per_sector_;

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"Reading directory fragment, " NUM_FMT " bytes at offset=" NUM_FMT,
                                        fragment_length, trans.QuadPart));

            BOOL result = ReadFile(data->disk_.volume_handle_, &buffer[buffer_offset], (uint32_t) fragment_length,
                                   &bytes_read, &g_overlapped);

            if (result == FALSE) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"Error: {}", DefragLib::system_error_str(GetLastError())));
                return nullptr;
            }

            buffer_offset = buffer_offset + fragment_length;
            first_cluster = cluster;
        }

        last_cluster = cluster;

        // Get next cluster from FAT
        switch (data->disk_.type_) {
            case DiskType::FAT12:
                if ((cluster & 1) == 1) {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] >> 4;
                } else {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] & 0xFFF;
                }

                break;
            case DiskType::FAT16:
                cluster = disk_info->fat_data_.fat16[cluster];
                break;
            case DiskType::FAT32:
                cluster = disk_info->fat_data_.fat32[cluster] & 0xFFFFFFF;
                break;
        }
    }

    // Load the last fragment
    if (last_cluster != 0) {
        fragment_length =
                (last_cluster - first_cluster + 1) * disk_info->sectors_per_cluster_ * disk_info->bytes_per_sector_;
        ULARGE_INTEGER trans;
        trans.QuadPart =
                (disk_info->first_data_sector_ + (first_cluster - 2) * disk_info->sectors_per_cluster_)
                * disk_info->bytes_per_sector_;

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"reading directory fragment, " NUM_FMT " bytes at offset=" NUM_FMT,
                                    fragment_length, trans.QuadPart));

        BOOL result = ReadFile(data->disk_.volume_handle_, &buffer[buffer_offset], (uint32_t) fragment_length,
                               &bytes_read, &g_overlapped);

        if (result == FALSE) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"Error: {}", DefragLib::system_error_str(GetLastError())));
            return nullptr;
        }
    }

    if (out_length != nullptr) *out_length = buffer_length;
    return buffer.release();
}
