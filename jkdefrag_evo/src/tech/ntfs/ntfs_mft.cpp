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
        DefragState &data, NtfsDiskInfoStruct *disk_info, FileNode **inode_array,
        const Inode64 inode_number, const Inode64 max_inode,
        PARAM_OUT std::list<FileFragment> &mft_data_fragments, PARAM_OUT Bytes64 &mft_data_bytes,
        PARAM_OUT std::list<FileFragment> &mft_bitmap_fragments, PARAM_OUT Bytes64 &mft_bitmap_bytes,
        MUT MemSlice &buffer
) {
    DefragGui *gui = DefragGui::get_instance();

    // If the record is not in use then quietly exit
    const auto file_record_header = buffer.ptr_to<FILE_RECORD_HEADER>(0);

    if ((file_record_header->flags_ & 1) != 1) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"Inode " NUM_FMT " is not in use.", inode_number.value()));

        return false;
    }

    // If the record has a BaseFileRecord then ignore it.
    // It is used by an AttributeAttributeList as an extension of another Inode, it's not an Inode by itself.
    const Inode64 base_inode = Inode64(
            (uint64_t) file_record_header->base_file_record_.inode_number_low_part_ +
            ((uint64_t) file_record_header->base_file_record_.inode_number_high_part_ << 32));

    if (base_inode) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"Ignoring Inode " NUM_FMT ", it's an extension of Inode %I64u",
                                    inode_number.value(), base_inode.value()));

        return true;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                    std::format(L"Processing Inode " NUM_FMT "…", inode_number.value()));

    // Show a warning if the Flags have an unknown value
    if ((file_record_header->flags_ & 252) != 0) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"  Inode " NUM_FMT " has Flags = {}", inode_number.value(),
                                    file_record_header->flags_));
    }

    // I think the MFTRecordNumber should always be the inode_number,
    // but it's an XP extension and I'm not sure about Win2K. Note: why is the MFTRecordNumber only 32 bit?
    // Inode numbers are 48 bit.
    if (Inode64(file_record_header->mft_record_number_) != inode_number) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"  Warning: Inode " NUM_FMT " contains a different MFTRecordNumber " NUM_FMT,
                                    inode_number.value(), file_record_header->mft_record_number_));
    }

    // Sanity check
    if (file_record_header->attribute_offset_ >= buffer.length().as<USHORT>()) {
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                L"Error: attributes in Inode {} are outside the FILE record, the MFT may be corrupt.",
                inode_number));

        return false;
    }

    if (file_record_header->bytes_in_use_ > buffer.length().as<ULONG>()) {
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                L"Error: in Inode {} the record is bigger than the size of the buffer, the MFT may be corrupt.",
                inode_number));

        return false;
    }

    // Initialize the InodeData struct
    InodeDataStruct inode_data{
            .inode_ = inode_number, // The Inode number
            .parent_inode_ = Inode64(5), // The Inode number of the parent directory
            .is_directory_ = (file_record_header->flags_ & 2) == 2,
            .long_filename_ = nullptr, // Long filename
            .short_filename_ = nullptr,// Short filename (8.3 DOS)
            .bytes_ = {}, // Size of the $DATA stream
            .creation_time_ = {},
            .mft_change_time_ = {},
            .last_access_time_ = {},
            .mft_data_fragments_ = mft_data_fragments,
            .mft_data_bytes_ = mft_data_bytes,
            .mft_bitmap_fragments_ = mft_bitmap_fragments,
            .mft_bitmap_bytes_ = {},
    };

    // Make sure that directories are always created
    if (inode_data.is_directory_) {
        auto empty_memv = MemSlice::empty();
        translate_rundata_to_fragmentlist(data, &inode_data, L"$I30", ATTRIBUTE_TYPE::AttributeIndexAllocation,
                                          empty_memv, Clusters64(0), Bytes64(0));
    }

    // Interpret the attributes
    auto mem_view = MemSlice::from_ptr(buffer.get() + file_record_header->attribute_offset_,
                                       buffer.length() - Bytes64(file_record_header->attribute_offset_));
    [[maybe_unused]] int result = process_attributes(
            data, disk_info, &inode_data, mem_view, 65535, 0);

    // Save the mft_data_fragments, mft_data_bytes, mft_bitmap_fragments, and MftBitmapBytes
    if (inode_number.is_zero()) {
        mft_data_fragments = inode_data.mft_data_fragments_;
        mft_data_bytes = inode_data.mft_data_bytes_;
        mft_bitmap_fragments = inode_data.mft_bitmap_fragments_;
        mft_bitmap_bytes = inode_data.mft_bitmap_bytes_;
    }

    // Create an item in the data.ItemTree for every stream
    auto stream_iter = inode_data.streams_.begin();

    while (stream_iter != inode_data.streams_.end()) {
        // Create and fill a new item record in memory
        auto item = std::make_unique<FileNode>();
        auto long_fn_constructed = construct_stream_name(inode_data.long_filename_.get(),
                                                         inode_data.short_filename_.get(),
                                                         &*stream_iter);
        auto short_fn_constructed = construct_stream_name(inode_data.short_filename_.get(),
                                                          inode_data.long_filename_.get(),
                                                          &*stream_iter);
        item->set_names(L"", long_fn_constructed.c_str(), nullptr, short_fn_constructed.c_str());
        item->bytes_ = inode_data.bytes_;

        if (stream_iter != inode_data.streams_.end()) item->bytes_ = stream_iter->bytes_;

        item->clusters_count_ = {};

        if (stream_iter != inode_data.streams_.end()) item->clusters_count_ = stream_iter->clusters_;

        item->creation_time_ = inode_data.creation_time_;
        item->mft_change_time_ = inode_data.mft_change_time_;
        item->last_access_time_ = inode_data.last_access_time_;
        item->fragments_.clear();

        if (stream_iter != inode_data.streams_.end()) item->fragments_ = stream_iter->fragments_;

        item->parent_inode_ = inode_data.parent_inode_;
        item->is_dir_ = inode_data.is_directory_;
        item->is_unmovable_ = false;
        item->is_excluded_ = false;
        item->is_hog_ = false;

        // Increment counters
        if (item->is_dir_) {
            data.count_directories_++;
        }

        data.count_all_files_++;

        if (stream_iter != inode_data.streams_.end() && stream_iter->stream_type_ == ATTRIBUTE_TYPE::AttributeData) {
            data.count_all_bytes_ += inode_data.bytes_;
        }

        if (stream_iter != inode_data.streams_.end()) data.count_all_clusters_ += stream_iter->clusters_;

        if (DefragRunner::get_fragment_count(item.get()) > 1) {
            data.count_fragmented_items_++;
            data.count_fragmented_bytes_ += inode_data.bytes_;

            if (stream_iter != inode_data.streams_.end()) {
                data.count_fragmented_clusters_ += stream_iter->clusters_;
            }
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
            && (inode_array[inode_number.value()] == nullptr
                || (inode_array[inode_number.value()]->have_long_fn()
                    && last_created_item->have_long_fn()
                    &&
                    wcscmp(inode_array[inode_number.value()]->get_long_fn(), last_created_item->get_long_fn()) > 0))) {
            inode_array[inode_number.value()] = last_created_item;
        }

        // Draw the item on the screen.
        gui->show_analyze(data, last_created_item);
        defrag_lib_->colorize_disk_item(data, last_created_item, Clusters64(0), Clusters64(0), false);

        stream_iter++;
    };

    // Cleanup and return TRUE
    inode_data.long_filename_.reset();
    inode_data.short_filename_.reset();

    // cleanup_streams(&inode_data);
    inode_data.streams_.clear();

    return true;
}

// Fixup the raw MFT data that was read from disk. Return TRUE if everything is ok,
// FALSE if the MFT data is corrupt (this can also happen when we have read a
// record past the end of the MFT, maybe it has shrunk while we were processing).
//
// - To protect against disk failure, the last 2 bytes of every sector in the MFT are
// not stored in the sector itself, but in the "Usa" array in the header (described
// by UsaOffset and UsaCount). The last 2 bytes are copied into the array and the
// Update Sequence Number is written in their place.
//
// - The Update Sequence Number is stored in the first item (item zero) of the "Usa"
// array.
//
// - The number of bytes per sector is defined in the $Boot record.
bool
ScanNTFS::fixup_raw_mftdata(DefragState &data, const NtfsDiskInfoStruct *disk_info, const MemSlice &buffer) const {
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check.
    if (!buffer) return false;
    if (buffer.length() < Bytes64(sizeof(NTFS_RECORD_HEADER))) return false;

    // If this is not a FILE record then return FALSE.
    if (memcmp(buffer.get(), "FILE", 4) != 0) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                L"This is not a valid MFT record, it does not begin with FILE (maybe trying to read past the end?).");

        DefragRunner::show_hex(data, buffer);

        return false;
    }

    // Walk through all the sectors and restore the last 2 bytes with the value
    // from the Usa array. If we encounter bad sector data then return with FALSE
    const auto buffer_words = buffer.ptr_to<WORD>(0);
    const auto buffer_NTFS_RH = buffer.ptr_to<NTFS_RECORD_HEADER>(0);
    const auto update_sequence_array = buffer.ptr_to<WORD>(buffer_NTFS_RH->usa_offset_);
    const auto increment = (uint32_t) (disk_info->bytes_per_sector_.value() / sizeof(USHORT));
    uint32_t index = increment - 1;

    for (USHORT i = 1; i < buffer_NTFS_RH->usa_count_; i++) {
        // Check if we are inside the buffer
        if (Bytes64(index * sizeof(WORD)) >= buffer.length()) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            L"Warning: USA data indicates that data is missing, the MFT may be corrupt.");
        }

        // Check if the last 2 bytes of the sector contain the Update Sequence Number. If not then return FALSE.
        if (buffer_words[index] != update_sequence_array[0]) {
            gui->show_debug(
                    DebugLevel::Progress, nullptr,
                    L"Error: USA fixup word is not equal to the Update Sequence Number, the MFT may be corrupt.");

            return false;
        }

        // Replace the last 2 bytes in the sector with the value from the Usa array
        buffer_words[index] = update_sequence_array[i];
        index = index + increment;
    }

    return true;
}
