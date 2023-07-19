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
#include "defrag_state.h"

#include <memory>

ScanFAT::ScanFAT() {
    defrag_lib_ = DefragRunner::get_instance();
}

ScanFAT::~ScanFAT() = default;

ScanFAT *ScanFAT::get_instance() {
    if (instance_ == nullptr) {
        instance_ = std::make_unique<ScanFAT>();
    }

    return instance_.get();
}

// Calculate the checksum of 8.3 filename
uint8_t ScanFAT::calculate_short_name_check_sum(const UCHAR *name) {
    uint8_t check_sum = 0;

    for (short index = 11; index != 0; index--) {
        check_sum = (check_sum & 1 ? 0x80 : 0) + (check_sum >> 1) + *name++;
    }

    return check_sum;
}

// Convert the FAT time fields into a uint64_t time.
// Note: the FAT stores times in local time, not in GMT time. This subroutine converts
// that into GMT time, to be compatible with the NTFS date/times.
filetime64_t ScanFAT::convert_time(const USHORT date, const USHORT time, const USHORT time10) {
    FILETIME time1;
    FILETIME time2;

    if (DosDateTimeToFileTime(date, time, &time1) == 0) return {};
    if (LocalFileTimeToFileTime(&time1, &time2) == 0) return {};

    return from_FILETIME(time2) + std::chrono::microseconds(time10 * 100000);
}

// Determine the number of clusters in an item and translate the FAT clusterlist
// into a FragmentList.
// - The first cluster number of an item is recorded in it's directory entry. The second
// and next cluster numbers are recorded in the FAT, which is simply an array of "next"
// cluster numbers.
// - A zero-length file has a first cluster number of 0.
// - The FAT contains either an EOC mark (End Of Clusterchain) or the cluster number of
// the next cluster of the file.
void ScanFAT::make_fragment_list(const DefragState &data, const FatDiskInfoStruct *disk_info,
                                 FileNode *item, uint64_t cluster) {
    FileFragment *new_fragment;
    FileFragment *last_fragment;

    int max_iterate;

    DefragGui *gui = DefragGui::get_instance();

    item->clusters_count_ = 0;
    item->fragments_ = nullptr;

    // If cluster is zero then return zero
    if (cluster == 0) return;

    // Loop through the FAT cluster list, counting the clusters and creating items in the fragment list
    uint64_t first_cluster = cluster;
    uint64_t last_cluster = 0;
    uint64_t vcn = 0;

    for (max_iterate = 0; max_iterate < disk_info->countof_clusters_ + 1; max_iterate++) {
        // Exit the loop when we have reached the end of the cluster list
        if (data.disk_.type_ == DiskType::FAT12 && cluster >= 0xFF8) break;
        if (data.disk_.type_ == DiskType::FAT16 && cluster >= 0xFFF8) break;
        if (data.disk_.type_ == DiskType::FAT32 && cluster >= 0xFFFFFF8) break;

        // Sanity check, test if the cluster is within the range of valid cluster numbers
        if (cluster < 2) break;
        if (cluster > disk_info->countof_clusters_ + 1) break;

        // Increment the cluster counter
        item->clusters_count_ = item->clusters_count_ + 1;

        /* If this is a new fragment then create a record for the previous fragment. If not then
            add the cluster to the counters and continue. */
        if (cluster != last_cluster + 1 && last_cluster != 0) {
            new_fragment = new FileFragment();

            new_fragment->lcn_ = first_cluster - 2;
            vcn = vcn + last_cluster - first_cluster + 1;
            new_fragment->next_vcn_ = vcn;
            new_fragment->next_ = nullptr;

            if (item->fragments_ == nullptr) {
                item->fragments_ = new_fragment;
            } else {
                if (last_fragment != nullptr) last_fragment->next_ = new_fragment;
            }

            last_fragment = new_fragment;
            first_cluster = cluster;
        }

        last_cluster = cluster;

        // Get next cluster from FAT
        switch (data.disk_.type_) {
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

    // If too many iterations (infinite loop in FAT) then exit
    if (max_iterate >= disk_info->countof_clusters_ + 1) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        L"Infinite loop in FAT detected, perhaps the disk is corrupted.");

        return;
    }

    // Create the last fragment
    if (last_cluster != 0) {
        new_fragment = new FileFragment();

        new_fragment->lcn_ = first_cluster - 2;
        vcn = vcn + last_cluster - first_cluster + 1;
        new_fragment->next_vcn_ = vcn;
        new_fragment->next_ = nullptr;

        if (item->fragments_ == nullptr) {
            item->fragments_ = new_fragment;
        } else {
            if (last_fragment != nullptr) last_fragment->next_ = new_fragment;
        }
    }
}
