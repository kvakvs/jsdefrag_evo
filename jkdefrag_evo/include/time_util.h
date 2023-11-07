#pragma once

#include <chrono>

using Clock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;

// 10-millionths of a second, 100ns resolution
using filetime64_t =
        std::chrono::duration<uint64_t, std::ratio_multiply<std::ratio<100>, std::nano>>;

// Millionths of a second
using micro64_t = std::chrono::duration<uint64_t, std::micro>;

// Thousandths of a second
using milli64_t = std::chrono::duration<uint64_t, std::milli>;

filetime64_t from_system_time();

filetime64_t from_FILETIME(FILETIME &ft);

// Starts on construction, stops on destruction.
// Calling stop_and_log() writes down the summary.
// Calling start() when paused, stores the diff calculated so far, and continues clocking, also increases start count.
class StopWatch {
public:
    explicit StopWatch(const wchar_t *description, bool start = true);

    virtual ~StopWatch();

    void pause();

    void start();

    void stop_and_log();

private:
    bool running_;
    // Store 1 because it starts immediately
    uint64_t start_count_{};
    std::wstring description_;
    Clock::time_point start_clock_;
    // Accumulated diff, in case we continue() after stopping.
    Clock::duration diff_accrued_{};
};