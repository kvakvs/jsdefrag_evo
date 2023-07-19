#include "precompiled_header.h"
#include "time_util.h"

filetime64_t from_system_time() {
    FILETIME system_time1;

    GetSystemTimeAsFileTime(&system_time1);

    ULARGE_INTEGER u;

    u.LowPart = system_time1.dwLowDateTime;
    u.HighPart = system_time1.dwHighDateTime;
    return filetime64_t(u.QuadPart);
}

filetime64_t from_FILETIME(FILETIME &ft) {
    ULARGE_INTEGER u;

    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;

    return filetime64_t(u.QuadPart);
}

StopWatch::StopWatch(const wchar_t *description) : description_(description), running_(true) {
    start_ = Clock::now();
}

StopWatch::~StopWatch() {
    if (running_) stop_and_log();
}

void StopWatch::stop_and_log() {
    pause();

    auto diff_ms = std::chrono::duration_cast<std::chrono::microseconds>(diff_accrued_);

    Log::log_always(std::format(L"StopWatch [{}] took " NUM_FMT L" microsec over {} calls",
                                description_.c_str(), diff_ms.count(), start_count_));
}

void StopWatch::pause() {
    if (!running_) return;

    running_ = false;

    diff_accrued_ += (Clock::now() - start_);
}

void StopWatch::start() {
    if (running_) return;

    running_ = true;
    start_count_++;

    start_ = Clock::now();
}
