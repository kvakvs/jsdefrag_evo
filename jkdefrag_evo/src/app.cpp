
/*
 JkDefrag  --  Defragment and optimize all harddisks.

 This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 Public License as published by the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 For the full text of the license see the "License gpl.txt" file.

 Jeroen C. Kessels, Internet Engineer
 http://www.kessels.com/
 */

#include "precompiled_header.h"
#include "app.h"

#include <ctime>
#include <memory>

DefragApp::DefragApp()
        : running_state_(RunningState::STOPPED),
          i_am_running_(RunningState::STOPPED),
          debug_level_(DebugLevel::Warning) {
    gui_ = DefragGui::get_instance();
    defrag_lib_ = DefragLib::get_instance();
    log_ = std::make_unique<DefragLog>();
    defrag_struct_ = std::make_unique<DefragStruct>();
}

DefragApp::~DefragApp() = default;

DefragApp *DefragApp::get_instance() {
    if (instance_ == nullptr) {
        instance_ = std::make_unique<DefragApp>();
    }

    return instance_.get();
}

void DefragApp::release_instance() {
    instance_.reset();
}

WPARAM DefragApp::start_program(HINSTANCE instance,
                                [[maybe_unused]] HINSTANCE prev_instance,
                                [[maybe_unused]] LPSTR cmd_line,
                                const int cmd_show) {
    i_am_running_ = RunningState::RUNNING;

    // Test if another instance is already running
    if (this->is_already_running()) return 0;

#ifdef _DEBUG
    // Setup crash report handler
    SetUnhandledExceptionFilter(&DefragApp::crash_report);
#endif

    gui_->initialize(instance, cmd_show, log_.get(), debug_level_);

    // Start up the defragmentation and timer threads
    std::thread defrag_thread_object(&DefragApp::defrag_thread);

    const WPARAM w_param = gui_->do_modal();

    // If the defragger is still running then ask & wait for it to stop
    i_am_running_ = RunningState::STOPPED;

    DefragLib::stop_jk_defrag(&running_state_, 0);

    // Wait for the defrag thread
    defrag_thread_object.join();

    return w_param;
}


#ifdef _DEBUG

// Write a crash report to the log.
// To test the crash handler add something like this: char *p1; p1 = 0; *p1 = 0;
LONG __stdcall DefragApp::crash_report(EXCEPTION_POINTERS *exception_info) {
    IMAGEHLP_LINE64 source_line;
    DWORD line_displacement;
    STACKFRAME64 stack_frame;
    uint32_t image_type;
    char s1[BUFSIZ];

    DefragLog *log = instance_->log_.get();
    const DefragLib *defrag_lib = instance_->defrag_lib_;

    // Exit if we're running inside a debugger
    //  if (IsDebuggerPresent() == TRUE) return(EXCEPTION_EXECUTE_HANDLER);

    log->log(L"I have crashed!");
    log->log(std::format(L"  Command line: {}", GetCommandLineW()).c_str());

    // Show the type of exception
    switch (exception_info->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
            strcpy_s(s1, BUFSIZ, "ACCESS_VIOLATION (the memory could not be read or written)");
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            strcpy_s(
                    s1, BUFSIZ,
                    "DATATYPE_MISALIGNMENT (a datatype misalignment error was detected in a load or store instruction)");
            break;
        case EXCEPTION_BREAKPOINT:
            strcpy_s(s1, BUFSIZ, "BREAKPOINT");
            break;
        case EXCEPTION_SINGLE_STEP:
            strcpy_s(s1, BUFSIZ, "SINGLE_STEP");
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            strcpy_s(s1, BUFSIZ, "ARRAY_BOUNDS_EXCEEDED");
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            strcpy_s(s1, BUFSIZ, "FLT_DENORMAL_OPERAND");
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            strcpy_s(s1, BUFSIZ, "FLT_DIVIDE_BY_ZERO");
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            strcpy_s(s1, BUFSIZ, "FLT_INEXACT_RESULT");
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            strcpy_s(s1, BUFSIZ, "FLT_INVALID_OPERATION");
            break;
        case EXCEPTION_FLT_OVERFLOW:
            strcpy_s(s1, BUFSIZ, "FLT_OVERFLOW");
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            strcpy_s(s1, BUFSIZ, "FLT_STACK_CHECK");
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            strcpy_s(s1, BUFSIZ, "FLT_UNDERFLOW");
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            strcpy_s(s1, BUFSIZ, "INT_DIVIDE_BY_ZERO");
            break;
        case EXCEPTION_INT_OVERFLOW:
            strcpy_s(s1, BUFSIZ, "INT_OVERFLOW");
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            strcpy_s(s1, BUFSIZ, "PRIV_INSTRUCTION");
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            strcpy_s(s1, BUFSIZ, "IN_PAGE_ERROR");
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            strcpy_s(s1, BUFSIZ, "ILLEGAL_INSTRUCTION");
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            strcpy_s(s1, BUFSIZ, "NONCONTINUABLE_EXCEPTION");
            break;
        case EXCEPTION_STACK_OVERFLOW:
            strcpy_s(s1, BUFSIZ, "STACK_OVERFLOW");
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            strcpy_s(s1, BUFSIZ, "INVALID_DISPOSITION");
            break;
        case EXCEPTION_GUARD_PAGE:
            strcpy_s(s1, BUFSIZ, "GUARD_PAGE");
            break;
        case EXCEPTION_INVALID_HANDLE:
            strcpy_s(s1, BUFSIZ, "INVALID_HANDLE");
            break;
        case CONTROL_C_EXIT:
            strcpy_s(s1, BUFSIZ, "STATUS_CONTROL_C_EXIT");
            break;
        case DBG_TERMINATE_THREAD:
            strcpy_s(s1, BUFSIZ, "DBG_TERMINATE_THREAD (Debugger terminated thread)");
            break;
        case DBG_TERMINATE_PROCESS:
            strcpy_s(s1, BUFSIZ, "DBG_TERMINATE_PROCESS (Debugger terminated process)");
            break;
        case DBG_CONTROL_C:
            strcpy_s(s1, BUFSIZ, "DBG_CONTROL_C (Debugger got control C)");
            break;
        case DBG_CONTROL_BREAK:
            strcpy_s(s1, BUFSIZ, "DBG_CONTROL_BREAK (Debugger received control break)");
            break;
        case DBG_COMMAND_EXCEPTION:
            strcpy_s(s1, BUFSIZ, "DBG_COMMAND_EXCEPTION (Debugger command communication exception)");
            break;
        default:
            strcpy_s(s1, BUFSIZ, "(unknown exception)");
    }

    log->log(std::format(L"  Exception: {}", Str::from_char(s1)));

    // Try to show the linenumber of the sourcefile
    SymSetOptions(SymGetOptions() || SYMOPT_LOAD_LINES);
    BOOL result = SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    if (result == FALSE) {
        log->log(std::format(L"  Failed to initialize SymInitialize(): {}",
                             DefragLib::system_error_str(GetLastError())));
        return EXCEPTION_EXECUTE_HANDLER;
    }

    ZeroMemory(&stack_frame, sizeof stack_frame);

#ifdef _M_IX86
    image_type = IMAGE_FILE_MACHINE_I386;
    stack_frame.AddrPC.Offset = exception_info->ContextRecord->Eip;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = exception_info->ContextRecord->Ebp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = exception_info->ContextRecord->Esp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
    image_type = IMAGE_FILE_MACHINE_AMD64;
    stack_frame.AddrPC.Offset = exception_info->ContextRecord->Rip;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = exception_info->ContextRecord->Rsp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = exception_info->ContextRecord->Rsp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#elif _M_IA64
    image_type = IMAGE_FILE_MACHINE_IA64;
    stack_frame.AddrPC.Offset = exception_info->ContextRecord->StIIP;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = exception_info->ContextRecord->IntSp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrBStore.Offset = exception_info->ContextRecord->RsBSP;
    stack_frame.AddrBStore.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = exception_info->ContextRecord->IntSp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#endif
    for (int frame_number = 1;; frame_number++) {
        result = StackWalk64(image_type, GetCurrentProcess(), GetCurrentThread(), &stack_frame,
                             exception_info->ContextRecord, nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                             nullptr);
        if (result == FALSE) break;
        if (stack_frame.AddrPC.Offset == stack_frame.AddrReturn.Offset) break;
        if (stack_frame.AddrPC.Offset != 0) {
            line_displacement = 0;
            ZeroMemory(&source_line, sizeof source_line);
            source_line.SizeOfStruct = sizeof source_line;
            result = SymGetLineFromAddr64(GetCurrentProcess(), stack_frame.AddrPC.Offset,
                                          &line_displacement, &source_line);
            if (result == TRUE) {
                log->log(std::format(L"  Frame {}. At line {} in '{}'", frame_number, source_line.LineNumber,
                                     Str::from_char(source_line.FileName)));
            } else {
                log->log(std::format(L"  Frame {}. At line (unknown) in (unknown)", frame_number));
                /*
                SystemErrorStr(GetLastError(),s2,BUFSIZ);
                LogMessage(L"  Error executing SymGetLineFromAddr64(): %s",s2);
                */
            }
        }
    }

    /* Possible return values:
    EXCEPTION_CONTINUE_SEARCH    = popup a window about the error, user has to click.
    EXCEPTION_CONTINUE_EXECUTION = infinite loop
    EXCEPTION_EXECUTE_HANDLER    = stop program, do not run debugger
    */
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif

// The main thread that performs all the work. Interpret the commandline
// parameters and call the defragger library.
// DWORD WINAPI Defrag::defrag_thread(LPVOID) {
void DefragApp::defrag_thread() {
    OptimizeMode optimize_mode = {}; // 1...11
    LPWSTR *argv;
    int argc;
    std::time_t now;
    std::tm now_tm{};
    OSVERSIONINFO os_version;
    int i;

    DefragLog *log = instance_->log_.get();
    DefragStruct *defrag_struct = instance_->defrag_struct_.get();
    DefragGui *gui = instance_->gui_;
    DefragLib *defrag_lib = instance_->defrag_lib_;

    // Setup the defaults
    optimize_mode = OptimizeMode::AnalyzeFixupFastopt;
    // Range 0...100 
    int speed = 100;
    double free_space = 1;
    Wstrings excludes;
    Wstrings space_hogs;
    bool quit_on_finish = false;

    // Fetch the commandline
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // Scan the commandline arguments for "-l" and setup the logfile
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"-l") == 0) {
                i++;
                if (i >= argc) continue;

                log->set_log_filename(argv[i]);

                continue;
            }
            if (wcsncmp(argv[i], L"-l", 2) == 0 && wcslen(argv[i]) >= 3) {
                log->set_log_filename(&argv[i][2]);

                continue;
            }
        }
    }

    // Show some standard information in the logfile
    log->log(defrag_struct->versiontext_.c_str());
    time(&now);

    localtime_s(&now_tm, &now);
    log->log(std::format(L"Date: {:04}/{:02}/{:02}", 1900 + now_tm.tm_year, 1 + now_tm.tm_mon, now_tm.tm_mday));

    ZeroMemory(&os_version, sizeof(OSVERSIONINFO));
    os_version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if (GetVersionEx(&os_version) != 0) {
        log->log(std::format(L"Windows version: v{}.{} build {} {}", os_version.dwMajorVersion,
                             os_version.dwMinorVersion, os_version.dwBuildNumber,
                             Str::from_char(os_version.szCSDVersion)));
    }

    // Scan the commandline again for all the other arguments
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"-a") == 0) {
                i++;

                if (i >= argc) {
                    gui->show_debug(
                            DebugLevel::Fatal, nullptr,
                            L"Error: you have not specified a number after the \"-a\" commandline argument.");

                    continue;
                }

                optimize_mode = (OptimizeMode) _wtol(argv[i]);

                if (optimize_mode < OptimizeMode::AnalyzeOnly || optimize_mode >= OptimizeMode::Max) {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    L"Error: the number after the \"-a\" commandline argument is invalid.");

                    optimize_mode = OptimizeMode::DeprecatedAnalyzeFixupFull;
                }

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-a' accepted, optimizemode = {}",
                                            (int) optimize_mode));

                continue;
            }

            if (wcsncmp(argv[i], L"-a", 2) == 0) {
                optimize_mode = (OptimizeMode) _wtol(&argv[i][2]);

                if (optimize_mode < OptimizeMode::AnalyzeOnly || optimize_mode >= OptimizeMode::Max) {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    L"Error: the number after the \"-a\" commandline argument is invalid.");

                    optimize_mode = OptimizeMode::DeprecatedAnalyzeFixupFull;
                }

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-a' accepted, optimizemode = {}",
                                            (int) optimize_mode));
                continue;
            }

            if (wcscmp(argv[i], L"-s") == 0) {
                i++;

                if (i >= argc) {
                    gui->show_debug(
                            DebugLevel::Fatal, nullptr,
                            L"Error: you have not specified a number after the \"-s\" commandline argument.");

                    continue;
                }

                speed = _wtol(argv[i]);

                if (speed < 1 || speed > 100) {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    L"Error: the number after the \"-s\" commandline argument is invalid.");

                    speed = 100;
                }

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-s' accepted, slowing down to {}%", speed));

                continue;
            }

            if (wcsncmp(argv[i], L"-s", 2) == 0 && wcslen(argv[i]) >= 3) {
                speed = _wtol(&argv[i][2]);

                if (speed < 1 || speed > 100) {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    L"Error: the number after the \"-s\" commandline argument is invalid.");

                    speed = 100;
                }

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-s' accepted, speed = {}%", speed));
                continue;
            }

            if (wcscmp(argv[i], L"-f") == 0) {
                i++;

                if (i >= argc) {
                    gui->show_debug(
                            DebugLevel::Fatal, nullptr,
                            L"Error: you have not specified a number after the \"-f\" commandline argument.");

                    continue;
                }

                free_space = _wtof(argv[i]);

                if (free_space < 0 || free_space > 100) {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    L"Error: the number after the \"-f\" commandline argument is invalid.");

                    free_space = 1;
                }

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-f' accepted, freespace = {:.1f}%", free_space));
                continue;
            }

            if (wcsncmp(argv[i], L"-f", 2) == 0 && wcslen(argv[i]) >= 3) {
                free_space = _wtof(&argv[i][2]);

                if (free_space < 0 || free_space > 100) {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    L"Error: the number after the \"-f\" command line argument is invalid.");

                    free_space = 1;
                }

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Command line argument '-f' accepted, free space = {:.1f}%", free_space));

                continue;
            }

            if (wcscmp(argv[i], L"-d") == 0) {
                i++;

                if (i >= argc) {
                    gui->show_debug(
                            DebugLevel::Fatal, nullptr,
                            L"Error: you have not specified a number after the \"-d\" commandline argument.");

                    continue;
                }

                instance_->debug_level_ = (DebugLevel) _wtol(argv[i]);

                if (instance_->debug_level_ < DebugLevel::Fatal ||
                    instance_->debug_level_ > DebugLevel::DetailedGapFinding) {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    L"Error: the number after the \"-d\" commandline argument is invalid.");

                    instance_->debug_level_ = DebugLevel::Warning;
                }

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-d' accepted, debug level set to {}",
                                            (int) instance_->debug_level_));
                continue;
            }

            if (wcsncmp(argv[i], L"-d", 2) == 0 && wcslen(argv[i]) == 3 &&
                argv[i][2] >= '0' && argv[i][2] <= '6') {
                instance_->debug_level_ = (DebugLevel) _wtol(&argv[i][2]);

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-d' accepted, debug level set to {}",
                                            (int) instance_->debug_level_));
                continue;
            }

            if (wcscmp(argv[i], L"-l") == 0) {
                i++;

                if (i >= argc) {
                    gui->show_debug(
                            DebugLevel::Fatal, nullptr,
                            L"Error: you have not specified a filename after the \"-l\" commandline argument.");

                    continue;
                }

                auto log_file = log->get_log_filename();

                if (*log_file != '\0') {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    std::format(L"Commandline argument '-l' accepted, logfile = {}", log_file));
                } else {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    L"Commandline argument '-l' accepted, logfile turned off");
                }

                continue;
            }

            if (wcsncmp(argv[i], L"-l", 2) == 0 && wcslen(argv[i]) >= 3) {
                auto log_file = log->get_log_filename();

                if (*log_file != '\0') {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    std::format(L"Commandline argument '-l' accepted, logfile = {}", log_file));
                } else {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    L"Commandline argument '-l' accepted, logfile turned off");
                }

                continue;
            }

            if (wcscmp(argv[i], L"-e") == 0) {
                i++;

                if (i >= argc) {
                    gui->show_debug(
                            DebugLevel::Fatal, nullptr,
                            L"Error: you have not specified a mask after the \"-e\" commandline argument.");

                    continue;
                }

                excludes.emplace_back(argv[i]);

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-e' accepted, added '{}' to the excludes",
                                            argv[i]));

                continue;
            }

            if (wcsncmp(argv[i], L"-e", 2) == 0 && wcslen(argv[i]) >= 3) {
                excludes.emplace_back(&argv[i][2]);

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-e' accepted, added '{}' to the excludes",
                                            &argv[i][2]));

                continue;
            }

            if (wcscmp(argv[i], L"-u") == 0) {
                i++;

                if (i >= argc) {
                    gui->show_debug(
                            DebugLevel::Fatal, nullptr,
                            L"Error: you have not specified a mask after the \"-u\" commandline argument.");

                    continue;
                }

                space_hogs.emplace_back(argv[i]);

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-u' accepted, added '{}' to the spacehogs",
                                            argv[i]));

                continue;
            }

            if (wcsncmp(argv[i], L"-u", 2) == 0 && wcslen(argv[i]) >= 3) {
                space_hogs.emplace_back(&argv[i][2]);

                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Commandline argument '-u' accepted, added '{}' to the spacehogs",
                                            &argv[i][2]));

                continue;
            }

            if (wcscmp(argv[i], L"-q") == 0) {
                quit_on_finish = true;

                gui->show_debug(DebugLevel::Fatal, nullptr, L"Commandline argument '-q' accepted, quitonfinish = yes");

                continue;
            }

            if (argv[i][0] == '-') {
                gui->show_debug(DebugLevel::Fatal, nullptr,
                                std::format(L"Error: commandline argument not recognised: {}", argv[i]));
            }
        }
    }

    // Defragment all the paths that are specified on the commandline one by one
    bool do_all_volumes = true;

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (instance_->i_am_running_ != RunningState::RUNNING) break;

            if (wcscmp(argv[i], L"-a") == 0 ||
                wcscmp(argv[i], L"-e") == 0 ||
                wcscmp(argv[i], L"-u") == 0 ||
                wcscmp(argv[i], L"-s") == 0 ||
                wcscmp(argv[i], L"-f") == 0 ||
                wcscmp(argv[i], L"-d") == 0 ||
                wcscmp(argv[i], L"-l") == 0) {
                i++;
                continue;
            }

            if (*argv[i] == '-') continue;
            if (*argv[i] == '\0') continue;

            defrag_lib->run_jk_defrag(argv[i], optimize_mode, speed, free_space, excludes, space_hogs,
                                      &instance_->running_state_);

            do_all_volumes = false;
        }
    }

    // If no paths are specified on the commandline then defrag all fixed harddisks
    if (do_all_volumes && instance_->i_am_running_ == RunningState::RUNNING) {
        defrag_lib->run_jk_defrag(nullptr, optimize_mode, speed, free_space, excludes, space_hogs,
                                  &instance_->running_state_);
    }

    // If the "-q" command line argument was specified then exit the program
    if (quit_on_finish) exit(EXIT_SUCCESS);

    // End of this thread
    // return 0;
}

/*

If the defragger is not yet running then return false. If it's already
running or if there was an error getting the processlist then return
true.

*/
bool DefragApp::is_already_running(void) const {
    // Get a process-snapshot from the kernel
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        auto err = std::format(L"Cannot get process snapshot: {}", DefragLib::system_error_str(GetLastError()));
        gui_->show_debug(DebugLevel::Fatal, nullptr, std::move(err));
        return true;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    // Get my own executable name
    const uint32_t my_pid = GetCurrentProcessId();
    char my_name[MAX_PATH];

    *my_name = '\0';

    if (Process32First(snapshot, &pe32) != FALSE) {
        do {
            if (my_pid == pe32.th32ProcessID) {
                strcpy_s(my_name, MAX_PATH, pe32.szExeFile);
                break;
            }
        } while (Process32Next(snapshot, &pe32));
    }

    if (*my_name == '\0') {
        // "Cannot find my own name in the process list: %s"
        auto s1 = std::format(L"Cannot find my own name in the process list: {}", Str::from_char(my_name));
        gui_->show_debug(DebugLevel::Fatal, nullptr, std::move(s1));
        return true;
    }

    // Search for any other process with the same executable name as myself. If found then return true.
    Process32First(snapshot, &pe32);

    do {
        if (my_pid == pe32.th32ProcessID) continue; // Ignore myself

        if (_stricmp(pe32.szExeFile, my_name) == 0 ||
            _stricmp(pe32.szExeFile, EXECUTABLE_NAME) == 0 ||
            _stricmp(pe32.szExeFile, SCREENSAVER_NAME) == 0 ||
            _stricmp(pe32.szExeFile, CMD_EXECUTABLE_NAME) == 0) {
            CloseHandle(snapshot);

            auto s1 = std::format(L"I am already running: {}", Str::from_char(pe32.szExeFile));

            gui_->show_debug(DebugLevel::Fatal, nullptr, std::move(s1));
            return true;
        }
    } while (Process32Next(snapshot, &pe32));

    // Return false, not yet running
    CloseHandle(snapshot);
    return false;
}
