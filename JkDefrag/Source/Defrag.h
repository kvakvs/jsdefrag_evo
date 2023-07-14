#pragma once

#include <memory>

#include "constants.h"

class Defrag {
public:
    Defrag();
    ~Defrag();

    // Return non-owning pointer to instance of the class
    static Defrag* get_instance();
    // Reset the owning instance pointer
    static void release_instance();

    WPARAM start_program(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show);

    static DWORD WINAPI defrag_thread(LPVOID);

#ifdef _DEBUG
    static LONG __stdcall crash_report(EXCEPTION_POINTERS* exception_info);
#endif

    [[nodiscard]] bool is_already_running(void) const;

private:
    // If not RUNNING then stop defragging. 
    RunningState running_state_;
    RunningState i_am_running_;
    DebugLevel debug_level_;

    std::unique_ptr<DefragGui> gui_;
    std::unique_ptr<DefragLib> defrag_lib_;
    std::unique_ptr<DefragLog> log_;
    std::unique_ptr<DefragStruct> defrag_struct_;

    // static member that is an instance of itself
    inline static std::unique_ptr<Defrag> instance_;
};
