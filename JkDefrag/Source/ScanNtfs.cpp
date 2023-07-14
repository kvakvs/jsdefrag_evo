#include "StdAfx.h"

/*
#include "JkDefragLib.h"
#include "JKDefragStruct.h"
#include "JKDefragLog.h"
#include "JkDefragGui.h"
#include "ScanNtfs.h"
*/

ScanNtfs* ScanNtfs::m_jkScanNtfs = 0;

ScanNtfs::ScanNtfs() {
    //	m_jkGui = JKDefragGui::getInstance();
}

ScanNtfs::~ScanNtfs() {
    delete m_jkScanNtfs;
}

ScanNtfs* ScanNtfs::getInstance() {
    if (m_jkScanNtfs == nullptr) {
        m_jkScanNtfs = new ScanNtfs();
    }

    return m_jkScanNtfs;
}

WCHAR* ScanNtfs::StreamTypeNames(ATTRIBUTE_TYPE StreamType) {
    switch (StreamType) {
    case AttributeStandardInformation: return (L"$STANDARD_INFORMATION");
    case AttributeAttributeList: return (L"$ATTRIBUTE_LIST");
    case AttributeFileName: return (L"$FILE_NAME");
    case AttributeObjectId: return (L"$OBJECT_ID");
    case AttributeSecurityDescriptor: return (L"$SECURITY_DESCRIPTOR");
    case AttributeVolumeName: return (L"$VOLUME_NAME");
    case AttributeVolumeInformation: return (L"$VOLUME_INFORMATION");
    case AttributeData: return (L"$DATA");
    case AttributeIndexRoot: return (L"$INDEX_ROOT");
    case AttributeIndexAllocation: return (L"$INDEX_ALLOCATION");
    case AttributeBitmap: return (L"$BITMAP");
    case AttributeReparsePoint: return (L"$REPARSE_POINT");
    case AttributeEAInformation: return (L"$EA_INFORMATION");
    case AttributeEA: return (L"$EA");
    case AttributePropertySet: return (L"$PROPERTY_SET"); /* guess, not documented */
    case AttributeLoggedUtilityStream: return (L"$LOGGED_UTILITY_STREAM");
    default: return (L"");
    }
}

/*

Fixup the raw MFT data that was read from disk. Return TRUE if everything is ok,
FALSE if the MFT data is corrupt (this can also happen when we have read a
record past the end of the MFT, maybe it has shrunk while we were processing).

- To protect against disk failure, the last 2 bytes of every sector in the MFT are
not stored in the sector itself, but in the "Usa" array in the header (described
by UsaOffset and UsaCount). The last 2 bytes are copied into the array and the
Update Sequence Number is written in their place.

- The Update Sequence Number is stored in the first item (item zero) of the "Usa"
array.

- The number of bytes per sector is defined in the $Boot record.

*/

BOOL ScanNtfs::FixupRawMftdata(struct DefragDataStruct* Data, struct NtfsDiskInfoStruct* DiskInfo, BYTE* Buffer,
                               uint64_t BufLength) {
    struct NTFS_RECORD_HEADER* RecordHeader;

    WORD* BufferW;
    WORD* UpdateSequenceArray;
    uint32_t Index;
    uint32_t Increment;

    USHORT i;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Sanity check. */
    if (Buffer == nullptr) return (FALSE);
    if (BufLength < sizeof(struct NTFS_RECORD_HEADER)) return (FALSE);

    /* If this is not a FILE record then return FALSE. */
    if (memcmp(Buffer, "FILE", 4) != 0) {
        jkGui->show_debug(
            DebugLevel::Progress, nullptr,
            L"This is not a valid MFT record, it does not begin with FILE (maybe trying to read past the end?).");

        m_jkLib->show_hex(Data, Buffer, BufLength);

        return (FALSE);
    }

    /* Walk through all the sectors and restore the last 2 bytes with the value
    from the Usa array. If we encounter bad sector data then return with
    FALSE. */
    BufferW = (WORD*)Buffer;

    RecordHeader = (struct NTFS_RECORD_HEADER*)Buffer;

    UpdateSequenceArray = (WORD*)&Buffer[RecordHeader->UsaOffset];

    Increment = (uint32_t)(DiskInfo->BytesPerSector / sizeof(USHORT));

    Index = Increment - 1;

    for (i = 1; i < RecordHeader->UsaCount; i++) {
        /* Check if we are inside the buffer. */
        if (Index * sizeof(WORD) >= BufLength) {
            jkGui->show_debug(DebugLevel::Progress, nullptr,
                             L"Warning: USA data indicates that data is missing, the MFT may be corrupt.");
        }

        /* Check if the last 2 bytes of the sector contain the Update Sequence Number.
        If not then return FALSE. */
        if (BufferW[Index] != UpdateSequenceArray[0]) {
            jkGui->show_debug(
                DebugLevel::Progress, nullptr,
                L"Error: USA fixup word is not equal to the Update Sequence Number, the MFT may be corrupt.");

            return (FALSE);
        }

        /* Replace the last 2 bytes in the sector with the value from the Usa array. */
        BufferW[Index] = UpdateSequenceArray[i];
        Index = Index + Increment;
    }

    return (TRUE);
}

/*

Read the data that is specified in a RunData list from disk into memory,
skipping the first Offset bytes. Return a malloc'ed buffer with the data,
or nullptr if error.
Note: The caller must free() the buffer.

*/
BYTE* ScanNtfs::ReadNonResidentData(
    struct DefragDataStruct* Data,
    struct NtfsDiskInfoStruct* DiskInfo,
    BYTE* RunData,
    uint32_t RunDataLength,
    uint64_t Offset, /* Bytes to skip from begin of data. */
    uint64_t WantedLength) /* Number of bytes to read. */
{
    uint32_t Index;

    BYTE* Buffer;

    int64_t Lcn;
    int64_t Vcn;

    int RunOffsetSize;
    int RunLengthSize;

    union UlongBytes {
        struct {
            BYTE Bytes[8];
        };

        int64_t Value;
    } RunOffset, RunLength;

    uint64_t ExtentVcn;
    uint64_t ExtentLcn;
    uint64_t ExtentLength;

    OVERLAPPED gOverlapped;

    ULARGE_INTEGER Trans;

    DWORD BytesRead;

    errno_t Result;

    WCHAR s1[BUFSIZ];

    int i;

    DefragGui* jkGui = DefragGui::get_instance();

    jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Reading %I64u bytes from offset %I64u",
                     WantedLength, Offset);

    /* Sanity check. */
    if ((RunData == nullptr) || (RunDataLength == 0)) return (nullptr);
    if (WantedLength >= INT_MAX) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"    Cannot read %I64u bytes, maximum is %lu.", WantedLength,
                         INT_MAX);

        return (nullptr);
    }

    /* We have to round up the WantedLength to the nearest sector. For some
    reason or other Microsoft has decided that raw reading from disk can
    only be done by whole sector, even though ReadFile() accepts it's
    parameters in bytes. */
    if (WantedLength % DiskInfo->BytesPerSector > 0) {
        WantedLength = WantedLength + DiskInfo->BytesPerSector - WantedLength % DiskInfo->BytesPerSector;
    }

    /* Allocate the data buffer. Clear the buffer with zero's in case of sparse
    content. */
    Buffer = (BYTE*)malloc((size_t)WantedLength);

    if (Buffer == nullptr) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        return (nullptr);
    }

    memset(Buffer, 0, (size_t)WantedLength);

    /* Walk through the RunData and read the requested data from disk. */
    Index = 0;
    Lcn = 0;
    Vcn = 0;

    while (RunData[Index] != 0) {
        /* Decode the RunData and calculate the next Lcn. */
        RunLengthSize = (RunData[Index] & 0x0F);
        RunOffsetSize = ((RunData[Index] & 0xF0) >> 4);

        Index++;

        if (Index >= RunDataLength) {
            jkGui->show_debug(DebugLevel::Progress, nullptr,
                             L"Error: datarun is longer than buffer, the MFT may be corrupt.");

            return (FALSE);
        }

        RunLength.Value = 0;

        for (i = 0; i < RunLengthSize; i++) {
            RunLength.Bytes[i] = RunData[Index];

            Index++;

            if (Index >= RunDataLength) {
                jkGui->show_debug(DebugLevel::Progress, nullptr,
                                 L"Error: datarun is longer than buffer, the MFT may be corrupt.");

                return (FALSE);
            }
        }

        RunOffset.Value = 0;

        for (i = 0; i < RunOffsetSize; i++) {
            RunOffset.Bytes[i] = RunData[Index];

            Index++;

            if (Index >= RunDataLength) {
                jkGui->show_debug(DebugLevel::Progress, nullptr,
                                 L"Error: datarun is longer than buffer, the MFT may be corrupt.");

                return (FALSE);
            }
        }

        if (RunOffset.Bytes[i - 1] >= 0x80) while (i < 8) RunOffset.Bytes[i++] = 0xFF;

        Lcn = Lcn + RunOffset.Value;
        Vcn = Vcn + RunLength.Value;

        /* Ignore virtual extents. */
        if (RunOffset.Value == 0) continue;

        /* I don't think the RunLength can ever be zero, but just in case. */
        if (RunLength.Value == 0) continue;

        /* Determine how many and which bytes we want to read. If we don't need
        any bytes from this extent then loop. */

        ExtentVcn = (Vcn - RunLength.Value) * DiskInfo->BytesPerSector * DiskInfo->SectorsPerCluster;
        ExtentLcn = Lcn * DiskInfo->BytesPerSector * DiskInfo->SectorsPerCluster;

        ExtentLength = RunLength.Value * DiskInfo->BytesPerSector * DiskInfo->SectorsPerCluster;

        if (Offset >= ExtentVcn + ExtentLength) continue;

        if (Offset > ExtentVcn) {
            ExtentLcn = ExtentLcn + Offset - ExtentVcn;
            ExtentLength = ExtentLength - (Offset - ExtentVcn);
            ExtentVcn = Offset;
        }

        if (Offset + WantedLength <= ExtentVcn) continue;

        if (Offset + WantedLength < ExtentVcn + ExtentLength) {
            ExtentLength = Offset + WantedLength - ExtentVcn;
        }

        if (ExtentLength == 0) continue;

        /* Read the data from the disk. If error then return FALSE. */
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                         L"    Reading %I64u bytes from Lcn=%I64u into offset=%I64u",
                         ExtentLength, ExtentLcn / (DiskInfo->BytesPerSector * DiskInfo->SectorsPerCluster),
                         ExtentVcn - Offset);

        Trans.QuadPart = ExtentLcn;

        gOverlapped.Offset = Trans.LowPart;
        gOverlapped.OffsetHigh = Trans.HighPart;
        gOverlapped.hEvent = nullptr;

        Result = ReadFile(Data->disk_.volume_handle_, &Buffer[ExtentVcn - Offset], (uint32_t)ExtentLength, &BytesRead,
                          &gOverlapped);

        if (Result == 0) {
            m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error while reading disk: %s", s1);

            free(Buffer);

            return (nullptr);
        }
    }

    /* Return the buffer. */
    return (Buffer);
}

/* Read the RunData list and translate into a list of fragments. */
BOOL ScanNtfs::TranslateRundataToFragmentlist(
    struct DefragDataStruct* Data,
    struct InodeDataStruct* InodeData,
    WCHAR* StreamName,
    ATTRIBUTE_TYPE StreamType,
    BYTE* RunData,
    uint32_t RunDataLength,
    uint64_t StartingVcn,
    uint64_t Bytes) {
    struct StreamStruct* Stream;

    uint32_t Index;

    int64_t Lcn;
    int64_t Vcn;

    int RunOffsetSize;
    int RunLengthSize;

    union UlongBytes {
        struct {
            BYTE Bytes[8];
        };

        int64_t Value;
    } RunOffset, RunLength;

    struct FragmentListStruct* NewFragment;
    struct FragmentListStruct* LastFragment;

    int i;

    DefragGui* gui = DefragGui::get_instance();

    /* Sanity check. */
    if ((Data == nullptr) || (InodeData == nullptr)) return (FALSE);

    /* Find the stream in the list of streams. If not found then create a new stream. */
    for (Stream = InodeData->Streams; Stream != nullptr; Stream = Stream->Next) {
        if (Stream->StreamType != StreamType) continue;
        if ((StreamName == nullptr) && (Stream->StreamName == nullptr)) break;
        if ((StreamName != nullptr) && (Stream->StreamName != nullptr) &&
            (wcscmp(Stream->StreamName, StreamName) == 0))
            break;
    }

    if (Stream == nullptr) {
        if (StreamName != nullptr) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Creating new stream: '%s:%s'",
                           StreamName, StreamTypeNames(StreamType));
        }
        else {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Creating new stream: ':%s'",
                           StreamTypeNames(StreamType));
        }

        Stream = (struct StreamStruct*)malloc(sizeof(struct StreamStruct));

        if (Stream == nullptr) {
            gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

            return (FALSE);
        }

        Stream->Next = InodeData->Streams;

        InodeData->Streams = Stream;

        Stream->StreamName = nullptr;

        if ((StreamName != nullptr) && (wcslen(StreamName) > 0)) {
            Stream->StreamName = _wcsdup(StreamName);
        }

        Stream->StreamType = StreamType;
        Stream->Fragments = nullptr;
        Stream->Clusters = 0;
        Stream->Bytes = Bytes;
    }
    else {
        if (StreamName != nullptr) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Appending rundata to existing stream: '%s:%s",
                           StreamName, StreamTypeNames(StreamType));
        }
        else {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Appending rundata to existing stream: ':%s",
                           StreamTypeNames(StreamType));
        }

        if (Stream->Bytes == 0) Stream->Bytes = Bytes;
    }

    /* If the stream already has a list of fragments then find the last fragment. */
    LastFragment = Stream->Fragments;

    if (LastFragment != nullptr) {
        while (LastFragment->next_ != nullptr) LastFragment = LastFragment->next_;

        if (StartingVcn != LastFragment->next_vcn_) {
            gui->show_debug(
                DebugLevel::Progress, nullptr,
                L"Error: Inode %I64u already has a list of fragments. LastVcn=%I64u, StartingVCN=%I64u",
                InodeData->Inode, LastFragment->next_vcn_, StartingVcn);

            return (FALSE);
        }
    }

    /* Walk through the RunData and add the extents. */
    Index = 0;

    Lcn = 0;

    Vcn = StartingVcn;

    if (RunData != nullptr)
        while (RunData[Index] != 0) {
            /* Decode the RunData and calculate the next Lcn. */
            RunLengthSize = (RunData[Index] & 0x0F);
            RunOffsetSize = ((RunData[Index] & 0xF0) >> 4);

            Index++;

            if (Index >= RunDataLength) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                               L"Error: datarun is longer than buffer, the MFT may be corrupt.",
                               InodeData->Inode);
                return (FALSE);
            }

            RunLength.Value = 0;

            for (i = 0; i < RunLengthSize; i++) {
                RunLength.Bytes[i] = RunData[Index];

                Index++;

                if (Index >= RunDataLength) {
                    gui->show_debug(DebugLevel::Progress, nullptr,
                                   L"Error: datarun is longer than buffer, the MFT may be corrupt.",
                                   InodeData->Inode);

                    return (FALSE);
                }
            }

            RunOffset.Value = 0;

            for (i = 0; i < RunOffsetSize; i++) {
                RunOffset.Bytes[i] = RunData[Index];

                Index++;

                if (Index >= RunDataLength) {
                    gui->show_debug(DebugLevel::Progress, nullptr,
                                   L"Error: datarun is longer than buffer, the MFT may be corrupt.",
                                   InodeData->Inode);

                    return (FALSE);
                }
            }

            if (RunOffset.Bytes[i - 1] >= 0x80) while (i < 8) RunOffset.Bytes[i++] = 0xFF;

            Lcn = Lcn + RunOffset.Value;

            Vcn = Vcn + RunLength.Value;

            /* Show debug message. */
            if (RunOffset.Value != 0) {
                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                               L"    Extent: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u", Lcn,
                               Vcn - RunLength.Value, Vcn);
            }
            else {
                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                               L"    Extent (virtual): Vcn=%I64u, NextVcn=%I64u", Vcn - RunLength.Value,
                               Vcn);
            }

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */

            if (RunOffset.Value != 0) {
                Stream->Clusters = Stream->Clusters + RunLength.Value;
            }

            /* Add the extent to the Fragments. */
            NewFragment = (struct FragmentListStruct*)malloc(sizeof(struct FragmentListStruct));

            if (NewFragment == nullptr) {
                gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

                return (FALSE);
            }

            NewFragment->lcn_ = Lcn;

            if (RunOffset.Value == 0) NewFragment->lcn_ = VIRTUALFRAGMENT;

            NewFragment->next_vcn_ = Vcn;
            NewFragment->next_ = nullptr;

            if (Stream->Fragments == nullptr) {
                Stream->Fragments = NewFragment;
            }
            else {
                if (LastFragment != nullptr) LastFragment->next_ = NewFragment;
            }

            LastFragment = NewFragment;
        }

    return (TRUE);
}

/*

Cleanup the Streams data in an InodeData struct. If CleanFragments is TRUE then
also cleanup the fragments.

*/
void ScanNtfs::CleanupStreams(struct InodeDataStruct* InodeData, BOOL CleanupFragments) {
    struct StreamStruct* Stream;
    struct StreamStruct* TempStream;

    struct FragmentListStruct* Fragment;
    struct FragmentListStruct* TempFragment;

    Stream = InodeData->Streams;

    while (Stream != nullptr) {
        if (Stream->StreamName != nullptr) free(Stream->StreamName);

        if (CleanupFragments == TRUE) {
            Fragment = Stream->Fragments;

            while (Fragment != nullptr) {
                TempFragment = Fragment;
                Fragment = Fragment->next_;

                free(TempFragment);
            }
        }

        TempStream = Stream;
        Stream = Stream->Next;

        free(TempStream);
    }

    InodeData->Streams = nullptr;
}

/* Construct the full stream name from the filename, the stream name, and the stream type. */
WCHAR* ScanNtfs::ConstructStreamName(WCHAR* FileName1, WCHAR* FileName2, struct StreamStruct* Stream) {
    WCHAR* FileName;
    WCHAR* StreamName;

    ATTRIBUTE_TYPE StreamType;

    size_t Length;

    WCHAR* p1;

    FileName = FileName1;

    if ((FileName == nullptr) || (wcslen(FileName) == 0)) FileName = FileName2;
    if ((FileName != nullptr) && (wcslen(FileName) == 0)) FileName = nullptr;

    StreamName = nullptr;
    StreamType = AttributeInvalid;

    if (Stream != nullptr) {
        StreamName = Stream->StreamName;

        if ((StreamName != nullptr) && (wcslen(StreamName) == 0)) StreamName = nullptr;

        StreamType = Stream->StreamType;
    }

    /* If the StreamName is empty and the StreamType is Data then return only the
    FileName. The Data stream is the default stream of regular files. */
    if (((StreamName == nullptr) || (wcslen(StreamName) == 0)) && (StreamType == AttributeData)) {
        if ((FileName == nullptr) || (wcslen(FileName) == 0)) return (nullptr);

        return (_wcsdup(FileName));
    }

    /* If the StreamName is "$I30" and the StreamType is AttributeIndexAllocation then
    return only the FileName. This must be a directory, and the Microsoft defragmentation
    API will automatically select this stream. */
    if ((StreamName != nullptr) &&
        (wcscmp(StreamName, L"$I30") == 0) &&
        (StreamType == AttributeIndexAllocation)) {
        if ((FileName == nullptr) || (wcslen(FileName) == 0)) return (nullptr);

        return (_wcsdup(FileName));
    }

    /* If the StreamName is empty and the StreamType is Data then return only the
    FileName. The Data stream is the default stream of regular files. */
    if (((StreamName == nullptr) || (wcslen(StreamName) == 0)) &&
        (wcslen(StreamTypeNames(StreamType)) == 0)) {
        if ((FileName == nullptr) || (wcslen(FileName) == 0)) return (nullptr);

        return (_wcsdup(FileName));
    }

    Length = 3;

    if (FileName != nullptr) Length = Length + wcslen(FileName);
    if (StreamName != nullptr) Length = Length + wcslen(StreamName);

    Length = Length + wcslen(StreamTypeNames(StreamType));

    if (Length == 3) return (nullptr);

    p1 = (WCHAR*)malloc(sizeof(WCHAR) * Length);

    if (p1 == nullptr) return (nullptr);

    *p1 = 0;

    if (FileName != nullptr) wcscat_s(p1, Length, FileName);

    wcscat_s(p1, Length, L":");

    if (StreamName != nullptr) wcscat_s(p1, Length, StreamName);

    wcscat_s(p1, Length, L":");
    wcscat_s(p1, Length, StreamTypeNames(StreamType));

    return (p1);
}

/* Forward declaration for recursion. */
/*
	BOOL ProcessAttributes(
	struct DefragDataStruct *Data,
	struct NtfsDiskInfoStruct *DiskInfo,
	struct InodeDataStruct *InodeData,
		BYTE *Buffer,
		uint64_t BufLength,
		USHORT Instance,
		int Depth);*/

/*

Process a list of attributes and store the gathered information in the Item
struct. Return FALSE if an error occurred.

*/
void ScanNtfs::ProcessAttributeList(
    struct DefragDataStruct* Data,
    struct NtfsDiskInfoStruct* DiskInfo,
    struct InodeDataStruct* InodeData,
    BYTE* Buffer,
    uint64_t BufLength,
    int Depth) {
    BYTE* Buffer2;

    struct ATTRIBUTE_LIST* Attribute;

    ULONG AttributeOffset;

    struct FILE_RECORD_HEADER* FileRecordHeader;
    struct FragmentListStruct* Fragment;

    uint64_t RefInode;
    uint64_t BaseInode;
    uint64_t Vcn;
    uint64_t RealVcn;
    uint64_t RefInodeVcn;

    OVERLAPPED gOverlapped;

    ULARGE_INTEGER Trans;

    DWORD BytesRead;

    int Result;

    WCHAR* p1;
    WCHAR s1[BUFSIZ];

    DefragGui* jkGui = DefragGui::get_instance();

    /* Sanity checks. */
    if ((Buffer == nullptr) || (BufLength == 0)) return;

    if (Depth > 1000) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: infinite attribute loop, the MFT may be corrupt.");

        return;
    }

    jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Processing AttributeList for Inode %I64u, %u bytes",
                     InodeData->Inode,
                     BufLength);

    /* Walk through all the attributes and gather information. */
    for (AttributeOffset = 0; AttributeOffset < BufLength; AttributeOffset = AttributeOffset + Attribute->Length) {
        Attribute = (struct ATTRIBUTE_LIST*)&Buffer[AttributeOffset];

        /* Exit if no more attributes. AttributeLists are usually not closed by the
        0xFFFFFFFF endmarker. Reaching the end of the buffer is therefore normal and
        not an error. */
        if (AttributeOffset + 3 > BufLength) break;
        if (*(ULONG*)Attribute == 0xFFFFFFFF) break;
        if (Attribute->Length < 3) break;
        if (AttributeOffset + Attribute->Length > BufLength) break;

        /* Extract the referenced Inode. If it's the same as the calling Inode then ignore
        (if we don't ignore then the program will loop forever, because for some
        reason the info in the calling Inode is duplicated here...). */
        RefInode = (uint64_t)Attribute->FileReferenceNumber.InodeNumberLowPart +
                ((uint64_t)Attribute->FileReferenceNumber.InodeNumberHighPart << 32);

        if (RefInode == InodeData->Inode) continue;

        /* Show debug message. */
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    List attribute: %s",
                         StreamTypeNames(Attribute->AttributeType));
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                         L"      LowestVcn = %I64u, RefInode = %I64u, InodeSequence = %u, Instance = %u",
                         Attribute->LowestVcn, RefInode, Attribute->FileReferenceNumber.SequenceNumber,
                         Attribute->Instance);

        /* Extract the streamname. I don't know why AttributeLists can have names, and
        the name is not used further down. It is only extracted for debugging purposes.
        */
        if (Attribute->NameLength > 0) {
            p1 = (WCHAR*)malloc(sizeof(WCHAR) * (Attribute->NameLength + 1));

            if (p1 == nullptr) {
                jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

                return;
            }

            wcsncpy_s(p1, Attribute->NameLength + 1,
                      (WCHAR*)&Buffer[AttributeOffset + Attribute->NameOffset], Attribute->NameLength);

            p1[Attribute->NameLength] = 0;

            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"      AttributeList name = '%s'", p1);

            free(p1);
        }

        /* Find the fragment in the MFT that contains the referenced Inode. */
        Vcn = 0;
        RealVcn = 0;
        RefInodeVcn = RefInode * DiskInfo->BytesPerMftRecord / (DiskInfo->BytesPerSector * DiskInfo->SectorsPerCluster);

        for (Fragment = InodeData->MftDataFragments; Fragment != nullptr; Fragment = Fragment->next_) {
            if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                if ((RefInodeVcn >= RealVcn) && (RefInodeVcn < RealVcn + Fragment->next_vcn_ - Vcn)) {
                    break;
                }

                RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
            }

            Vcn = Fragment->next_vcn_;
        }

        if (Fragment == nullptr) {
            jkGui->show_debug(
                DebugLevel::DetailedGapFinding, nullptr,
                L"      Error: Inode %I64u is an extension of Inode %I64u, but does not exist (outside the MFT).",
                RefInode, InodeData->Inode);

            continue;
        }

        /* Fetch the record of the referenced Inode from disk. */
        Buffer2 = (BYTE*)malloc((size_t)DiskInfo->BytesPerMftRecord);

        if (Buffer2 == nullptr) {
            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

            return;
        }

        Trans.QuadPart = (Fragment->lcn_ - RealVcn) * DiskInfo->BytesPerSector *
                DiskInfo->SectorsPerCluster + RefInode * DiskInfo->BytesPerMftRecord;

        gOverlapped.Offset = Trans.LowPart;
        gOverlapped.OffsetHigh = Trans.HighPart;
        gOverlapped.hEvent = nullptr;

        Result = ReadFile(Data->disk_.volume_handle_, Buffer2, (uint32_t)DiskInfo->BytesPerMftRecord, &BytesRead,
                          &gOverlapped);

        if ((Result == 0) || (BytesRead != DiskInfo->BytesPerMftRecord)) {
            m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

            jkGui->show_debug(DebugLevel::Progress, nullptr, L"      Error while reading Inode %I64u: %s", RefInode, s1);

            free(Buffer2);

            return;
        }

        /* Fixup the raw data. */
        if (FixupRawMftdata(Data, DiskInfo, Buffer2, DiskInfo->BytesPerMftRecord) == FALSE) {
            jkGui->show_debug(DebugLevel::Progress, nullptr, L"The error occurred while processing Inode %I64u",
                             RefInode);

            free(Buffer2);

            continue;
        }

        /* If the Inode is not in use then skip. */
        FileRecordHeader = (struct FILE_RECORD_HEADER*)Buffer2;

        if ((FileRecordHeader->Flags & 1) != 1) {
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"      Referenced Inode %I64u is not in use.",
                             RefInode);

            free(Buffer2);

            continue;
        }

        /* If the BaseInode inside the Inode is not the same as the calling Inode then skip. */
        BaseInode = (uint64_t)FileRecordHeader->BaseFileRecord.InodeNumberLowPart +
                ((uint64_t)FileRecordHeader->BaseFileRecord.InodeNumberHighPart << 32);

        if (InodeData->Inode != BaseInode) {
            jkGui->show_debug(
                DebugLevel::DetailedGapFinding, nullptr,
                L"      Warning: Inode %I64u is an extension of Inode %I64u, but thinks it's an extension of Inode %I64u.",
                RefInode, InodeData->Inode, BaseInode);

            free(Buffer2);

            continue;
        }

        /* Process the list of attributes in the Inode, by recursively calling the ProcessAttributes() subroutine. */
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"      Processing Inode %I64u Instance %u", RefInode,
                         Attribute->Instance);

        Result = ProcessAttributes(Data, DiskInfo, InodeData,
                                   &Buffer2[FileRecordHeader->AttributeOffset],
                                   DiskInfo->BytesPerMftRecord - FileRecordHeader->AttributeOffset,
                                   Attribute->Instance, Depth + 1);

        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"      Finished processing Inode %I64u Instance %u",
                         RefInode,
                         Attribute->Instance);

        free(Buffer2);
    }
}

/* Process a list of attributes and store the gathered information in the Item
struct. Return FALSE if an error occurred. */
BOOL ScanNtfs::ProcessAttributes(
    struct DefragDataStruct* Data,
    struct NtfsDiskInfoStruct* DiskInfo,
    struct InodeDataStruct* InodeData,
    BYTE* Buffer,
    uint64_t BufLength,
    USHORT Instance,
    int Depth) {
    BYTE* Buffer2;

    uint64_t Buffer2Length;
    ULONG AttributeOffset;

    struct ATTRIBUTE* Attribute;
    struct RESIDENT_ATTRIBUTE* ResidentAttribute;
    struct NONRESIDENT_ATTRIBUTE* NonResidentAttribute;
    struct STANDARD_INFORMATION* StandardInformation;
    struct FILENAME_ATTRIBUTE* FileNameAttribute;

    WCHAR* p1;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Walk through all the attributes and gather information. AttributeLists are
    skipped and interpreted later. */
    for (AttributeOffset = 0; AttributeOffset < BufLength; AttributeOffset = AttributeOffset + Attribute->Length) {
        Attribute = (struct ATTRIBUTE*)&Buffer[AttributeOffset];

        /* Exit the loop if end-marker. */
        if ((AttributeOffset + 4 <= BufLength) && (*(ULONG*)Attribute == 0xFFFFFFFF)) break;

        /* Sanity check. */
        if ((AttributeOffset + 4 > BufLength) ||
            (Attribute->Length < 3) ||
            (AttributeOffset + Attribute->Length > BufLength)) {
            jkGui->show_debug(
                DebugLevel::Progress, nullptr,
                L"Error: attribute in Inode %I64u is bigger than the data, the MFT may be corrupt.",
                InodeData->Inode);
            jkGui->show_debug(DebugLevel::Progress, nullptr,
                             L"  BufLength=%I64u, AttributeOffset=%lu, AttributeLength=%u(%X)",
                             BufLength, AttributeOffset, Attribute->Length, Attribute->Length);

            m_jkLib->show_hex(Data, Buffer, BufLength);

            return (FALSE);
        }

        /* Skip AttributeList's for now. */
        if (Attribute->AttributeType == AttributeAttributeList) continue;

        /* If the Instance does not equal the AttributeNumber then ignore the attribute.
        This is used when an AttributeList is being processed and we only want a specific
        instance. */
        if ((Instance != 65535) && (Instance != Attribute->AttributeNumber)) continue;

        /* Show debug message. */
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"  Attribute %u: %s", Attribute->AttributeNumber,
                         StreamTypeNames(Attribute->AttributeType));

        if (Attribute->Nonresident == 0) {
            ResidentAttribute = (struct RESIDENT_ATTRIBUTE*)Attribute;

            /* The AttributeFileName (0x30) contains the filename and the link to the parent directory. */
            if (Attribute->AttributeType == AttributeFileName) {
                FileNameAttribute = (struct FILENAME_ATTRIBUTE*)&Buffer[AttributeOffset + ResidentAttribute->
                    ValueOffset];

                InodeData->ParentInode = FileNameAttribute->ParentDirectory.InodeNumberLowPart +
                        (((uint64_t)FileNameAttribute->ParentDirectory.InodeNumberHighPart) << 32);

                if (FileNameAttribute->NameLength > 0) {
                    /* Extract the filename. */
                    p1 = (WCHAR*)malloc(sizeof(WCHAR) * (FileNameAttribute->NameLength + 1));

                    if (p1 == nullptr) {
                        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

                        return (FALSE);
                    }

                    wcsncpy_s(p1, FileNameAttribute->NameLength + 1, FileNameAttribute->Name,
                              FileNameAttribute->NameLength);

                    p1[FileNameAttribute->NameLength] = 0;

                    /* Save the filename in either the Long or the Short filename. We only
                    save the first filename, any additional filenames are hard links. They
                    might be useful for an optimization algorithm that sorts by filename,
                    but which of the hardlinked names should it sort? So we only store the
                    first filename. */
                    if (FileNameAttribute->NameType == 2) {
                        if (InodeData->ShortFilename != nullptr) {
                            free(p1);
                        }
                        else {
                            InodeData->ShortFilename = p1;

                            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Short filename = '%s'", p1);
                        }
                    }
                    else {
                        if (InodeData->LongFilename != nullptr) {
                            free(p1);
                        }
                        else {
                            InodeData->LongFilename = p1;

                            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Long filename = '%s'", p1);
                        }
                    }
                }
            }

            /* The AttributeStandardInformation (0x10) contains the CreationTime, LastAccessTime,
            the MftChangeTime, and the file attributes. */
            if (Attribute->AttributeType == AttributeStandardInformation) {
                StandardInformation = (struct STANDARD_INFORMATION*)&Buffer[AttributeOffset + ResidentAttribute->
                    ValueOffset];

                InodeData->CreationTime = StandardInformation->CreationTime;
                InodeData->MftChangeTime = StandardInformation->MftChangeTime;
                InodeData->LastAccessTime = StandardInformation->LastAccessTime;
            }

            /* The value of the AttributeData (0x80) is the actual data of the file. */
            if (Attribute->AttributeType == AttributeData) {
                InodeData->Bytes = ResidentAttribute->ValueLength;
            }
        }
        else {
            NonResidentAttribute = (struct NONRESIDENT_ATTRIBUTE*)Attribute;

            /* Save the length (number of bytes) of the data. */
            if ((Attribute->AttributeType == AttributeData) && (InodeData->Bytes == 0)) {
                InodeData->Bytes = NonResidentAttribute->DataSize;
            }

            /* Extract the streamname. */
            p1 = nullptr;

            if (Attribute->NameLength > 0) {
                p1 = (WCHAR*)malloc(sizeof(WCHAR) * (Attribute->NameLength + 1));

                if (p1 == nullptr) {
                    jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

                    return (FALSE);
                }

                wcsncpy_s(p1, Attribute->NameLength + 1, (WCHAR*)&Buffer[AttributeOffset + Attribute->NameOffset],
                          Attribute->NameLength);

                p1[Attribute->NameLength] = 0;
            }

            /* Create a new stream with a list of fragments for this data. */
            TranslateRundataToFragmentlist(Data, InodeData, p1, Attribute->AttributeType,
                                           (BYTE*)&Buffer[AttributeOffset + NonResidentAttribute->RunArrayOffset],
                                           Attribute->Length - NonResidentAttribute->RunArrayOffset,
                                           NonResidentAttribute->StartingVcn, NonResidentAttribute->DataSize);

            /* Cleanup the streamname. */
            if (p1 != nullptr) free(p1);

            /* Special case: If this is the $MFT then save data. */
            if (InodeData->Inode == 0) {
                if ((Attribute->AttributeType == AttributeData) && (InodeData->MftDataFragments == nullptr)) {
                    InodeData->MftDataFragments = InodeData->Streams->Fragments;
                    InodeData->MftDataBytes = NonResidentAttribute->DataSize;
                }

                if ((Attribute->AttributeType == AttributeBitmap) && (InodeData->MftBitmapFragments == nullptr)) {
                    InodeData->MftBitmapFragments = InodeData->Streams->Fragments;
                    InodeData->MftBitmapBytes = NonResidentAttribute->DataSize;
                }
            }
        }
    }

    /* Walk through all the attributes and interpret the AttributeLists. We have to
    do this after the DATA and BITMAP attributes have been interpreted, because
    some MFT's have an AttributeList that is stored in fragments that are
    defined in the DATA attribute, and/or contain a continuation of the DATA or
    BITMAP attributes. */
    for (AttributeOffset = 0; AttributeOffset < BufLength; AttributeOffset = AttributeOffset + Attribute->Length) {
        Attribute = (struct ATTRIBUTE*)&Buffer[AttributeOffset];

        if (*(ULONG*)Attribute == 0xFFFFFFFF) break;
        if (Attribute->AttributeType != AttributeAttributeList) continue;

        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"  Attribute %u: %s", Attribute->AttributeNumber,
                         StreamTypeNames(Attribute->AttributeType));

        if (Attribute->Nonresident == 0) {
            ResidentAttribute = (struct RESIDENT_ATTRIBUTE*)Attribute;

            ProcessAttributeList(Data, DiskInfo, InodeData,
                                 (BYTE*)&Buffer[AttributeOffset + ResidentAttribute->ValueOffset],
                                 ResidentAttribute->ValueLength, Depth);
        }
        else {
            NonResidentAttribute = (struct NONRESIDENT_ATTRIBUTE*)Attribute;
            Buffer2Length = NonResidentAttribute->DataSize;

            Buffer2 = ReadNonResidentData(Data, DiskInfo,
                                          (BYTE*)&Buffer[AttributeOffset + NonResidentAttribute->RunArrayOffset],
                                          Attribute->Length - NonResidentAttribute->RunArrayOffset, 0, Buffer2Length);

            ProcessAttributeList(Data, DiskInfo, InodeData, Buffer2, Buffer2Length, Depth);

            free(Buffer2);
        }
    }

    return (TRUE);
}

BOOL ScanNtfs::InterpretMftRecord(
    struct DefragDataStruct* Data,
    struct NtfsDiskInfoStruct* DiskInfo,
    struct ItemStruct** InodeArray,
    uint64_t InodeNumber,
    uint64_t MaxInode,
    struct FragmentListStruct** MftDataFragments,
    uint64_t* MftDataBytes,
    struct FragmentListStruct** MftBitmapFragments,
    uint64_t* MftBitmapBytes,
    BYTE* Buffer,
    uint64_t BufLength) {
    struct FILE_RECORD_HEADER* FileRecordHeader;

    struct InodeDataStruct InodeData;

    struct ItemStruct* Item;
    struct StreamStruct* Stream;

    uint64_t BaseInode;

    int Result;

    DefragGui* gui = DefragGui::get_instance();

    /* If the record is not in use then quietly exit. */
    FileRecordHeader = (struct FILE_RECORD_HEADER*)Buffer;

    if ((FileRecordHeader->Flags & 1) != 1) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Inode %I64u is not in use.", InodeNumber);

        return (FALSE);
    }

    /* If the record has a BaseFileRecord then ignore it. It is used by an
    AttributeAttributeList as an extension of another Inode, it's not an
    Inode by itself. */
    BaseInode = (uint64_t)FileRecordHeader->BaseFileRecord.InodeNumberLowPart +
            ((uint64_t)FileRecordHeader->BaseFileRecord.InodeNumberHighPart << 32);

    if (BaseInode != 0) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                         L"Ignoring Inode %I64u, it's an extension of Inode %I64u", InodeNumber, BaseInode);

        return (TRUE);
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Processing Inode %I64u...", InodeNumber);

    /* Show a warning if the Flags have an unknown value. */
    if ((FileRecordHeader->Flags & 252) != 0) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"  Inode %I64u has Flags = %u", InodeNumber,
                         FileRecordHeader->Flags);
    }

    /* I think the MFTRecordNumber should always be the InodeNumber, but it's an XP
    extension and I'm not sure about Win2K.
    Note: why is the MFTRecordNumber only 32 bit? Inode numbers are 48 bit. */
    if (FileRecordHeader->MFTRecordNumber != InodeNumber) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                         L"  Warning: Inode %I64u contains a different MFTRecordNumber %lu",
                         InodeNumber, FileRecordHeader->MFTRecordNumber);
    }

    /* Sanity check. */
    if (FileRecordHeader->AttributeOffset >= BufLength) {
        gui->show_debug(
            DebugLevel::Progress, nullptr,
            L"Error: attributes in Inode %I64u are outside the FILE record, the MFT may be corrupt.",
            InodeNumber);

        return (FALSE);
    }

    if (FileRecordHeader->BytesInUse > BufLength) {
        gui->show_debug(
            DebugLevel::Progress, nullptr,
            L"Error: in Inode %I64u the record is bigger than the size of the buffer, the MFT may be corrupt.",
            InodeNumber);

        return (FALSE);
    }

    /* Initialize the InodeData struct. */
    InodeData.Inode = InodeNumber; /* The Inode number. */
    InodeData.ParentInode = 5; /* The Inode number of the parent directory. */
    InodeData.Directory = false;

    if ((FileRecordHeader->Flags & 2) == 2) InodeData.Directory = true;

    InodeData.LongFilename = nullptr; /* Long filename. */
    InodeData.ShortFilename = nullptr; /* Short filename (8.3 DOS). */
    InodeData.CreationTime = 0; /* 1 second = 10000000 */
    InodeData.MftChangeTime = 0;
    InodeData.LastAccessTime = 0;
    InodeData.Bytes = 0; /* Size of the $DATA stream. */
    InodeData.Streams = nullptr; /* List of StreamStruct. */
    InodeData.MftDataFragments = *MftDataFragments;
    InodeData.MftDataBytes = *MftDataBytes;
    InodeData.MftBitmapFragments = nullptr;
    InodeData.MftBitmapBytes = 0;

    /* Make sure that directories are always created. */
    if (InodeData.Directory == true) {
        TranslateRundataToFragmentlist(Data, &InodeData, L"$I30", AttributeIndexAllocation, nullptr, 0, 0, 0);
    }

    /* Interpret the attributes. */
    Result = ProcessAttributes(Data, DiskInfo, &InodeData, &Buffer[FileRecordHeader->AttributeOffset],
                               BufLength - FileRecordHeader->AttributeOffset, 65535, 0);

    /* Save the MftDataFragments, MftDataBytes, MftBitmapFragments, and MftBitmapBytes. */
    if (InodeNumber == 0) {
        *MftDataFragments = InodeData.MftDataFragments;
        *MftDataBytes = InodeData.MftDataBytes;
        *MftBitmapFragments = InodeData.MftBitmapFragments;
        *MftBitmapBytes = InodeData.MftBitmapBytes;
    }

    /* Create an item in the Data->ItemTree for every stream. */
    Stream = InodeData.Streams;
    do {
        /* Create and fill a new item record in memory. */
        Item = (struct ItemStruct*)malloc(sizeof(struct ItemStruct));

        if (Item == nullptr) {
            gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

            if (InodeData.LongFilename != nullptr) free(InodeData.LongFilename);
            if (InodeData.ShortFilename != nullptr) free(InodeData.ShortFilename);

            CleanupStreams(&InodeData,TRUE);

            return (FALSE);
        }

        Item->LongFilename = ConstructStreamName(InodeData.LongFilename, InodeData.ShortFilename, Stream);
        Item->LongPath = nullptr;

        Item->ShortFilename = ConstructStreamName(InodeData.ShortFilename, InodeData.LongFilename, Stream);
        Item->ShortPath = nullptr;

        Item->Bytes = InodeData.Bytes;

        if (Stream != nullptr) Item->Bytes = Stream->Bytes;

        Item->Clusters = 0;

        if (Stream != nullptr) Item->Clusters = Stream->Clusters;

        Item->CreationTime = InodeData.CreationTime;
        Item->MftChangeTime = InodeData.MftChangeTime;
        Item->LastAccessTime = InodeData.LastAccessTime;
        Item->Fragments = nullptr;

        if (Stream != nullptr) Item->Fragments = Stream->Fragments;

        Item->ParentInode = InodeData.ParentInode;
        Item->is_dir_ = InodeData.Directory;
        Item->is_unmovable_ = false;
        Item->is_excluded_ = false;
        Item->is_hog_ = false;

        /* Increment counters. */
        if (Item->is_dir_ == true) {
            Data->count_directories_ = Data->count_directories_ + 1;
        }

        Data->count_all_files_ = Data->count_all_files_ + 1;

        if ((Stream != nullptr) && (Stream->StreamType == AttributeData)) {
            Data->count_all_bytes_ = Data->count_all_bytes_ + InodeData.Bytes;
        }

        if (Stream != nullptr) Data->count_all_clusters_ = Data->count_all_clusters_ + Stream->Clusters;

        if (m_jkLib->FragmentCount(Item) > 1) {
            Data->count_fragmented_items_ = Data->count_fragmented_items_ + 1;
            Data->count_fragmented_bytes_ = Data->count_fragmented_bytes_ + InodeData.Bytes;

            if (Stream != nullptr)
                Data->count_fragmented_clusters_ = Data->count_fragmented_clusters_ + Stream->
                        Clusters;
        }

        /* Add the item record to the sorted item tree in memory. */
        m_jkLib->TreeInsert(Data, Item);

        /* Also add the item to the array that is used to construct the full pathnames.
        Note: if the array already contains an entry, and the new item has a shorter
        filename, then the entry is replaced. This is needed to make sure that
        the shortest form of the name of directories is used. */

        if ((InodeArray != nullptr) &&
            (InodeNumber < MaxInode) &&
            ((InodeArray[InodeNumber] == nullptr) ||
                ((InodeArray[InodeNumber]->LongFilename != nullptr) &&
                    (Item->LongFilename != nullptr) &&
                    (wcscmp(InodeArray[InodeNumber]->LongFilename, Item->LongFilename) > 0)))) {
            InodeArray[InodeNumber] = Item;
        }

        /* Draw the item on the screen. */
        gui->show_analyze(Data, Item);
        //		if (*Data->RedrawScreen == false) {
        m_jkLib->ColorizeItem(Data, Item, 0, 0, false);
        //		} else {
        //			m_jkGui->ShowDiskmap(Data);
        //		}

        if (Stream != nullptr) Stream = Stream->Next;
    }
    while (Stream != nullptr);

    /* Cleanup and return TRUE. */
    if (InodeData.LongFilename != nullptr) free(InodeData.LongFilename);
    if (InodeData.ShortFilename != nullptr) free(InodeData.ShortFilename);

    CleanupStreams(&InodeData,FALSE);

    return (TRUE);
}

/* Load the MFT into a list of ItemStruct records in memory. */
BOOL ScanNtfs::AnalyzeNtfsVolume(struct DefragDataStruct* Data) {
    struct NtfsDiskInfoStruct DiskInfo;

    BYTE* Buffer;

    OVERLAPPED gOverlapped;

    ULARGE_INTEGER Trans;

    DWORD BytesRead;

    struct FragmentListStruct* MftDataFragments;

    uint64_t MftDataBytes;

    struct FragmentListStruct* MftBitmapFragments;

    uint64_t MftBitmapBytes;
    uint64_t MaxMftBitmapBytes;

    BYTE* MftBitmap;

    struct FragmentListStruct* Fragment;

    struct ItemStruct** InodeArray;

    uint64_t MaxInode;

    struct ItemStruct* Item;

    uint64_t Vcn;
    uint64_t RealVcn;
    uint64_t InodeNumber;
    uint64_t BlockStart;
    uint64_t BlockEnd;

    BYTE BitmapMasks[8] = {1, 2, 4, 8, 16, 32, 64, 128};

    int Result;

    ULONG ClustersPerMftRecord;

    struct __timeb64 Time;

    int64_t StartTime;
    int64_t EndTime;

    WCHAR s1[BUFSIZ];

    uint64_t u1;

    DefragGui* jkGui = DefragGui::get_instance();

    /* Read the boot block from the disk. */
    Buffer = (BYTE*)malloc(MFTBUFFERSIZE);

    if (Buffer == nullptr) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        return (FALSE);
    }

    gOverlapped.Offset = 0;
    gOverlapped.OffsetHigh = 0;
    gOverlapped.hEvent = nullptr;

    Result = ReadFile(Data->disk_.volume_handle_, Buffer, (uint32_t)512, &BytesRead, &gOverlapped);

    if ((Result == 0) || (BytesRead != 512)) {
        m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error while reading bootblock: %s", s1);

        free(Buffer);

        return (FALSE);
    }

    /* Test if the boot block is an NTFS boot block. */
    if (*(ULONGLONG*)&Buffer[3] != 0x202020205346544E) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"This is not an NTFS disk (different cookie).");

        free(Buffer);

        return (FALSE);
    }

    /* Extract data from the bootblock. */
    Data->disk_.type_ = DiskType::NTFS;
    DiskInfo.BytesPerSector = *(USHORT*)&Buffer[11];

    /* Still to do: check for impossible values. */
    DiskInfo.SectorsPerCluster = Buffer[13];
    DiskInfo.TotalSectors = *(ULONGLONG*)&Buffer[40];
    DiskInfo.MftStartLcn = *(ULONGLONG*)&Buffer[48];
    DiskInfo.Mft2StartLcn = *(ULONGLONG*)&Buffer[56];
    ClustersPerMftRecord = *(ULONG*)&Buffer[64];

    if (ClustersPerMftRecord >= 128) {
        DiskInfo.BytesPerMftRecord = ((uint64_t)1 << (256 - ClustersPerMftRecord));
    }
    else {
        DiskInfo.BytesPerMftRecord = ClustersPerMftRecord * DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster;
    }

    DiskInfo.ClustersPerIndexRecord = *(ULONG*)&Buffer[68];

    Data->bytes_per_cluster_ = DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster;

    if (DiskInfo.SectorsPerCluster > 0) {
        Data->total_clusters_ = DiskInfo.TotalSectors / DiskInfo.SectorsPerCluster;
    }

    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"This is an NTFS disk.");
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  Disk cookie: %I64X", *(ULONGLONG*)&Buffer[3]);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BytesPerSector: %I64u", DiskInfo.BytesPerSector);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  TotalSectors: %I64u", DiskInfo.TotalSectors);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  SectorsPerCluster: %I64u", DiskInfo.SectorsPerCluster);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  SectorsPerTrack: %lu", *(USHORT*)&Buffer[24]);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  NumberOfHeads: %lu", *(USHORT*)&Buffer[26]);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  MftStartLcn: %I64u", DiskInfo.MftStartLcn);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  Mft2StartLcn: %I64u", DiskInfo.Mft2StartLcn);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  BytesPerMftRecord: %I64u", DiskInfo.BytesPerMftRecord);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  ClustersPerIndexRecord: %I64u",
                     DiskInfo.ClustersPerIndexRecord);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  MediaType: %X", Buffer[21]);
    jkGui->show_debug(DebugLevel::Progress, nullptr, L"  VolumeSerialNumber: %I64X", *(ULONGLONG*)&Buffer[72]);

    /* Calculate the size of first 16 Inodes in the MFT. The Microsoft defragmentation
    API cannot move these inodes. */
    Data->disk_.mft_locked_clusters_ = DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster / DiskInfo.
            BytesPerMftRecord;

    /* Read the $MFT record from disk into memory, which is always the first record in
    the MFT. */
    Trans.QuadPart = DiskInfo.MftStartLcn * DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster;
    gOverlapped.Offset = Trans.LowPart;
    gOverlapped.OffsetHigh = Trans.HighPart;
    gOverlapped.hEvent = nullptr;
    Result = ReadFile(Data->disk_.volume_handle_, Buffer, (uint32_t)DiskInfo.BytesPerMftRecord, &BytesRead,
                      &gOverlapped);

    if ((Result == 0) || (BytesRead != DiskInfo.BytesPerMftRecord)) {
        m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error while reading first MFT record: %s", s1);

        free(Buffer);

        return (FALSE);
    }

    /* Fixup the raw data from disk. This will also test if it's a valid $MFT record. */
    if (FixupRawMftdata(Data, &DiskInfo, Buffer, DiskInfo.BytesPerMftRecord) == FALSE) {
        free(Buffer);

        return (FALSE);
    }

    /* Extract data from the MFT record and put into an Item struct in memory. If
    there was an error then exit. */
    MftDataBytes = 0;
    MftDataFragments = nullptr;
    MftBitmapBytes = 0;
    MftBitmapFragments = nullptr;

    Result = InterpretMftRecord(Data, &DiskInfo, nullptr, 0, 0, &MftDataFragments, &MftDataBytes,
                                &MftBitmapFragments, &MftBitmapBytes, Buffer, DiskInfo.BytesPerMftRecord);

    if ((Result == FALSE) ||
        (MftDataFragments == nullptr) || (MftDataBytes == 0) ||
        (MftBitmapFragments == nullptr) || (MftBitmapBytes == 0)) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Fatal error, cannot process this disk.");

        free(Buffer);

        m_jkLib->DeleteItemTree(Data->item_tree_);

        Data->item_tree_ = nullptr;

        return (FALSE);
    }

    jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"MftDataBytes = %I64u, MftBitmapBytes = %I64u",
                     MftDataBytes, MftBitmapBytes);

    /* Read the complete $MFT::$BITMAP into memory.
    Note: The allocated size of the bitmap is a multiple of the cluster size. This
    is only to make it easier to read the fragments, the extra bytes are not used. */
    jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading $MFT::$BITMAP into memory");

    Vcn = 0;
    MaxMftBitmapBytes = 0;

    for (Fragment = MftBitmapFragments; Fragment != nullptr; Fragment = Fragment->next_) {
        if (Fragment->lcn_ != VIRTUALFRAGMENT) {
            MaxMftBitmapBytes = MaxMftBitmapBytes +
                    (Fragment->next_vcn_ - Vcn) * DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster;
        }

        Vcn = Fragment->next_vcn_;
    }

    if (MaxMftBitmapBytes < MftBitmapBytes) MaxMftBitmapBytes = (size_t)MftBitmapBytes;

    MftBitmap = (BYTE*)malloc((size_t)MaxMftBitmapBytes);

    if (MftBitmap == nullptr) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        free(Buffer);

        m_jkLib->DeleteItemTree(Data->item_tree_);

        Data->item_tree_ = nullptr;

        return (FALSE);
    }

    memset(MftBitmap, 0, (size_t)MftBitmapBytes);

    Vcn = 0;
    RealVcn = 0;

    jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading $MFT::$BITMAP into memory");

    for (Fragment = MftBitmapFragments; Fragment != nullptr; Fragment = Fragment->next_) {
        if (Fragment->lcn_ != VIRTUALFRAGMENT) {
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"  Extent Lcn=%I64u, RealVcn=%I64u, Size=%I64u",
                             Fragment->lcn_, RealVcn, Fragment->next_vcn_ - Vcn);

            Trans.QuadPart = Fragment->lcn_ * DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster;

            gOverlapped.Offset = Trans.LowPart;
            gOverlapped.OffsetHigh = Trans.HighPart;
            gOverlapped.hEvent = nullptr;

            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                             L"    Reading %I64u clusters (%I64u bytes) from LCN=%I64u",
                             Fragment->next_vcn_ - Vcn,
                             (Fragment->next_vcn_ - Vcn) * DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster,
                             Fragment->lcn_);

            Result = ReadFile(Data->disk_.volume_handle_,
                              &MftBitmap[RealVcn * DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster],
                              (uint32_t)((Fragment->next_vcn_ - Vcn) * DiskInfo.BytesPerSector * DiskInfo.
                                  SectorsPerCluster),
                              &BytesRead, &gOverlapped);

            if ((Result == 0) || (BytesRead != (Fragment->next_vcn_ - Vcn) * DiskInfo.BytesPerSector * DiskInfo.
                SectorsPerCluster)) {
                m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

                jkGui->show_debug(DebugLevel::Progress, nullptr, L"  %s", s1);

                free(MftBitmap);
                free(Buffer);

                m_jkLib->DeleteItemTree(Data->item_tree_);

                Data->item_tree_ = nullptr;

                return (FALSE);
            }

            RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
        }

        Vcn = Fragment->next_vcn_;
    }

    /* Construct an array of all the items in memory, indexed by Inode.
    Note: the maximum number of Inodes is primarily determined by the size of the
    bitmap. But that is rounded up to 8 Inodes, and the MFT can be shorter. */
    MaxInode = MftBitmapBytes * 8;

    if (MaxInode > MftDataBytes / DiskInfo.BytesPerMftRecord) {
        MaxInode = MftDataBytes / DiskInfo.BytesPerMftRecord;
    }

    InodeArray = (struct ItemStruct**)malloc((size_t)(MaxInode * sizeof(struct ItemStruct*)));

    if (InodeArray == nullptr) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        free(Buffer);

        m_jkLib->DeleteItemTree(Data->item_tree_);

        Data->item_tree_ = nullptr;

        return (FALSE);
    }

    InodeArray[0] = Data->item_tree_;

    for (InodeNumber = 1; InodeNumber < MaxInode; InodeNumber++) {
        InodeArray[InodeNumber] = nullptr;
    }

    /* Read and process all the records in the MFT. The records are read into a
    buffer and then given one by one to the InterpretMftRecord() subroutine. */
    Fragment = MftDataFragments;
    BlockEnd = 0;
    Vcn = 0;
    RealVcn = 0;

    Data->PhaseDone = 0;
    Data->PhaseTodo = 0;

    _ftime64_s(&Time);

    StartTime = Time.time * 1000 + Time.millitm;

    for (InodeNumber = 1; InodeNumber < MaxInode; InodeNumber++) {
        if ((MftBitmap[InodeNumber >> 3] & BitmapMasks[InodeNumber % 8]) == 0) continue;

        Data->PhaseTodo = Data->PhaseTodo + 1;
    }

    for (InodeNumber = 1; InodeNumber < MaxInode; InodeNumber++) {
        if (*Data->running_ != RunningState::RUNNING) break;

        /* Ignore the Inode if the bitmap says it's not in use. */
        if ((MftBitmap[InodeNumber >> 3] & BitmapMasks[InodeNumber % 8]) == 0) {
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Inode %I64u is not in use.", InodeNumber);

            continue;
        }

        /* Update the progress counter. */
        Data->PhaseDone = Data->PhaseDone + 1;

        /* Read a block of inode's into memory. */
        if (InodeNumber >= BlockEnd) {
            /* Slow the program down to the percentage that was specified on the command line. */
            m_jkLib->slow_down(Data);

            BlockStart = InodeNumber;
            BlockEnd = BlockStart + MFTBUFFERSIZE / DiskInfo.BytesPerMftRecord;

            if (BlockEnd > MftBitmapBytes * 8) BlockEnd = MftBitmapBytes * 8;

            while (Fragment != nullptr) {
                /* Calculate Inode at the end of the fragment. */
                u1 = (RealVcn + Fragment->next_vcn_ - Vcn) * DiskInfo.BytesPerSector *
                        DiskInfo.SectorsPerCluster / DiskInfo.BytesPerMftRecord;

                if (u1 > InodeNumber) break;

                do {
                    jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Skipping to next extent");

                    if (Fragment->lcn_ != VIRTUALFRAGMENT) RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;

                    Vcn = Fragment->next_vcn_;
                    Fragment = Fragment->next_;

                    if (Fragment == nullptr) break;
                }
                while (Fragment->lcn_ == VIRTUALFRAGMENT);

                jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                                 L"  Extent Lcn=%I64u, RealVcn=%I64u, Size=%I64u",
                                 Fragment->lcn_, RealVcn, Fragment->next_vcn_ - Vcn);
            }
            if (Fragment == nullptr) break;
            if (BlockEnd >= u1) BlockEnd = u1;

            Trans.QuadPart = (Fragment->lcn_ - RealVcn) * DiskInfo.BytesPerSector *
                    DiskInfo.SectorsPerCluster + BlockStart * DiskInfo.BytesPerMftRecord;

            gOverlapped.Offset = Trans.LowPart;
            gOverlapped.OffsetHigh = Trans.HighPart;
            gOverlapped.hEvent = nullptr;

            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                             L"Reading block of %I64u Inodes from MFT into memory, %u bytes from LCN=%I64u",
                             BlockEnd - BlockStart, (uint32_t)((BlockEnd - BlockStart) * DiskInfo.BytesPerMftRecord),
                             Trans.QuadPart / (DiskInfo.BytesPerSector * DiskInfo.SectorsPerCluster));

            Result = ReadFile(Data->disk_.volume_handle_, Buffer,
                              (uint32_t)((BlockEnd - BlockStart) * DiskInfo.BytesPerMftRecord), &BytesRead,
                              &gOverlapped);

            if ((Result == 0) || (BytesRead != (BlockEnd - BlockStart) * DiskInfo.BytesPerMftRecord)) {
                m_jkLib->system_error_str(GetLastError(), s1,BUFSIZ);

                jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error while reading Inodes %I64u to %I64u: %s",
                                 InodeNumber,
                                 BlockEnd - 1, s1);

                free(Buffer);
                free(InodeArray);

                m_jkLib->DeleteItemTree(Data->item_tree_);

                Data->item_tree_ = nullptr;

                return (FALSE);
            }
        }

        /* Fixup the raw data of this Inode. */
        if (FixupRawMftdata(Data, &DiskInfo, &Buffer[(InodeNumber - BlockStart) * DiskInfo.BytesPerMftRecord],
                            DiskInfo.BytesPerMftRecord) == FALSE) {
            jkGui->show_debug(DebugLevel::Progress, nullptr,
                             L"The error occurred while processing Inode %I64u (max %I64u)",
                             InodeNumber, MaxInode);

            continue;
        }

        /* Interpret the Inode's attributes. */
        Result = InterpretMftRecord(Data, &DiskInfo, InodeArray, InodeNumber, MaxInode,
                                    &MftDataFragments, &MftDataBytes, &MftBitmapFragments, &MftBitmapBytes,
                                    &Buffer[(InodeNumber - BlockStart) * DiskInfo.BytesPerMftRecord],
                                    DiskInfo.BytesPerMftRecord);
    }

    _ftime64_s(&Time);

    EndTime = Time.time * 1000 + Time.millitm;

    if (EndTime > StartTime) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"  Analysis speed: %I64u items per second",
                         MaxInode * 1000 / (EndTime - StartTime));
    }

    free(Buffer);

    if (MftBitmap != nullptr) free(MftBitmap);

    if (*Data->running_ != RunningState::RUNNING) {
        free(InodeArray);

        m_jkLib->DeleteItemTree(Data->item_tree_);

        Data->item_tree_ = nullptr;

        return (FALSE);
    }

    /* Setup the ParentDirectory in all the items with the info in the InodeArray. */
    for (Item = m_jkLib->tree_smallest(Data->item_tree_); Item != nullptr; Item = m_jkLib->TreeNext(Item)) {
        Item->ParentDirectory = InodeArray[Item->ParentInode];

        if (Item->ParentInode == 5) Item->ParentDirectory = nullptr;
    }

    free(InodeArray);

    return (TRUE);
}
