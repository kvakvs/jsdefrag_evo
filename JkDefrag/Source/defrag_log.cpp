#include "std_afx.h"

#include <ctime>
#include <cstdarg>

DefragLog::DefragLog() {
    defrag_lib_ = DefragLib::get_instance();

    wchar_t name_buf[MAX_PATH];
    wchar_t s_name_buf[MAX_PATH];

    GetModuleFileNameW(nullptr, name_buf, sizeof(name_buf));
    GetShortPathNameW(name_buf, s_name_buf, sizeof(s_name_buf));
    GetLongPathNameW(s_name_buf, name_buf, sizeof(name_buf));

    my_name_ = name_buf;
    my_short_name_ = s_name_buf;

    // Determine default path to logfile
    log_file_ = my_name_;

    const wchar_t *p1 = DefragLib::stristr_w(log_file_.c_str(), L".exe");

    if (p1 == nullptr) {
        p1 = DefragLib::stristr_w(log_file_.c_str(), L".scr");
        log_file_.clear();
    } else {
//        *p1 = '\0';
        log_file_ += L".log";
        _wunlink(log_file_.c_str());
    }
}

void DefragLog::set_log_filename(const wchar_t *file_name) {
    /* Determine default path to logfile. */
    log_file_ = file_name;
    _wunlink(file_name);
}

const wchar_t *DefragLog::get_log_filename() {
    return log_file_.c_str();
}

/* Write a text to the logfile. The parameters are the same as for the "printf"
functions, a Format string and a series of parameters. */
void DefragLog::log_message(const wchar_t *format, ...) const {

    // If there is no message then return
    if (format == nullptr) return;

    // If there is no logfile then return
    if (log_file_.empty()) return;

    // Open the logfile
    FILE *fout;
    int result = open_log_append(fout);
    if (result != 0 || fout == nullptr) return;

    result = write_timestamp(fout);

    // Write to logfile
    std::va_list var_args;
    va_start(var_args, format);
    vfwprintf_s(fout, format, var_args);
    va_end(var_args);

    fwprintf_s(fout, L"\n");

    // Close the logfile
    flush_close_log(fout);
}

/// \brief Write a pre-formatted line to the logfile, prefixed by timestamp
void DefragLog::log_string(const wchar_t *line) const {

    // If there is no logfile then return
    if (log_file_.empty()) return;

    // Open the logfile
    FILE *fout;
    int result = open_log_append(fout);
    if (result != 0 || fout == nullptr) return;

    result = write_timestamp(fout);

    // Write to logfile
    fwprintf_s(fout, L"%s\n", line);

    // Close the logfile
    flush_close_log(fout);
}

void DefragLog::flush_close_log(FILE *fout) {
    fflush(fout);
    fclose(fout);
}

/// \return Errno, or 0 on success. Fout is set to the file handle.
errno_t DefragLog::open_log_append(FILE *&fout) const {
    return _wfopen_s(&fout, log_file_.c_str(), L"a, ccs=UTF-8");
}

int DefragLog::write_timestamp(FILE *fout) {
    // Write the string to the logfile.
    std::time_t now = std::time(nullptr);
    std::tm now_tm{};
    auto result = localtime_s(&now_tm, &now);
    fwprintf_s(fout, L"%02lu:%02lu:%02lu ", now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
    return result;
}
