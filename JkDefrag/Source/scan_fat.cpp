#include "std_afx.h"
#include "defrag_data_struct.h"

/*
#include "JkDefragLib.h"
#include "JKDefragStruct.h"
#include "JKDefragLog.h"
#include "JkDefragGui.h"
#include "ScanFat.h"
*/

JKScanFat* JKScanFat::m_jkScanFat = 0;

JKScanFat::JKScanFat() {
    m_jkLib = DefragLib::getInstance();
}

JKScanFat::~JKScanFat() {
    delete m_jkScanFat;
}

JKScanFat* JKScanFat::getInstance() {
    if (m_jkScanFat == nullptr) {
        m_jkScanFat = new JKScanFat();
    }

    return m_jkScanFat;
}

/* Calculate the checksum of 8.3 filename. */
UCHAR JKScanFat::CalculateShortNameCheckSum(UCHAR* Name) {
    short Index;
    UCHAR CheckSum;

    CheckSum = 0;

    for (Index = 11; Index != 0; Index--) {
        CheckSum = (CheckSum & 1 ? 0x80 : 0) + (CheckSum >> 1) + *Name++;
    }

    return CheckSum;
}

/*

Convert the FAT time fields into a uint64_t time.
Note: the FAT stores times in local time, not in GMT time. This subroutine converts
that into GMT time, to be compatible with the NTFS date/times.

*/
uint64_t JKScanFat::ConvertTime(USHORT Date, USHORT Time, USHORT Time10) {
    FILETIME Time1;
    FILETIME Time2;

    ULARGE_INTEGER Time3;

    if (DosDateTimeToFileTime(Date, Time, &Time1) == 0) return 0;
    if (LocalFileTimeToFileTime(&Time1, &Time2) == 0) return 0;

    Time3.LowPart = Time2.dwLowDateTime;
    Time3.HighPart = Time2.dwHighDateTime;
    Time3.QuadPart = Time3.QuadPart + Time10 * 100000;

    return Time3.QuadPart;
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
void JKScanFat::make_fragment_list(const DefragDataStruct* data, const FatDiskInfoStruct* disk_info,
                                   ItemStruct* item, uint64_t cluster) {
    FragmentListStruct* new_fragment;
    FragmentListStruct* last_fragment;

    int max_iterate;

    DefragGui* gui = DefragGui::get_instance();

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
            new_fragment = (FragmentListStruct*)malloc(sizeof(FragmentListStruct));

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
            }
            else {
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
                cluster = *(WORD*)&disk_info->FatData.FAT12[cluster + cluster / 2] >> 4;
            }
            else {
                cluster = *(WORD*)&disk_info->FatData.FAT12[cluster + cluster / 2] & 0xFFF;
            }

            break;

        case DiskType::FAT16: cluster = disk_info->FatData.FAT16[cluster];
            break;
        case DiskType::FAT32: cluster = disk_info->FatData.FAT32[cluster] & 0xFFFFFFF;
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
        new_fragment = (FragmentListStruct*)malloc(sizeof(FragmentListStruct));

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
        }
        else {
            if (last_fragment != nullptr) last_fragment->next_ = new_fragment;
        }
    }
}

/* Load a directory from disk into a new memory buffer. Return nullptr if error.
Note: the caller is responsible for free'ing the buffer. */
BYTE* JKScanFat::LoadDirectory(DefragDataStruct* Data, FatDiskInfoStruct* DiskInfo, uint64_t StartCluster,
                               uint64_t* OutLength) {
    BYTE* Buffer;

    uint64_t BufferLength;
    uint64_t BufferOffset;
    uint64_t Cluster;
    uint64_t FirstCluster;
    uint64_t LastCluster;
    uint64_t FragmentLength;

    OVERLAPPED gOverlapped;

    ULARGE_INTEGER Trans;

    DWORD BytesRead;

    int Result;
    int MaxIterate;

    WCHAR s1[BUFSIZ];

    DefragGui* jkGui = DefragGui::get_instance();

    /* Reset the OutLength to zero, in case we exit for an error. */
    if (OutLength != nullptr) *OutLength = 0;

    /* If cluster is zero then return nullptr. */
    if (StartCluster == 0) return nullptr;

    /* Count the size of the directory. */
    BufferLength = 0;
    Cluster = StartCluster;

    for (MaxIterate = 0; MaxIterate < DiskInfo->CountofClusters + 1; MaxIterate++) {
        /* Exit the loop when we have reached the end of the cluster list. */
        if (Data->disk_.type_ == DiskType::FAT12 && Cluster >= 0xFF8) break;
        if (Data->disk_.type_ == DiskType::FAT16 && Cluster >= 0xFFF8) break;
        if (Data->disk_.type_ == DiskType::FAT32 && Cluster >= 0xFFFFFF8) break;

        /* Sanity check, test if the cluster is within the range of valid cluster numbers. */
        if (Cluster < 2) return nullptr;
        if (Cluster > DiskInfo->CountofClusters + 1) return nullptr;

        /* Increment the BufferLength counter. */
        BufferLength = BufferLength + DiskInfo->SectorsPerCluster * DiskInfo->BytesPerSector;

        /* Get next cluster from FAT. */
        switch (Data->disk_.type_) {
        case DiskType::FAT12:
            if ((Cluster & 1) == 1) {
                Cluster = *(WORD*)&DiskInfo->FatData.FAT12[Cluster + Cluster / 2] >> 4;
            }
            else {
                Cluster = *(WORD*)&DiskInfo->FatData.FAT12[Cluster + Cluster / 2] & 0xFFF;
            }
            break;

        case DiskType::FAT16: Cluster = DiskInfo->FatData.FAT16[Cluster];
            break;
        case DiskType::FAT32: Cluster = DiskInfo->FatData.FAT32[Cluster] & 0xFFFFFFF;
            break;
        }
    }

    /* If too many iterations (infinite loop in FAT) then return nullptr. */
    if (MaxIterate >= DiskInfo->CountofClusters + 1) {
        jkGui->show_debug(DebugLevel::Progress, nullptr,
                         L"Infinite loop in FAT detected, perhaps the disk is corrupted.");

        return nullptr;
    }

    /* Allocate buffer. */
    if (BufferLength > UINT_MAX) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Directory is too big, %I64u bytes", BufferLength);

        return nullptr;
    }

    Buffer = (BYTE*)malloc((size_t)BufferLength);

    if (Buffer == nullptr) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        return nullptr;
    }

    /* Loop through the FAT cluster list and load all fragments from disk into the buffer. */
    BufferOffset = 0;
    Cluster = StartCluster;
    FirstCluster = Cluster;
    LastCluster = 0;

    for (MaxIterate = 0; MaxIterate < DiskInfo->CountofClusters + 1; MaxIterate++) {
        /* Exit the loop when we have reached the end of the cluster list. */
        if (Data->disk_.type_ == DiskType::FAT12 && Cluster >= 0xFF8) break;
        if (Data->disk_.type_ == DiskType::FAT16 && Cluster >= 0xFFF8) break;
        if (Data->disk_.type_ == DiskType::FAT32 && Cluster >= 0xFFFFFF8) break;

        /* Sanity check, test if the cluster is within the range of valid cluster numbers. */
        if (Cluster < 2) break;
        if (Cluster > DiskInfo->CountofClusters + 1) break;

        /* If this is a new fragment then load the previous fragment from disk. If not then
        add to the counters and continue. */
        if (Cluster != LastCluster + 1 && LastCluster != 0) {
            FragmentLength = (LastCluster - FirstCluster + 1) * DiskInfo->SectorsPerCluster * DiskInfo->BytesPerSector;
            Trans.QuadPart = (DiskInfo->FirstDataSector + (FirstCluster - 2) * DiskInfo->SectorsPerCluster) * DiskInfo->
                    BytesPerSector;

            gOverlapped.Offset = Trans.LowPart;
            gOverlapped.OffsetHigh = Trans.HighPart;
            gOverlapped.hEvent = nullptr;

            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                             L"Reading directory fragment, %I64u bytes at offset=%I64u.", FragmentLength,
                             Trans.QuadPart);

            Result = ReadFile(Data->disk_.volume_handle_, &Buffer[BufferOffset], (uint32_t)FragmentLength, &BytesRead,
                              &gOverlapped);

            if (Result == 0) {
                m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

                jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: %s", s1);

                free(Buffer);

                return nullptr;
            }

            //ShowHex(Data,Buffer,256);
            BufferOffset = BufferOffset + FragmentLength;
            FirstCluster = Cluster;
        }

        LastCluster = Cluster;

        /* Get next cluster from FAT. */
        switch (Data->disk_.type_) {
        case DiskType::FAT12:
            if ((Cluster & 1) == 1) {
                Cluster = *(WORD*)&DiskInfo->FatData.FAT12[Cluster + Cluster / 2] >> 4;
            }
            else {
                Cluster = *(WORD*)&DiskInfo->FatData.FAT12[Cluster + Cluster / 2] & 0xFFF;
            }

            break;
        case DiskType::FAT16: Cluster = DiskInfo->FatData.FAT16[Cluster];
            break;
        case DiskType::FAT32: Cluster = DiskInfo->FatData.FAT32[Cluster] & 0xFFFFFFF;
            break;
        }
    }

    /* Load the last fragment. */
    if (LastCluster != 0) {
        FragmentLength = (LastCluster - FirstCluster + 1) * DiskInfo->SectorsPerCluster * DiskInfo->BytesPerSector;
        Trans.QuadPart = (DiskInfo->FirstDataSector + (FirstCluster - 2) * DiskInfo->SectorsPerCluster) * DiskInfo->
                BytesPerSector;

        gOverlapped.Offset = Trans.LowPart;
        gOverlapped.OffsetHigh = Trans.HighPart;
        gOverlapped.hEvent = nullptr;

        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading directory fragment, %I64u bytes at offset=%I64u.", FragmentLength,
                         Trans.QuadPart);

        Result = ReadFile(Data->disk_.volume_handle_, &Buffer[BufferOffset], (uint32_t)FragmentLength, &BytesRead,
                          &gOverlapped);

        if (Result == 0) {
            m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: %s", s1);

            free(Buffer);

            return nullptr;
        }
    }

    if (OutLength != nullptr) *OutLength = BufferLength;

    return Buffer;
}

/* Analyze a directory and add all the items to the item tree. */
void JKScanFat::AnalyzeFatDirectory(DefragDataStruct* Data, FatDiskInfoStruct* DiskInfo, BYTE* Buffer,
                                    uint64_t Length, ItemStruct* ParentDirectory) {
    FatDirStruct* Dir;
    FatLongNameDirStruct* LDir;
    ItemStruct* Item;

    uint32_t Index;

    WCHAR ShortName[13];
    WCHAR LongName[820];

    int LastLongNameSection;

    UCHAR LongNameChecksum;

    BYTE* SubDirBuf;

    uint64_t SubDirLength;
    uint64_t StartCluster;

    WCHAR* p1;

    int i;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Sanity check. */
    if (Buffer == nullptr || Length == 0) return;

    /* Slow the program down to the percentage that was specified on the
    command line. */
    m_jkLib->slow_down(Data);

    //ShowHex(Data,Buffer,256);

    /* Walk through all the directory entries, extract the info, and store in memory
    in the ItemTree. */
    LastLongNameSection = 0;

    for (Index = 0; Index + 31 < Length; Index = Index + 32) {
        if (*Data->running_ != RunningState::RUNNING) break;

        Dir = (FatDirStruct*)&Buffer[Index];

        /* Ignore free (not used) entries. */
        if (Dir->DIR_Name[0] == 0xE5) {
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\tFree (not used)", Index / 32);

            continue;
        }

        /* Exit at the end of the directory. */
        if (Dir->DIR_Name[0] == 0) {
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\tFree (not used), end of directory.", Index / 32);

            break;
        }

        /* If this is a long filename component then save the string and loop. */
        if ((Dir->DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) {
            LDir = (FatLongNameDirStruct*)&Buffer[Index];

            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\tLong filename part.", Index / 32);

            i = LDir->LDIR_Ord & 0x3F;

            if (i == 0) {
                LastLongNameSection = 0;

                continue;
            }

            if ((LDir->LDIR_Ord & 0x40) == 0x40) {
                wmemset(LongName, L'\0', 820);

                LastLongNameSection = i;
                LongNameChecksum = LDir->LDIR_Chksum;
            }
            else {
                if (i + 1 != LastLongNameSection || LongNameChecksum != LDir->LDIR_Chksum) {
                    LastLongNameSection = 0;

                    continue;
                }

                LastLongNameSection = i;
            }

            i = (i - 1) * 13;

            LongName[i++] = LDir->LDIR_Name1[0];
            LongName[i++] = LDir->LDIR_Name1[1];
            LongName[i++] = LDir->LDIR_Name1[2];
            LongName[i++] = LDir->LDIR_Name1[3];
            LongName[i++] = LDir->LDIR_Name1[4];
            LongName[i++] = LDir->LDIR_Name2[0];
            LongName[i++] = LDir->LDIR_Name2[1];
            LongName[i++] = LDir->LDIR_Name2[2];
            LongName[i++] = LDir->LDIR_Name2[3];
            LongName[i++] = LDir->LDIR_Name2[4];
            LongName[i++] = LDir->LDIR_Name2[5];
            LongName[i++] = LDir->LDIR_Name3[0];
            LongName[i++] = LDir->LDIR_Name3[1];

            continue;
        }

        /* If we are here and the long filename counter is not 1 then something is wrong
        with the long filename. Ignore the long filename. */
        if (LastLongNameSection != 1) {
            LongName[0] = '\0';
        }
        else if (CalculateShortNameCheckSum(Dir->DIR_Name) != LongNameChecksum) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"%u.\tError: long filename is out of sync");
            LongName[0] = '\0';
        }

        LastLongNameSection = 0;

        /* Extract the short name. */
        for (i = 0; i < 8; i++) ShortName[i] = Dir->DIR_Name[i];

        for (i = 7; i > 0; i--) if (ShortName[i] != ' ') break;

        if (ShortName[i] != ' ') i++;

        ShortName[i] = '.';
        ShortName[i + 1] = Dir->DIR_Name[8];
        ShortName[i + 2] = Dir->DIR_Name[9];
        ShortName[i + 3] = Dir->DIR_Name[10];

        if (ShortName[i + 3] != ' ') {
            ShortName[i + 4] = '\0';
        }
        else if (ShortName[i + 2] != ' ') {
            ShortName[i + 3] = '\0';
        }
        else if (ShortName[i + 1] != ' ') {
            ShortName[i + 2] = '\0';
        }
        else {
            ShortName[i] = '\0';
        }
        if (ShortName[0] == 0x05) ShortName[0] = 0xE5;

        /* If this is a VolumeID then loop. We have no use for it. */
        if ((Dir->DIR_Attr & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == ATTR_VOLUME_ID) {
            p1 = wcschr(ShortName, L'.');

            if (p1 != nullptr) wcscpy_s(p1, wcslen(p1), p1 + 1);

            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\t'%s' (volume ID)", Index / 32, ShortName);

            continue;
        }

        if ((Dir->DIR_Attr & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == (ATTR_DIRECTORY | ATTR_VOLUME_ID)) {
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\tInvalid directory entry");

            continue;
        }

        /* Ignore "." and "..". */
        if (wcscmp(ShortName, L".") == 0) {
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\t'.'", Index / 32);

            continue;
        }

        if (wcscmp(ShortName, L"..") == 0) {
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\t'..'", Index / 32);

            continue;
        }

        /* Create and fill a new item record in memory. */
        Item = (ItemStruct*)malloc(sizeof(ItemStruct));

        if (Item == nullptr) {
            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");
            break;
        }

        if (wcscmp(ShortName, L".") == 0) {
            Item->short_filename_ = nullptr;
        }
        else {
            Item->short_filename_ = _wcsdup(ShortName);
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"%u.\t'%s'", Index / 32, ShortName);
        }

        if (LongName[0] == '\0') {
            Item->long_filename_ = nullptr;
        }
        else {
            Item->long_filename_ = _wcsdup(LongName);
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"\tLong filename = '%s'", LongName);
        }

        if (Item->long_filename_ != nullptr &&
            Item->short_filename_ != nullptr &&
            _wcsicmp(Item->long_filename_, Item->short_filename_) == 0) {
            free(Item->short_filename_);

            Item->short_filename_ = Item->long_filename_;
        }

        if (Item->long_filename_ == nullptr && Item->short_filename_ != nullptr) Item->long_filename_ = Item->
                short_filename_;
        if (Item->long_filename_ != nullptr && Item->short_filename_ == nullptr) Item->short_filename_ = Item->
                long_filename_;

        Item->short_path_ = nullptr;
        Item->long_path_ = nullptr;
        Item->bytes_ = Dir->DIR_FileSize;

        if (Data->disk_.type_ == DiskType::FAT32) {
            StartCluster = MAKELONG(Dir->DIR_FstClusLO, Dir->DIR_FstClusHI);
        }
        else {
            StartCluster = Dir->DIR_FstClusLO;
        }

        make_fragment_list(Data, DiskInfo, Item, StartCluster);

        Item->creation_time_ = ConvertTime(Dir->DIR_CrtDate, Dir->DIR_CrtTime, Dir->DIR_CrtTimeTenth);
        Item->mft_change_time_ = ConvertTime(Dir->DIR_WrtDate, Dir->DIR_WrtTime, 0);
        Item->last_access_time_ = ConvertTime(Dir->DIR_LstAccDate, 0, 0);
        Item->parent_inode_ = 0;
        Item->parent_directory_ = ParentDirectory;
        Item->is_dir_ = false;

        if ((Dir->DIR_Attr & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == ATTR_DIRECTORY) Item->is_dir_ = true;

        Item->is_unmovable_ = false;
        Item->is_excluded_ = false;
        Item->is_hog_ = false;

        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"\tSize = %I64u clusters, %I64u bytes", Item->clusters_count_, Item->bytes_);

        /* Add the item record to the sorted item tree in memory. */
        m_jkLib->tree_insert(Data, Item);

        /* Draw the item on the screen. */
        jkGui->show_analyze(Data, Item);
        //		if (*Data->RedrawScreen == false) {
        m_jkLib->colorize_item(Data, Item, 0, 0, false);
        //		} else {
        //			ShowDiskmap(Data);
        //		}

        /* Increment counters. */
        if (Item->is_dir_ == true) {
            Data->count_directories_ = Data->count_directories_ + 1;
        }

        Data->count_all_files_ = Data->count_all_files_ + 1;
        Data->count_all_bytes_ = Data->count_all_bytes_ + Item->bytes_;
        Data->count_all_clusters_ = Data->count_all_clusters_ + Item->clusters_count_;

        if (m_jkLib->fragment_count(Item) > 1) {
            Data->count_fragmented_items_ = Data->count_fragmented_items_ + 1;
            Data->count_fragmented_bytes_ = Data->count_fragmented_bytes_ + Item->bytes_;
            Data->count_fragmented_clusters_ = Data->count_fragmented_clusters_ + Item->clusters_count_;
        }

        /* If this is a directory then iterate. */
        if (Item->is_dir_ == true) {
            SubDirBuf = LoadDirectory(Data, DiskInfo, StartCluster, &SubDirLength);

            AnalyzeFatDirectory(Data, DiskInfo, SubDirBuf, SubDirLength, Item);

            free(SubDirBuf);

            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Finished with subdirectory.");
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
BOOL JKScanFat::AnalyzeFatVolume(DefragDataStruct* Data) {
    FatBootSectorStruct BootSector;
    FatDiskInfoStruct DiskInfo;

    ULARGE_INTEGER Trans;

    OVERLAPPED gOverlapped;

    DWORD BytesRead;

    size_t FatSize;

    BYTE* RootDirectory;

    uint64_t RootStart;
    uint64_t RootLength;

    int Result;

    WCHAR s1[BUFSIZ];

    char s2[BUFSIZ];

    DefragGui* jkGui = DefragGui::get_instance();

    /* Read the boot block from the disk. */
    gOverlapped.Offset = 0;
    gOverlapped.OffsetHigh = 0;
    gOverlapped.hEvent = nullptr;

    Result = ReadFile(Data->disk_.volume_handle_, &BootSector, sizeof(FatBootSectorStruct), &BytesRead,
                      &gOverlapped);

    if (Result == 0 || BytesRead != 512) {
        m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

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
        Data->disk_.type_ = DiskType::FAT12;

        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"This is a FAT12 disk.");
    }
    else if (DiskInfo.CountofClusters < 65525) {
        Data->disk_.type_ = DiskType::FAT16;

        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"This is a FAT16 disk.");
    }
    else {
        Data->disk_.type_ = DiskType::FAT32;

        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"This is a FAT32 disk.");
    }

    Data->bytes_per_cluster_ = DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster;
    Data->total_clusters_ = DiskInfo.CountofClusters;

    /* Output debug information. */
    strncpy_s(s2,BUFSIZ, (char*)&BootSector.BS_OEMName[0], 8);

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

    if (Data->disk_.type_ != DiskType::FAT32) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BS_DrvNum: %u", BootSector.Fat16.BS_DrvNum);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BS_BootSig: %u", BootSector.Fat16.BS_BootSig);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BS_VolID: %u", BootSector.Fat16.BS_VolID);

        strncpy_s(s2,BUFSIZ, (char*)&BootSector.Fat16.BS_VolLab[0], 11);

        s2[11] = '\0';

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  VolLab: %S", s2);

        strncpy_s(s2,BUFSIZ, (char*)&BootSector.Fat16.BS_FilSysType[0], 8);

        s2[8] = '\0';

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FilSysType: %S", s2);
    }
    else {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FATSz32: %lu", BootSector.Fat32.BPB_FATSz32);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  ExtFlags: %lu", BootSector.Fat32.BPB_ExtFlags);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FSVer: %lu", BootSector.Fat32.BPB_FSVer);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  RootClus: %lu", BootSector.Fat32.BPB_RootClus);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FSInfo: %lu", BootSector.Fat32.BPB_FSInfo);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BkBootSec: %lu", BootSector.Fat32.BPB_BkBootSec);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  DrvNum: %lu", BootSector.Fat32.BS_DrvNum);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BootSig: %lu", BootSector.Fat32.BS_BootSig);
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  VolID: %lu", BootSector.Fat32.BS_VolID);

        strncpy_s(s2,BUFSIZ, (char*)&BootSector.Fat32.BS_VolLab[0], 11);

        s2[11] = '\0';

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  VolLab: %S", s2);

        strncpy_s(s2,BUFSIZ, (char*)&BootSector.Fat32.BS_FilSysType[0], 8);

        s2[8] = '\0';

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  FilSysType: %S", s2);
    }

    /* Read the FAT from disk into memory. */
    switch (Data->disk_.type_) {
    case DiskType::FAT12: FatSize = (size_t)(DiskInfo.CountofClusters + 1 + (DiskInfo.CountofClusters + 1) / 2);
        break;
    case DiskType::FAT16: FatSize = (size_t)((DiskInfo.CountofClusters + 1) * 2);
        break;
    case DiskType::FAT32: FatSize = (size_t)((DiskInfo.CountofClusters + 1) * 4);
        break;
    }

    if (FatSize % DiskInfo.BytesPerSector > 0) {
        FatSize = (size_t)(FatSize + DiskInfo.BytesPerSector - FatSize % DiskInfo.BytesPerSector);
    }

    DiskInfo.FatData.FAT12 = (BYTE*)malloc(FatSize);

    if (DiskInfo.FatData.FAT12 == nullptr) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        return FALSE;
    }

    Trans.QuadPart = BootSector.BPB_RsvdSecCnt * DiskInfo.BytesPerSector;
    gOverlapped.Offset = Trans.LowPart;
    gOverlapped.OffsetHigh = Trans.HighPart;
    gOverlapped.hEvent = nullptr;

    jkGui->show_debug(DebugLevel::Progress, nullptr, L"Reading FAT, %lu bytes at offset=%I64u", FatSize, Trans.QuadPart);

    Result = ReadFile(Data->disk_.volume_handle_, DiskInfo.FatData.FAT12, (uint32_t)FatSize, &BytesRead, &gOverlapped);

    if (Result == 0) {
        m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: %s", s1);

        return FALSE;
    }

    //ShowHex(Data,DiskInfo.FatData.FAT12,32);

    /* Read the root directory from disk into memory. */
    if (Data->disk_.type_ == DiskType::FAT32) {
        RootDirectory = LoadDirectory(Data, &DiskInfo, BootSector.Fat32.BPB_RootClus, &RootLength);
    }
    else {
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
        BytesRead = (uint32_t)RootLength;

        if (RootLength % DiskInfo.BytesPerSector > 0) {
            BytesRead = (uint32_t)(RootLength + DiskInfo.BytesPerSector - RootLength % DiskInfo.BytesPerSector);
        }

        /* Allocate buffer. */
        RootDirectory = (BYTE*)malloc(BytesRead);
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

        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading root directory, %lu bytes at offset=%I64u.", BytesRead, Trans.QuadPart);

        Result = ReadFile(Data->disk_.volume_handle_, RootDirectory, BytesRead, &BytesRead, &gOverlapped);

        if (Result == 0) {
            m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: %s", s1);

            free(DiskInfo.FatData.FAT12);
            free(RootDirectory);

            return FALSE;
        }
    }

    /* Analyze all the items in the root directory and add to the item tree. */
    AnalyzeFatDirectory(Data, &DiskInfo, RootDirectory, RootLength, nullptr);

    /* Cleanup. */
    free(RootDirectory);
    free(DiskInfo.FatData.FAT12);

    return TRUE;
}
