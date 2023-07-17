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

bool ScanNTFS::interpret_mft_record(
        DefragState &data, NtfsDiskInfoStruct *disk_info, ItemStruct **inode_array,
        const uint64_t inode_number, const uint64_t max_inode,
        PARAM_OUT FragmentListStruct *&mft_data_fragments, PARAM_OUT uint64_t &mft_data_bytes,
        PARAM_OUT FragmentListStruct *&mft_bitmap_fragments, PARAM_OUT uint64_t &mft_bitmap_bytes,
        BYTE *buffer, const uint64_t buf_length
) {
    InodeDataStruct inode_data{};
    DefragGui *gui = DefragGui::get_instance();

    // If the record is not in use then quietly exit
    const FILE_RECORD_HEADER *file_record_header = (FILE_RECORD_HEADER *) buffer;

    if ((file_record_header->flags_ & 1) != 1) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"Inode " NUM_FMT " is not in use.", inode_number));

        return false;
    }

    /* If the record has a BaseFileRecord then ignore it. It is used by an
    AttributeAttributeList as an extension of another Inode, it's not an
    Inode by itself. */
    const uint64_t base_inode = (uint64_t) file_record_header->base_file_record_.inode_number_low_part_ +
                                ((uint64_t) file_record_header->base_file_record_.inode_number_high_part_ << 32);

    if (base_inode != 0) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"Ignoring Inode " NUM_FMT ", it's an extension of Inode %I64u", inode_number,
                                    base_inode));

        return true;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                    std::format(L"Processing Inode " NUM_FMT "…", inode_number));

    // Show a warning if the Flags have an unknown value
    if ((file_record_header->flags_ & 252) != 0) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"  Inode " NUM_FMT " has Flags = {}", inode_number, file_record_header->flags_));
    }

    /* I think the MFTRecordNumber should always be the inode_number, but it's an XP
    extension and I'm not sure about Win2K.
    Note: why is the MFTRecordNumber only 32 bit? Inode numbers are 48 bit. */
    if (file_record_header->mft_record_number_ != inode_number) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"  Warning: Inode " NUM_FMT " contains a different MFTRecordNumber " NUM_FMT,
                                    inode_number, file_record_header->mft_record_number_));
    }

    // Sanity check
    if (file_record_header->attribute_offset_ >= buf_length) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                std::format(
                        L"Error: attributes in Inode " NUM_FMT " are outside the FILE record, the MFT may be corrupt.",
                        inode_number));

        return false;
    }

    if (file_record_header->bytes_in_use_ > buf_length) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                std::format(
                        L"Error: in Inode " NUM_FMT " the record is bigger than the size of the buffer, the MFT may be corrupt.",
                        inode_number));

        return false;
    }

    // Initialize the InodeData struct
    inode_data.inode_ = inode_number; // The Inode number
    inode_data.parent_inode_ = 5; // The Inode number of the parent directory
    inode_data.is_directory_ = false;

    if ((file_record_header->flags_ & 2) == 2) inode_data.is_directory_ = true;

    inode_data.long_filename_ = nullptr; // Long filename
    inode_data.short_filename_ = nullptr; // Short filename (8.3 DOS)
    inode_data.creation_time_ = {};
    inode_data.mft_change_time_ = {};
    inode_data.last_access_time_ = {};
    inode_data.bytes_ = 0; // Size of the $DATA stream
    inode_data.streams_ = nullptr; // List of StreamStruct
    inode_data.mft_data_fragments_ = mft_data_fragments;
    inode_data.mft_data_bytes_ = mft_data_bytes;
    inode_data.mft_bitmap_fragments_ = nullptr;
    inode_data.mft_bitmap_bytes_ = 0;

    // Make sure that directories are always created
    if (inode_data.is_directory_) {
        translate_rundata_to_fragmentlist(data, &inode_data, L"$I30", ATTRIBUTE_TYPE::AttributeIndexAllocation, nullptr,
                                          0,
                                          0, 0);
    }

    // Interpret the attributes
    [[maybe_unused]] int result = process_attributes(data, disk_info, &inode_data,
                                                     &buffer[file_record_header->attribute_offset_],
                                                     buf_length - file_record_header->attribute_offset_, 65535, 0);

    // Save the mft_data_fragments, mft_data_bytes, mft_bitmap_fragments, and MftBitmapBytes
    if (inode_number == 0) {
        mft_data_fragments = inode_data.mft_data_fragments_;
        mft_data_bytes = inode_data.mft_data_bytes_;
        mft_bitmap_fragments = inode_data.mft_bitmap_fragments_;
        mft_bitmap_bytes = inode_data.mft_bitmap_bytes_;
    }

    // Create an item in the data.ItemTree for every stream
    StreamStruct *stream = inode_data.streams_;
    do {
        // Create and fill a new item record in memory
        auto item = std::make_unique<ItemStruct>();
        auto long_fn_constructed = construct_stream_name(inode_data.long_filename_.get(),
                                                         inode_data.short_filename_.get(), stream);
        auto short_fn_constructed = construct_stream_name(inode_data.short_filename_.get(),
                                                          inode_data.long_filename_.get(),
                                                          stream);
        item->set_names(L"", long_fn_constructed.c_str(), nullptr, short_fn_constructed.c_str());

        item->bytes_ = inode_data.bytes_;

        if (stream != nullptr) item->bytes_ = stream->bytes_;

        item->clusters_count_ = 0;

        if (stream != nullptr) item->clusters_count_ = stream->clusters_;

        item->creation_time_ = inode_data.creation_time_;
        item->mft_change_time_ = inode_data.mft_change_time_;
        item->last_access_time_ = inode_data.last_access_time_;
        item->fragments_ = nullptr;

        if (stream != nullptr) item->fragments_ = stream->fragments_;

        item->parent_inode_ = inode_data.parent_inode_;
        item->is_dir_ = inode_data.is_directory_;
        item->is_unmovable_ = false;
        item->is_excluded_ = false;
        item->is_hog_ = false;

        // Increment counters
        if (item->is_dir_) {
            data.count_directories_ += 1;
        }

        data.count_all_files_ += 1;

        if (stream != nullptr && stream->stream_type_ == ATTRIBUTE_TYPE::AttributeData) {
            data.count_all_bytes_ += inode_data.bytes_;
        }

        if (stream != nullptr) data.count_all_clusters_ += stream->clusters_;

        if (DefragLib::get_fragment_count(item.get()) > 1) {
            data.count_fragmented_items_ += 1;
            data.count_fragmented_bytes_ += inode_data.bytes_;

            if (stream != nullptr)
                data.count_fragmented_clusters_ += stream->clusters_;
        }

        // Add the item record to the sorted item tree in memory
        auto last_created_item = item.release();
        Tree::insert(data.item_tree_, data.balance_count_, last_created_item);

        // Also add the item to the array that is used to construct the full pathnames.
        // Note: if the array already contains an entry, and the new item has a shorter
        // filename, then the entry is replaced. This is needed to make sure that
        // the shortest form of the name of directories is used.

        if (inode_array != nullptr
            && inode_number < max_inode
            && (inode_array[inode_number] == nullptr
                || (inode_array[inode_number]->have_long_fn()
                    && last_created_item->have_long_fn()
                    && wcscmp(inode_array[inode_number]->get_long_fn(), last_created_item->get_long_fn()) > 0))) {
            inode_array[inode_number] = last_created_item;
        }

        // Draw the item on the screen.
        gui->show_analyze(data, last_created_item);
        defrag_lib_->colorize_disk_item(data, last_created_item, 0, 0, false);

        if (stream != nullptr) stream = stream->next_;
    } while (stream != nullptr);

    // Cleanup and return TRUE
    inode_data.long_filename_.reset();
    inode_data.short_filename_.reset();

    cleanup_streams(&inode_data, false);

    return true;
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

bool ScanNTFS::fixup_raw_mftdata(DefragState &data, const NtfsDiskInfoStruct *disk_info, BYTE *buffer,
                                 const uint64_t buf_length) const {
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check.
    if (buffer == nullptr) return false;
    if (buf_length < sizeof(NTFS_RECORD_HEADER)) return false;

    // If this is not a FILE record then return FALSE.
    if (memcmp(buffer, "FILE", 4) != 0) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                L"This is not a valid MFT record, it does not begin with FILE (maybe trying to read past the end?).");

        DefragLib::show_hex(data, buffer, buf_length);

        return false;
    }

    /* Walk through all the sectors and restore the last 2 bytes with the value
    from the Usa array. If we encounter bad sector data then return with FALSE. */
    const auto buffer_w = (WORD *) buffer;
    const auto record_header = (NTFS_RECORD_HEADER *) buffer;
    const auto update_sequence_array = (WORD *) &buffer[record_header->usa_offset_];
    const auto increment = (uint32_t) (disk_info->bytes_per_sector_ / sizeof(USHORT));
    uint32_t index = increment - 1;

    for (USHORT i = 1; i < record_header->usa_count_; i++) {
        // Check if we are inside the buffer
        if (index * sizeof(WORD) >= buf_length) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            L"Warning: USA data indicates that data is missing, the MFT may be corrupt.");
        }

        /* Check if the last 2 bytes of the sector contain the Update Sequence Number.
        If not then return FALSE. */
        if (buffer_w[index] != update_sequence_array[0]) {
            gui->show_debug(
                    DebugLevel::Progress, nullptr,
                    L"Error: USA fixup word is not equal to the Update Sequence Number, the MFT may be corrupt.");

            return false;
        }

        // Replace the last 2 bytes in the sector with the value from the Usa array
        buffer_w[index] = update_sequence_array[i];
        index = index + increment;
    }

    return true;
}
