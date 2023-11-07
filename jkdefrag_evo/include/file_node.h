#pragma once

#include "types.h"
#include "time_util.h"
#include "constants.h"

#include <optional>
#include <list>

/// File fragment descriptor, stored as a list of file fragments
// TODO: Storage as std::forward_list
struct FileFragment {
    lcn64_t lcn_; // Logical cluster number, location on disk
    vcn64_t next_vcn_; // Virtual cluster number of next fragment

    void set_virtual() {
        lcn_ = VIRTUALFRAGMENT;
    }

    [[nodiscard]] bool is_virtual() const {
        return lcn_ == VIRTUALFRAGMENT;
    }

private:
    static constexpr vcn64_t VIRTUALFRAGMENT = std::numeric_limits<vcn64_t>::max();
};


/// List in memory of all the files on disk, sorted by LCN (Logical Cluster Number)
struct FileNode {
public:
    void set_names(const wchar_t *long_path, const wchar_t *long_filename, const wchar_t *short_path,
                   const wchar_t *short_filename);

    FileNode() = default;

    virtual ~FileNode();

    // Tree node location type
    using TreeLcn = lcn64_t;
//    using TreeLcn = struct {
//        uint64_t lcn;
//    };

    // Return the location on disk (LCN, Logical Cluster Number) of an item
    [[nodiscard]] TreeLcn get_item_lcn() const {
        // Sanity check
        if (this == nullptr) return 0;

        auto fragment = fragments_.begin();

        // Skip forward over virtual fragments
        while (fragment != fragments_.end() && fragment->is_virtual()) {
            fragment++;
        }

        // Return 0 if the fragment list end was reached without finding a real fragment
        return fragment == fragments_.end() ? 0 : fragment->lcn_;
    }

    [[nodiscard]] Zone get_preferred_zone() const {
        if (is_dir_) return Zone::ZoneFirst;
        if (is_hog_) return Zone::ZoneLast;
        return Zone::ZoneCommon;
    }

public:
    FileNode *parent_ = nullptr;
    // Next smaller item
    FileNode *smaller_ = nullptr;
    // Next bigger item
    FileNode *bigger_ = nullptr;

    uint64_t bytes_;
    cluster_count64_t clusters_count_;
    filetime64_t creation_time_;
    filetime64_t mft_change_time_;
    filetime64_t last_access_time_;

    // List of fragments
    // TODO: Owning pointer
    std::list<FileFragment> fragments_;

    // The Inode number of the parent directory
    inode_t parent_inode_;

    FileNode *parent_directory_;

    bool is_dir_;
    bool is_unmovable_;
    bool is_excluded_;
    // file to be moved to the end of disk
    bool is_hog_;

    void set_long_path(const wchar_t *value) {
        long_path_ = value;
        if (short_path_.has_value() && short_path_.value() == value) {
            short_path_ = std::nullopt;
        }
    }

    void set_short_path(const wchar_t *value) {
        if (long_path_ == value) {
            short_path_ = std::nullopt;
        } else {
            short_path_ = {value};
        }
    }

    [[nodiscard]] bool have_long_fn() const {
        return !long_filename_.empty();
    }

    [[nodiscard]] bool have_long_path() const {
        return !long_path_.empty();
    }

    [[nodiscard]] bool have_short_fn() const {
        return !short_filename_.has_value();
    }

    [[nodiscard]] bool have_short_path() const {
        return !short_path_.has_value();
    }

    [[nodiscard]] const wchar_t *get_long_fn() const {
        return long_filename_.c_str();
    }

    [[nodiscard]] const wchar_t *get_long_path() const {
        return long_path_.c_str();
    }

    void clear_long_fn() {
        long_filename_.clear();
    }

    void clear_long_path() {
        long_path_.clear();
    }

    void set_long_fn(const wchar_t *value) {
        long_filename_ = value;
        if (short_filename_.has_value() && short_filename_.value() == value) {
            short_filename_ = std::nullopt;
        }
    }

    void clear_short_fn() {
        short_filename_ = std::nullopt;
    }

    void clear_short_path() {
        short_path_ = std::nullopt;
    }

    void set_short_fn(const wchar_t *value) {
        if (long_filename_ == value) {
            short_filename_ = std::nullopt;
        } else {
            short_filename_ = {value};
        }
    }

    [[nodiscard]] const wchar_t *get_short_fn() const {
        if (short_filename_.has_value()) {
            return short_filename_.value().c_str();
        }
        return this->get_long_fn();
    }

    [[nodiscard]] const wchar_t *get_short_path() const {
        if (short_path_.has_value()) {
            return short_path_.value().c_str();
        }
        return this->get_long_path();
    }

private:
    std::wstring long_filename_;
    // Full path on disk, long filenames.
    std::wstring long_path_;

    // Short filename(8.3 DOS)
    std::optional<std::wstring> short_filename_;
    // Full path on disk, short filenames
    std::optional<std::wstring> short_path_;
};
