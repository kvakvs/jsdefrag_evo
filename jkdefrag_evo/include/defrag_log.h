#pragma once

class DefragLog {
public:
    DefragLog();

    void log(std::wstring &&text) const { log(text.c_str()); }

    void log(std::wstring &text) const { log(text.c_str()); }

    void log(const wchar_t *line) const;

    void set_log_filename(const wchar_t *file_name);

    std::wstring my_name_;
    std::wstring my_short_name_;

    const wchar_t *get_log_filename();

private:
    std::wstring log_file_;

    errno_t open_log_append(FILE *&fout) const;

    static void write_timestamp(FILE *fout);

    static void flush_close_log(FILE *fout);
};
