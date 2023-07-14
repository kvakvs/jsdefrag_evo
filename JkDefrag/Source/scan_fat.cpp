#include "std_afx.h"
#include "defrag_data_struct.h"

/*
#include "JkDefragLib.h"
#include "JKDefragStruct.h"
#include "JKDefragLog.h"
#include "JkDefragGui.h"
#include "ScanFat.h"
*/

ScanFAT::ScanFAT() {
    defrag_lib_ = DefragLib::get_instance();
}

ScanFAT::~ScanFAT() = default;

ScanFAT *ScanFAT::get_instance() {
    if (instance_ == nullptr) {
        instance_.reset(new ScanFAT());
    }

    return instance_.get();
}

/* Calculate the checksum of 8.3 filename. */
UCHAR ScanFAT::calculate_short_name_check_sum(const UCHAR *name) {
    UCHAR check_sum = 0;

    for (short index = 11; index != 0; index--) {
        check_sum = (check_sum & 1 ? 0x80 : 0) + (check_sum >> 1) + *name++;
    }

    return check_sum;
}

/*

Convert the FAT time fields into a uint64_t time.
Note: the FAT stores times in local time, not in GMT time. This subroutine converts
that into GMT time, to be compatible with the NTFS date/times.

*/
uint64_t ScanFAT::convert_time(const USHORT date, const USHORT time, const USHORT time10) {
    FILETIME time1;
    FILETIME time2;
    ULARGE_INTEGER time3;

    if (DosDateTimeToFileTime(date, time, &time1) == 0) return 0;
    if (LocalFileTimeToFileTime(&time1, &time2) == 0) return 0;

    time3.LowPart = time2.dwLowDateTime;
    time3.HighPart = time2.dwHighDateTime;
    time3.QuadPart = time3.QuadPart + time10 * 100000;

    return time3.QuadPart;
}

/*

Determine the number of clusters in an item and translate the FAT clusterlist
into a FragmentList.
- The first cluster number of an item is recorded in it's directory entry. The second
and next cluster numbers are recorded in the FAT, which is simply an array of "next"
cluster numbers.
- A zero-length file has a first cluster number of 0.
- The FAT contains either an EOC mark (End Of Clusterchain) or the cluster number of
the next cluster of the file.

*/
void ScanFAT::make_fragment_list(const DefragDataStruct *data, const FatDiskInfoStruct *disk_info,
                                 ItemStruct *item, uint64_t cluster) {
    FragmentListStruct *new_fragment;
    FragmentListStruct *last_fragment;

    int max_iterate;

    DefragGui *gui = DefragGui::get_instance();

    item->clusters_count_ = 0;
    item->fragments_ = nullptr;

    /* If cluster is zero then return zero. */
    if (cluster == 0) return;

    /* Loop through the FAT cluster list, counting the clusters and creating items in the fragment list. */
    uint64_t first_cluster = cluster;
    uint64_t last_cluster = 0;
    uint64_t vcn = 0;

    for (max_iterate = 0; max_iterate < disk_info->CountofClusters + 1; max_iterate++) {
        /* Exit the loop when we have reached the end of the cluster list. */
        if (data->disk_.type_ == DiskType::FAT12 && cluster >= 0xFF8) break;
        if (data->disk_.type_ == DiskType::FAT16 && cluster >= 0xFFF8) break;
        if (data->disk_.type_ == DiskType::FAT32 && cluster >= 0xFFFFFF8) break;

        /* Sanity check, test if the cluster is within the range of valid cluster numbers. */
        if (cluster < 2) break;
        if (cluster > disk_info->CountofClusters + 1) break;

        /* Increment the cluster counter. */
        item->clusters_count_ = item->clusters_count_ + 1;

        /* If this is a new fragment then create a record for the previous fragment. If not then
            add the cluster to the counters and continue. */
        if (cluster != last_cluster + 1 && last_cluster != 0) {
            new_fragment = (FragmentListStruct *) malloc(sizeof(FragmentListStruct));

            if (new_fragment == nullptr) {
                gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");
                return;
            }

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

        /* Get next cluster from FAT. */
        switch (data->disk_.type_) {
            case DiskType::FAT12:
                if ((cluster & 1) == 1) {
                    cluster = *(WORD *) &disk_info->FatData.FAT12[cluster + cluster / 2] >> 4;
                } else {
                    cluster = *(WORD *) &disk_info->FatData.FAT12[cluster + cluster / 2] & 0xFFF;
                }

                break;

            case DiskType::FAT16:
                cluster = disk_info->FatData.FAT16[cluster];
                break;
            case DiskType::FAT32:
                cluster = disk_info->FatData.FAT32[cluster] & 0xFFFFFFF;
                break;
        }
    }

    /* If too many iterations (infinite loop in FAT) then exit. */
    if (max_iterate >= disk_info->CountofClusters + 1) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        L"Infinite loop in FAT detected, perhaps the disk is corrupted.");

        return;
    }

    /* Create the last fragment. */
    if (last_cluster != 0) {
        new_fragment = (FragmentListStruct *) malloc(sizeof(FragmentListStruct));

        if (new_fragment == nullptr) {
            gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

            return;
        }

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

/* Load a directory from disk into a new memory buffer. Return nullptr if error.
Note: the caller is responsible for free'ing the buffer. */
BYTE *ScanFAT::load_directory(DefragDataStruct *Data, FatDiskInfoStruct *DiskInfo, uint64_t StartCluster,
                              uint64_t *OutLength) {
    BYTE *buffer;
    uint64_t fragment_length;
    OVERLAPPED g_overlapped;
    ULARGE_INTEGER trans;
    DWORD bytes_read;

    int result;
    int max_iterate;
    wchar_t s1[BUFSIZ];
    DefragGui *gui = DefragGui::get_instance();

    /* Reset the OutLength to zero, in case we exit for an error. */
    if (OutLength != nullptr) *OutLength = 0;

    /* If cluster is zero then return nullptr. */
    if (StartCluster == 0) return nullptr;

    /* Count the size of the directory. */
    uint64_t buffer_length = 0;
    uint64_t cluster = StartCluster;

    for (max_iterate = 0; max_iterate < DiskInfo->CountofClusters + 1; max_iterate++) {
        /* Exit the loop when we have reached the end of the cluster list. */
        if (Data->disk_.type_ == DiskType::FAT12 && cluster >= 0xFF8) break;
        if (Data->disk_.type_ == DiskType::FAT16 && cluster >= 0xFFF8) break;
        if (Data->disk_.type_ == DiskType::FAT32 && cluster >= 0xFFFFFF8) break;

        /* Sanity check, test if the cluster is within the range of valid cluster numbers. */
        if (cluster < 2) return nullptr;
        if (cluster > DiskInfo->CountofClusters + 1) return nullptr;

        /* Increment the BufferLength counter. */
        buffer_length = buffer_length + DiskInfo->SectorsPerCluster * DiskInfo->BytesPerSector;

        /* Get next cluster from FAT. */
        switch (Data->disk_.type_) {
            case DiskType::FAT12:
                if ((cluster & 1) == 1) {
                    cluster = *(WORD *) &DiskInfo->FatData.FAT12[cluster + cluster / 2] >> 4;
                } else {
                    cluster = *(WORD *) &DiskInfo->FatData.FAT12[cluster + cluster / 2] & 0xFFF;
                }
                break;

            case DiskType::FAT16:
                cluster = DiskInfo->FatData.FAT16[cluster];
                break;
            case DiskType::FAT32:
                cluster = DiskInfo->FatData.FAT32[cluster] & 0xFFFFFFF;
                break;
        }
    }

    /* If too many iterations (infinite loop in FAT) then return nullptr. */
    if (max_iterate >= DiskInfo->CountofClusters + 1) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        L"Infinite loop in FAT detected, perhaps the disk is corrupted.");

        return nullptr;
    }

    /* Allocate buffer. */
    if (buffer_length > UINT_MAX) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Directory is too big, %I64u bytes", buffer_length);

        return nullptr;
    }

    buffer = (BYTE *) malloc((size_t) buffer_length);

    if (buffer == nullptr) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        return nullptr;
    }

    /* Loop through the FAT cluster list and load all fragments from disk into the buffer. */
    uint64_t buffer_offset = 0;
    cluster = StartCluster;
    uint64_t first_cluster = cluster;
    uint64_t last_cluster = 0;

    for (max_iterate = 0; max_iterate < DiskInfo->CountofClusters + 1; max_iterate++) {
        /* Exit the loop when we have reached the end of the cluster list. */
        if (Data->disk_.type_ == DiskType::FAT12 && cluster >= 0xFF8) break;
        if (Data->disk_.type_ == DiskType::FAT16 && cluster >= 0xFFF8) break;
        if (Data->disk_.type_ == DiskType::FAT32 && cluster >= 0xFFFFFF8) break;

        /* Sanity check, test if the cluster is within the range of valid cluster numbers. */
        if (cluster < 2) break;
        if (cluster > DiskInfo->CountofClusters + 1) break;

        /* If this is a new fragment then load the previous fragment from disk. If not then
        add to the counters and continue. */
        if (cluster != last_cluster + 1 && last_cluster != 0) {
            fragment_length =
                    (last_cluster - first_cluster + 1) * DiskInfo->SectorsPerCluster * DiskInfo->BytesPerSector;
            trans.QuadPart =
                    (DiskInfo->FirstDataSector + (first_cluster - 2) * DiskInfo->SectorsPerCluster) * DiskInfo->
                            BytesPerSector;

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            L"Reading directory fragment, %I64u bytes at offset=%I64u.", fragment_length,
                            trans.QuadPart);

            result = ReadFile(Data->disk_.volume_handle_, &buffer[buffer_offset], (uint32_t) fragment_length,
                              &bytes_read,
                              &g_overlapped);

            if (result == 0) {
                defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

                gui->show_debug(DebugLevel::Progress, nullptr, L"Error: %s", s1);

                free(buffer);

                return nullptr;
            }

            //ShowHex(Data,Buffer,256);
            buffer_offset = buffer_offset + fragment_length;
            first_cluster = cluster;
        }

        last_cluster = cluster;

        /* Get next cluster from FAT. */
        switch (Data->disk_.type_) {
            case DiskType::FAT12:
                if ((cluster & 1) == 1) {
                    cluster = *(WORD *) &DiskInfo->FatData.FAT12[cluster + cluster / 2] >> 4;
                } else {
                    cluster = *(WORD *) &DiskInfo->FatData.FAT12[cluster + cluster / 2] & 0xFFF;
                }

                break;
            case DiskType::FAT16:
                cluster = DiskInfo->FatData.FAT16[cluster];
                break;
            case DiskType::FAT32:
                cluster = DiskInfo->FatData.FAT32[cluster] & 0xFFFFFFF;
                break;
        }
    }

    /* Load the last fragment. */
    if (last_cluster != 0) {
        fragment_length = (last_cluster - first_cluster + 1) * DiskInfo->SectorsPerCluster * DiskInfo->BytesPerSector;
        trans.QuadPart = (DiskInfo->FirstDataSector + (first_cluster - 2) * DiskInfo->SectorsPerCluster) * DiskInfo->
                BytesPerSector;

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        L"Reading directory fragment, %I64u bytes at offset=%I64u.", fragment_length,
                        trans.QuadPart);

        result = ReadFile(Data->disk_.volume_handle_, &buffer[buffer_offset], (uint32_t) fragment_length, &bytes_read,
                          &g_overlapped);

        if (result == 0) {
            defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

            gui->show_debug(DebugLevel::Progress, nullptr, L"Error: %s", s1);

            free(buffer);

            return nullptr;
        }
    }

    if (OutLength != nullptr) *OutLength = buffer_length;

    return buffer;
}

/* Analyze a directory and add all the items to the item tree. */
void ScanFAT::analyze_fat_directory(DefragDataStruct *data, FatDiskInfoStruct *disk_info, BYTE *buffer,
                                    uint64_t length, ItemStruct *parent_directory) {
    FatDirStruct *dir;
    FatLongNameDirStruct *l_dir;
    ItemStruct *item;
    uint32_t index;
    wchar_t short_name[13];
    wchar_t long_name[820];
    int last_long_name_section;
    UCHAR long_name_checksum;
    BYTE *sub_dir_buf;
    uint64_t sub_dir_length;
    uint64_t start_cluster;
    wchar_t *p1;
    int i;
    DefragGui *gui = DefragGui::get_instance();

    /* Sanity check. */
    if (buffer == nullptr || length == 0) return;

    /* Slow the program down to the percentage that was specified on the
    command line. */
    DefragLib::slow_down(data);

    //ShowHex(data,buffer,256);

    /* Walk through all the directory entries, extract the info, and store in memory
    in the ItemTree. */
    last_long_name_section = 0;

    for (index = 0; index + 31 < length; index = index + 32) {
        if (*data->running_ != RunningState::RUNNING) break;

        dir = (FatDirStruct *) &buffer[index];

        /* Ignore free (not used) entries. */
        if (dir->DIR_Name[0] == 0xE5) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\tFree (not used)", index / 32);

            continue;
        }

        /* Exit at the end of the directory. */
        if (dir->DIR_Name[0] == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\tFree (not used), end of directory.",
                            index / 32);

            break;
        }

        /* If this is a long filename component then save the string and loop. */
        if ((dir->DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) {
            l_dir = (FatLongNameDirStruct *) &buffer[index];

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\tLong filename part.", index / 32);

            i = l_dir->LDIR_Ord & 0x3F;

            if (i == 0) {
                last_long_name_section = 0;

                continue;
            }

            if ((l_dir->LDIR_Ord & 0x40) == 0x40) {
                wmemset(long_name, L'\0', 820);

                last_long_name_section = i;
                long_name_checksum = l_dir->LDIR_Chksum;
            } else {
                if (i + 1 != last_long_name_section || long_name_checksum != l_dir->LDIR_Chksum) {
                    last_long_name_section = 0;

                    continue;
                }

                last_long_name_section = i;
            }

            i = (i - 1) * 13;

            long_name[i++] = l_dir->LDIR_Name1[0];
            long_name[i++] = l_dir->LDIR_Name1[1];
            long_name[i++] = l_dir->LDIR_Name1[2];
            long_name[i++] = l_dir->LDIR_Name1[3];
            long_name[i++] = l_dir->LDIR_Name1[4];
            long_name[i++] = l_dir->LDIR_Name2[0];
            long_name[i++] = l_dir->LDIR_Name2[1];
            long_name[i++] = l_dir->LDIR_Name2[2];
            long_name[i++] = l_dir->LDIR_Name2[3];
            long_name[i++] = l_dir->LDIR_Name2[4];
            long_name[i++] = l_dir->LDIR_Name2[5];
            long_name[i++] = l_dir->LDIR_Name3[0];
            long_name[i++] = l_dir->LDIR_Name3[1];

            continue;
        }

        /* If we are here and the long filename counter is not 1 then something is wrong
        with the long filename. Ignore the long filename. */
        if (last_long_name_section != 1) {
            long_name[0] = '\0';
        } else if (calculate_short_name_check_sum(dir->DIR_Name) != long_name_checksum) {
            gui->show_debug(DebugLevel::Fatal, nullptr, L"%u.\tError: long filename is out of sync");
            long_name[0] = '\0';
        }

        last_long_name_section = 0;

        /* Extract the short name. */
        for (i = 0; i < 8; i++) short_name[i] = dir->DIR_Name[i];

        for (i = 7; i > 0; i--) if (short_name[i] != ' ') break;

        if (short_name[i] != ' ') i++;

        short_name[i] = '.';
        short_name[i + 1] = dir->DIR_Name[8];
        short_name[i + 2] = dir->DIR_Name[9];
        short_name[i + 3] = dir->DIR_Name[10];

        if (short_name[i + 3] != ' ') {
            short_name[i + 4] = '\0';
        } else if (short_name[i + 2] != ' ') {
            short_name[i + 3] = '\0';
        } else if (short_name[i + 1] != ' ') {
            short_name[i + 2] = '\0';
        } else {
            short_name[i] = '\0';
        }
        if (short_name[0] == 0x05) short_name[0] = 0xE5;

        /* If this is a VolumeID then loop. We have no use for it. */
        if ((dir->DIR_Attr & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == ATTR_VOLUME_ID) {
            p1 = wcschr(short_name, L'.');

            if (p1 != nullptr) wcscpy_s(p1, wcslen(p1), p1 + 1);

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\t'%s' (volume ID)", index / 32, short_name);

            continue;
        }

        if ((dir->DIR_Attr & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == (ATTR_DIRECTORY | ATTR_VOLUME_ID)) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\tInvalid directory entry");

            continue;
        }

        /* Ignore "." and "..". */
        if (wcscmp(short_name, L".") == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\t'.'", index / 32);

            continue;
        }

        if (wcscmp(short_name, L"..") == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\t'..'", index / 32);

            continue;
        }

        /* Create and fill a new item record in memory. */
        item = new ItemStruct();

        if (item == nullptr) {
            gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");
            break;
        }

        if (wcscmp(short_name, L".") == 0) {
            item->clear_short_fn();
        } else {
            item->set_short_fn(short_name);
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\t'%s'", index / 32, short_name);
        }

        if (long_name[0] == '\0') {
            item->clear_long_fn();
        } else {
            item->set_long_fn(long_name);
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"\tLong filename = '%s'", long_name);
        }

        item->clear_short_path();
        item->clear_long_path();
        item->bytes_ = dir->DIR_FileSize;

        if (data->disk_.type_ == DiskType::FAT32) {
            start_cluster = MAKELONG(dir->DIR_FstClusLO, dir->DIR_FstClusHI);
        } else {
            start_cluster = dir->DIR_FstClusLO;
        }

        make_fragment_list(data, disk_info, item, start_cluster);

        item->creation_time_ = convert_time(dir->DIR_CrtDate, dir->DIR_CrtTime, dir->DIR_CrtTimeTenth);
        item->mft_change_time_ = convert_time(dir->DIR_WrtDate, dir->DIR_WrtTime, 0);
        item->last_access_time_ = convert_time(dir->DIR_LstAccDate, 0, 0);
        item->parent_inode_ = 0;
        item->parent_directory_ = parent_directory;
        item->is_dir_ = false;

        if ((dir->DIR_Attr & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == ATTR_DIRECTORY) item->is_dir_ = true;

        item->is_unmovable_ = false;
        item->is_excluded_ = false;
        item->is_hog_ = false;

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"\tSize = %I64u clusters, %I64u bytes",
                        item->clusters_count_, item->bytes_);

        /* Add the item record to the sorted item tree in memory. */
        DefragLib::tree_insert(data, item);

        /* Draw the item on the screen. */
        gui->show_analyze(data, item);
        //		if (*data->RedrawScreen == false) {
        defrag_lib_->colorize_item(data, item, 0, 0, false);
        //		} else {
        //			show_diskmap(data);
        //		}

        /* Increment counters. */
        if (item->is_dir_) {
            data->count_directories_ = data->count_directories_ + 1;
        }

        data->count_all_files_ = data->count_all_files_ + 1;
        data->count_all_bytes_ = data->count_all_bytes_ + item->bytes_;
        data->count_all_clusters_ = data->count_all_clusters_ + item->clusters_count_;

        if (DefragLib::get_fragment_count(item) > 1) {
            data->count_fragmented_items_ = data->count_fragmented_items_ + 1;
            data->count_fragmented_bytes_ = data->count_fragmented_bytes_ + item->bytes_;
            data->count_fragmented_clusters_ = data->count_fragmented_clusters_ + item->clusters_count_;
        }

        /* If this is a directory then iterate. */
        if (item->is_dir_ == true) {
            sub_dir_buf = load_directory(data, disk_info, start_cluster, &sub_dir_length);

            analyze_fat_directory(data, disk_info, sub_dir_buf, sub_dir_length, item);

            free(sub_dir_buf);

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Finished with subdirectory.");
        }
    }
}

/* Analyze a FAT disk and load into the ItemTree in memory. Return FALSE if the disk is
not a FAT disk, or could not be analyzed.

- The maximum valid cluster number for the volume is CountofClusters + 1
- The FAT runs from cluster 0 to CountofClusters + 1.
- The size of the FAT is:
FAT12: (CountofClusters + 1) * 1.5
FAT16: (CountofClusters + 1) * 2
FAT32: (CountofClusters + 1) * 4
- Compute the sector number for data cluster number N:
FirstSectorOfCluster = ((N - 2) * BootSector.BPB_SecPerClus) + DiskInfo.FirstDataSector;
- Calculate location in the FAT for a given cluster number N:
switch (FATType) {
case FAT12: FATOffset = N + (N / 2); break;            // 12 bit is 1.5 bytes.
case FAT16: FATOffset = N * 2;       break;            // 16 bit is 2 bytes.
case FAT32: FATOffset = N * 4;       break;            // 32 bit is 4 bytes.
}
ThisFATSecNum = BPB_ResvdSecCnt + (FATOffset / BPB_BytsPerSec);
ThisFATEntOffset = FATOffset % BPB_BytsPerSec;
- Determine End Of Clusterchain:
IsEOC = FALSE;
switch (FATType) {
case FAT12: if (FATContent >= 0x0FF8) IsEOC = TRUE; break;
case FAT16: if (FATContent >= 0xFFF8) IsEOC = TRUE; break;
case FAT32: if (FATContent >= 0x0FFFFFF8) IsEOC = TRUE; break;
}
- Determine Bad Cluster:
IsBadCluster = FALSE;
switch (FATType) {
case FAT12: if (FATContent == 0x0FF7) IsBadCluster = TRUE; break;
case FAT16: if (FATContent == 0xFFF7) IsBadCluster = TRUE; break;
case FAT32: if (FATContent == 0x0FFFFFF7) IsBadCluster = TRUE; break;
}
*/
BOOL ScanFAT::analyze_fat_volume(DefragDataStruct *data) {
    FatBootSectorStruct BootSector;
    FatDiskInfoStruct DiskInfo;

    ULARGE_INTEGER Trans;

    OVERLAPPED gOverlapped;

    DWORD BytesRead;

    size_t FatSize;

    BYTE *RootDirectory;

    uint64_t RootStart;
    uint64_t RootLength;

    int Result;

    wchar_t s1[BUFSIZ];

    char s2[BUFSIZ];

    DefragGui *jkGui = DefragGui::get_instance();

    /* Read the boot block from the disk. */
    gOverlapped.Offset = 0;
    gOverlapped.OffsetHigh = 0;
    gOverlapped.hEvent = nullptr;

    Result = ReadFile(data->disk_.volume_handle_, &BootSector, sizeof(FatBootSectorStruct), &BytesRead,
                      &gOverlapped);

    if (Result == 0 || BytesRead != 512) {
        defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error while reading bootblock: %s", s1);

        return FALSE;
    }

    /* Test if the boot block is a FAT boot block. */
    if (BootSector.Signature != 0xAA55 ||
        ((BootSector.BS_jmpBoot[0] != 0xEB || BootSector.BS_jmpBoot[2] != 0x90) &&
         BootSector.BS_jmpBoot[0] != 0xE9)) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"This is not a FAT disk (different cookie).");

        return FALSE;
    }

    /* Fetch values from the bootblock and determine what FAT this is, FAT12, FAT16, or FAT32. */
    DiskInfo.BytesPerSector = BootSector.BPB_BytsPerSec;

    if (DiskInfo.BytesPerSector == 0) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"This is not a FAT disk (BytesPerSector is zero).");

        return FALSE;
    }

    DiskInfo.SectorsPerCluster = BootSector.BPB_SecPerClus;

    if (DiskInfo.SectorsPerCluster == 0) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"This is not a FAT disk (SectorsPerCluster is zero).");

        return FALSE;
    }

    DiskInfo.TotalSectors = BootSector.BPB_TotSec16;

    if (DiskInfo.TotalSectors == 0) DiskInfo.TotalSectors = BootSector.BPB_TotSec32;

    DiskInfo.RootDirSectors = (BootSector.BPB_RootEntCnt * 32 + (BootSector.BPB_BytsPerSec - 1)) / BootSector.
            BPB_BytsPerSec;

    DiskInfo.FATSz = BootSector.BPB_FATSz16;

    if (DiskInfo.FATSz == 0) DiskInfo.FATSz = BootSector.Fat32.BPB_FATSz32;

    DiskInfo.FirstDataSector = BootSector.BPB_RsvdSecCnt + BootSector.BPB_NumFATs * DiskInfo.FATSz + DiskInfo.
            RootDirSectors;

    DiskInfo.DataSec = DiskInfo.TotalSectors - (BootSector.BPB_RsvdSecCnt + BootSector.BPB_NumFATs * DiskInfo.FATSz +
                                                DiskInfo.RootDirSectors);

    DiskInfo.CountofClusters = DiskInfo.DataSec / BootSector.BPB_SecPerClus;

    if (DiskInfo.CountofClusters < 4085) {
        data->disk_.type_ = DiskType::FAT12;

        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"This is a FAT12 disk.");
    } else if (DiskInfo.CountofClusters < 65525) {
        data->disk_.type_ = DiskType::FAT16;

        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"This is a FAT16 disk.");
    } else {
        data->disk_.type_ = DiskType::FAT32;

        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"This is a FAT32 disk.");
    }

    data->bytes_per_cluster_ = DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster;
    data->total_clusters_ = DiskInfo.CountofClusters;

    /* Output debug information. */
    strncpy_s(s2, BUFSIZ, (char *) &BootSector.BS_OEMName[0], 8);

    s2[8] = '\0';

    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  OEMName: %S", s2);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BytesPerSector: %I64u", DiskInfo.BytesPerSector);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  TotalSectors: %I64u", DiskInfo.TotalSectors);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  SectorsPerCluster: %I64u", DiskInfo.SectorsPerCluster);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  RootDirSectors: %I64u", DiskInfo.RootDirSectors);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FATSz: %I64u", DiskInfo.FATSz);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FirstDataSector: %I64u", DiskInfo.FirstDataSector);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  DataSec: %I64u", DiskInfo.DataSec);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  CountofClusters: %I64u", DiskInfo.CountofClusters);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  ReservedSectors: %lu", BootSector.BPB_RsvdSecCnt);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  NumberFATs: %lu", BootSector.BPB_NumFATs);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  RootEntriesCount: %lu", BootSector.BPB_RootEntCnt);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  MediaType: %X", BootSector.BPB_Media);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  SectorsPerTrack: %lu", BootSector.BPB_SecPerTrk);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  NumberOfHeads: %lu", BootSector.BPB_NumHeads);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  HiddenSectors: %lu", BootSector.BPB_HiddSec);

    if (data->disk_.type_ != DiskType::FAT32) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BS_DrvNum: %u", BootSector.Fat16.BS_DrvNum);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BS_BootSig: %u", BootSector.Fat16.BS_BootSig);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BS_VolID: %u", BootSector.Fat16.BS_VolID);

        strncpy_s(s2, BUFSIZ, (char *) &BootSector.Fat16.BS_VolLab[0], 11);

        s2[11] = '\0';

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  VolLab: %S", s2);

        strncpy_s(s2, BUFSIZ, (char *) &BootSector.Fat16.BS_FilSysType[0], 8);

        s2[8] = '\0';

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FilSysType: %S", s2);
    } else {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FATSz32: %lu", BootSector.Fat32.BPB_FATSz32);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  ExtFlags: %lu", BootSector.Fat32.BPB_ExtFlags);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FSVer: %lu", BootSector.Fat32.BPB_FSVer);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  RootClus: %lu", BootSector.Fat32.BPB_RootClus);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FSInfo: %lu", BootSector.Fat32.BPB_FSInfo);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BkBootSec: %lu", BootSector.Fat32.BPB_BkBootSec);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  DrvNum: %lu", BootSector.Fat32.BS_DrvNum);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BootSig: %lu", BootSector.Fat32.BS_BootSig);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  VolID: %lu", BootSector.Fat32.BS_VolID);

        strncpy_s(s2, BUFSIZ, (char *) &BootSector.Fat32.BS_VolLab[0], 11);

        s2[11] = '\0';

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  VolLab: %S", s2);

        strncpy_s(s2, BUFSIZ, (char *) &BootSector.Fat32.BS_FilSysType[0], 8);

        s2[8] = '\0';

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FilSysType: %S", s2);
    }

    /* Read the FAT from disk into memory. */
    switch (data->disk_.type_) {
        case DiskType::FAT12:
            FatSize = (size_t) (DiskInfo.CountofClusters + 1 + (DiskInfo.CountofClusters + 1) / 2);
            break;
        case DiskType::FAT16:
            FatSize = (size_t) ((DiskInfo.CountofClusters + 1) * 2);
            break;
        case DiskType::FAT32:
            FatSize = (size_t) ((DiskInfo.CountofClusters + 1) * 4);
            break;
    }

    if (FatSize % DiskInfo.BytesPerSector > 0) {
        FatSize = (size_t) (FatSize + DiskInfo.BytesPerSector - FatSize % DiskInfo.BytesPerSector);
    }

    DiskInfo.FatData.FAT12 = (BYTE *) malloc(FatSize);

    if (DiskInfo.FatData.FAT12 == nullptr) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        return FALSE;
    }

    Trans.QuadPart = BootSector.BPB_RsvdSecCnt * DiskInfo.BytesPerSector;
    gOverlapped.Offset = Trans.LowPart;
    gOverlapped.OffsetHigh = Trans.HighPart;
    gOverlapped.hEvent = nullptr;

    jkGui->show_debug(DebugLevel::Progress, nullptr, L"Reading FAT, %lu bytes at offset=%I64u", FatSize,
                      Trans.QuadPart);

    Result = ReadFile(data->disk_.volume_handle_, DiskInfo.FatData.FAT12, (uint32_t) FatSize, &BytesRead, &gOverlapped);

    if (Result == 0) {
        defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: %s", s1);

        return FALSE;
    }

    //ShowHex(Data,DiskInfo.FatData.FAT12,32);

    /* Read the root directory from disk into memory. */
    if (data->disk_.type_ == DiskType::FAT32) {
        RootDirectory = load_directory(data, &DiskInfo, BootSector.Fat32.BPB_RootClus, &RootLength);
    } else {
        RootStart = (BootSector.BPB_RsvdSecCnt + BootSector.BPB_NumFATs * DiskInfo.FATSz) * DiskInfo.BytesPerSector;
        RootLength = BootSector.BPB_RootEntCnt * 32;

        /* Sanity check. */
        if (RootLength > UINT_MAX) {
            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Root directory is too big, %I64u bytes", RootLength);

            free(DiskInfo.FatData.FAT12);

            return FALSE;
        }

        if (RootStart > (DiskInfo.CountofClusters + 1) * DiskInfo.SectorsPerCluster * DiskInfo.BytesPerSector) {
            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Trying to access %I64u, but the last sector is at %I64u",
                              RootStart,
                              (DiskInfo.CountofClusters + 1) * DiskInfo.SectorsPerCluster * DiskInfo.BytesPerSector);

            free(DiskInfo.FatData.FAT12);

            return FALSE;
        }

        /* We have to round up the Length to the nearest sector. For some reason or other
        Microsoft has decided that raw reading from disk can only be done by whole sector,
        even though ReadFile() accepts it's parameters in bytes. */
        BytesRead = (uint32_t) RootLength;

        if (RootLength % DiskInfo.BytesPerSector > 0) {
            BytesRead = (uint32_t) (RootLength + DiskInfo.BytesPerSector - RootLength % DiskInfo.BytesPerSector);
        }

        /* Allocate buffer. */
        RootDirectory = (BYTE *) malloc(BytesRead);
        if (RootDirectory == nullptr) {
            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");
            free(DiskInfo.FatData.FAT12);
            return FALSE;
        }

        /* Read data from disk. */
        Trans.QuadPart = RootStart;

        gOverlapped.Offset = Trans.LowPart;
        gOverlapped.OffsetHigh = Trans.HighPart;
        gOverlapped.hEvent = nullptr;

        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                          L"Reading root directory, %lu bytes at offset=%I64u.", BytesRead, Trans.QuadPart);

        Result = ReadFile(data->disk_.volume_handle_, RootDirectory, BytesRead, &BytesRead, &gOverlapped);

        if (Result == 0) {
            defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: %s", s1);

            free(DiskInfo.FatData.FAT12);
            free(RootDirectory);

            return FALSE;
        }
    }

    /* Analyze all the items in the root directory and add to the item tree. */
    analyze_fat_directory(data, &DiskInfo, RootDirectory, RootLength, nullptr);

    /* Cleanup. */
    free(RootDirectory);
    free(DiskInfo.FatData.FAT12);

    return TRUE;
}
