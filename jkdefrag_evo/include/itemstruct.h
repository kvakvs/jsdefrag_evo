#pragma once

#include "types.h"
#include "time_util.h"
#include "constants.h"

#include <optional>

// List in memory of the fragments of a file
struct FragmentListStruct {
    uint64_t lcn_; // Logical cluster number, location on disk
    uint64_t next_vcn_; // Virtual cluster number of next fragment
    FragmentListStruct *next_;
};


// List in memory of all the files on disk, sorted by LCN (Logical Cluster Number)
struct ItemStruct {
public:
    void set_names(const wchar_t *long_path, const wchar_t *long_filename, const wchar_t *short_path,
                   const wchar_t *short_filename);

    ItemStruct() = default;

    virtual ~ItemStruct();

    // Tree node location type
    using TreeLcn = uint64_t;
//    using TreeLcn = struct {
//        uint64_t lcn;
//    };

    // Return the location on disk (LCN, Logical Cluster Number) of an item
    TreeLcn get_item_lcn() const {
        // Sanity check
        if (this == nullptr) return 0;

        const FragmentListStruct *fragment = fragments_;

        while (fragment != nullptr && fragment->lcn_ == VIRTUALFRAGMENT) {
            fragment = fragment->next_;
        }
        return fragment == nullptr ? 0 : fragment->lcn_;
    }

public:
    ItemStruct *parent_;
    // Next smaller item
    ItemStruct *smaller_;
    // Next bigger item
    ItemStruct *bigger_;

    uint64_t bytes_;
    uint64_t clusters_count_;
    filetime64_t creation_time_;
    filetime64_t mft_change_time_;
    filetime64_t last_access_time_;

    // List of fragments
    // TODO: Owning pointer
    FragmentListStruct *fragments_;

    // The Inode number of the parent directory
    uint64_t parent_inode_;

    ItemStruct *parent_directory_;

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
