#pragma once

enum class DebugLevel {
    // Fatal errors, final stats, etc
    AlwaysLog = 0,
    // Warning messages
    Warning = 1,
    // General progress messages.
    Progress = 2,
    // Detailed progress messages
    DetailedProgress = 3,
    // Detailed file information.
    DetailedFileInfo = 4,
    // Detailed gap-filling messages.
    DetailedGapFilling = 5,
    // Detailed gap-finding messages.
    DetailedGapFinding = 6,
    Debug = 7,
};

class DefragLog {
public:
    DefragLog();

    static DefragLog *get_instance();

    void log(std::wstring &&text) const { log(text.c_str()); }

    void log(std::wstring &text) const { log(text.c_str()); }

    void log(const wchar_t *line) const;

    void set_log_filename(const wchar_t *file_name);

    std::wstring my_name_;
    std::wstring my_short_name_;

    const wchar_t *get_log_filename();


private:
    errno_t open_log_append(FILE *&fout) const;

    static void write_timestamp(FILE *fout);

    static void flush_close_log(FILE *fout);

public:
    inline static DebugLevel debug_level_ = {};

private:
    std::wstring log_file_;

    inline static std::unique_ptr<DefragLog> instance_;
};

namespace Log {
    inline void log(const DebugLevel level, std::wstring &&text) {
        if (level <= DefragLog::debug_level_) {
            DefragLog().log(std::move(text));
        }
    }

    inline void log(const DebugLevel level, const std::wstring &text) {
        if (level <= DefragLog::debug_level_) {
            DefragLog().log(text.c_str());
        }
    }

    inline void log(const DebugLevel level, const wchar_t *text) {
        if (level <= DefragLog::debug_level_) {
            DefragLog().log(text);
        }
    }
}