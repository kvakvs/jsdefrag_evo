#include "std_afx.h"

DefragLog::DefragLog() {
    defrag_lib_ = DefragLib::get_instance();

    GetModuleFileNameW(nullptr, my_name_,MAX_PATH);
    GetShortPathNameW(my_name_, my_short_name_,MAX_PATH);
    GetLongPathNameW(my_short_name_, my_name_,MAX_PATH);

    /* Determine default path to logfile. */
    swprintf_s(log_file_,MAX_PATH, L"%s", my_name_);

    wchar_t* p1 = DefragLib::stristr_w(log_file_, L".exe");

    if (p1 == nullptr) p1 = DefragLib::stristr_w(log_file_, L".scr");

    if (p1 != nullptr) {
        *p1 = '\0';

        wcscat_s(log_file_,MAX_PATH, L".log");
        _wunlink(log_file_);
    }
    else {
        *log_file_ = '\0';
    }
}

void DefragLog::set_log_filename(const wchar_t* file_name) {
    /* Determine default path to logfile. */
    swprintf_s(log_file_,MAX_PATH, L"%s", file_name);
    _wunlink(log_file_);
}

wchar_t* DefragLog::get_log_filename() {
    return log_file_;
}

/* Write a text to the logfile. The parameters are the same as for the "printf"
functions, a Format string and a series of parameters. */
void DefragLog::log_message(const wchar_t* format, ...) const {
    va_list var_args;
    FILE* fout;
    time_t now;
    tm now_tm{};

    /* If there is no message then return. */
    if (format == nullptr) return;

    /* If there is no logfile then return. */
    if (*log_file_ == '\0') return;

    /* Open the logfile. */
    int result = _wfopen_s(&fout, log_file_, L"a, ccs=UTF-8");
    if (result != 0 || fout == nullptr) return;

    /* Write the string to the logfile. */
    time(&now);
    result = localtime_s(&now_tm, &now);
    fwprintf_s(fout, L"%02lu:%02lu:%02lu ", now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);

    va_start(var_args, format);
    vfwprintf_s(fout, format, var_args);
    va_end(var_args);

    fwprintf_s(fout, L"\n");

    /* Close the logfile. */
    fflush(fout);
    fclose(fout);
}
