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
    // Log all, also is used as max value for range comparisons
    Debug = 7,
};

class DefragLog {
public:
    DefragLog();

    static DefragLog *get_instance();

    // Log without checking debug level
    void log_always(std::wstring &&text) const { log_always(text.c_str()); }

    // Log without checking debug level
    void log_always(std::wstring &text) const { log_always(text.c_str()); }

    // Log without checking debug level
    void log_always(const wchar_t *line) const;

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
    // Do not check debug level, equal to DebugLevel::AlwaysLog
    template<typename T>
    void log_always(T text) {
        DefragLog::get_instance()->log_always(std::move(text));
    }

    inline void log(const DebugLevel level, std::wstring &&text) {
        if (level <= DefragLog::debug_level_) {
            DefragLog::get_instance()->log_always(std::move(text));
        }
    }

    inline void log(const DebugLevel level, const std::wstring &text) {
        if (level <= DefragLog::debug_level_) {
            DefragLog::get_instance()->log_always(text.c_str());
        }
    }

    inline void log(const DebugLevel level, const wchar_t *text) {
        if (level <= DefragLog::debug_level_) {
            DefragLog::get_instance()->log_always(text);
        }
    }
}