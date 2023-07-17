
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

    // Initialize the GUI and start update timer (sends WM_TIMER)
    gui_->initialize(instance, cmd_show, log_.get(), debug_level_);

    // Start up the defragmentation thread
    std::thread defrag_thread_object(&DefragApp::defrag_thread);

    // Message handling loop (main thread)
    const WPARAM w_param = gui_->windows_event_loop();

    // If the defragger is still running then ask & wait for it to stop
    i_am_running_ = RunningState::STOPPED;

    DefragLib::stop_jk_defrag(&running_state_, 0);

    // Wait for the defrag thread
    defrag_thread_object.join();

    return w_param;
}

// The main thread that performs all the work. Interpret the commandline
// parameters and call the defragger library.
// DWORD WINAPI Defrag::defrag_thread(LPVOID) {
void DefragApp::defrag_thread() {
//    std::time_t now;
//    std::tm now_tm{};
    OSVERSIONINFO os_version;
    int i;

    DefragLog *log = instance_->log_.get();
    DefragStruct *defrag_struct = instance_->defrag_struct_.get();
    DefragGui *gui = instance_->gui_;
    DefragLib *defrag_lib = instance_->defrag_lib_;

    // Setup the defaults
    OptimizeMode optimize_mode = OptimizeMode::AnalyzeFixupFastopt;
    // Range 0...100 
    int speed = 100;
    double free_space = 1;
    Wstrings excludes;
    Wstrings space_hogs;
    bool quit_on_finish = false;

    // Fetch the commandline
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

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
//    time(&now);
//    localtime_s(&now_tm, &now);
    auto now = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
    log->log(std::format(L"Date: {:%Y-%m-%d %X}", now));

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
