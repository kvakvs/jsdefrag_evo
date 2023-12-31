#pragma once

#include <memory>

#include "constants.h"

class DefragApp {
public:
    DefragApp();

    ~DefragApp();

    /// Return non-owning pointer to instance of the class
    static DefragApp *get_instance();

    /// Reset the owning instance pointer
    static void release_instance();

    WPARAM start_program(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show);

    static void defrag_thread();

#ifdef _DEBUG

    static LONG __stdcall crash_report(EXCEPTION_POINTERS *exception_info);

#endif

    /// If the defragger is not yet running then return false. If it's already running or if there was an error getting
    /// the processlist then return true.
    [[nodiscard]] bool is_already_running(void) const;

public:
    inline static const wchar_t *versiontext_ = L"JkDefrag Evolution";

private:
    // If not RUNNING then stop defragging.
    RunningState running_state_;
    RunningState i_am_running_;
    DebugLevel debug_level_;

    // Non-owning
    DefragGui *gui_;
    DefragRunner *defrag_lib_;

    // Owning; singleton instance
    inline static std::unique_ptr<DefragApp> instance_;
};
