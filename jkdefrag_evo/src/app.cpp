
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

#include "app.h"
#include "precompiled_header.h"

#include <ctime>
#include <functional>
#include <memory>

DefragApp::DefragApp()
    : running_state_(RunningState::STOPPED),
      i_am_running_(RunningState::STOPPED),
      debug_level_(DebugLevel::Warning) {
    gui_ = DefragGui::get_instance();
    defrag_lib_ = DefragRunner::get_instance();
}

DefragApp::~DefragApp() = default;

DefragApp *DefragApp::get_instance() {
    if (instance_ == nullptr) { instance_ = std::make_unique<DefragApp>(); }

    return instance_.get();
}

void DefragApp::release_instance() { instance_.reset(); }

WPARAM DefragApp::start_program(HINSTANCE instance, [[maybe_unused]] HINSTANCE prev_instance,
                                [[maybe_unused]] LPSTR cmd_line, const int cmd_show) {
    i_am_running_ = RunningState::RUNNING;

    // Test if another instance is already running
    if (this->is_already_running()) return 0;

#ifdef _DEBUG
        // Setup crash report handler
        // SetUnhandledExceptionFilter(&DefragApp::crash_report);
#endif

    // Initialize the GUI and start update timer (sends WM_TIMER)
    gui_->initialize(instance, cmd_show, debug_level_);

    // Start up the defragmentation thread
    std::thread defrag_thread_object(&DefragApp::defrag_thread);

    // Message handling loop (main thread)
    const WPARAM w_param = gui_->windows_event_loop();

    // If the defragger is still running then ask & wait for it to stop
    i_am_running_ = RunningState::STOPPED;

    DefragRunner::stop_defrag_sync(&running_state_, SystemClock::duration::zero());

    // Wait for the defrag thread
    defrag_thread_object.join();

    return w_param;
}

static void log_windows_version() {
    OSVERSIONINFO os_version;
    ZeroMemory(&os_version, sizeof(OSVERSIONINFO));
    os_version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if (GetVersionEx(&os_version) != 0) {
        Log::log_always(std::format(L"Windows version: v{}.{} build {} {}",
                                    os_version.dwMajorVersion, os_version.dwMinorVersion,
                                    os_version.dwBuildNumber,
                                    Str::from_char(os_version.szCSDVersion)));
    }

    if (false) {
        OSVERSIONINFOEX ver_info{.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX)};
        DWORDLONG condition_mask = 0;
        VER_SET_CONDITION(condition_mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
        VER_SET_CONDITION(condition_mask, VER_MINORVERSION, VER_GREATER_EQUAL);
        // VER_SET_CONDITION(condition_mask, VER_BUILDNUMBER, VER_EQUAL);

        // we're not picky about the modern version as long as its NTFS
        ver_info.dwMajorVersion = HIBYTE(_WIN32_WINNT_WIN7);
        ver_info.dwMinorVersion = LOBYTE(_WIN32_WINNT_WIN7);
        ver_info.dwBuildNumber = 0;

        if (VerifyVersionInfo(&ver_info, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER,
                              condition_mask)) {
            Log::log_always(static_cast<const std::wstring>(
                    std::format(L"Windows version {}.{} build {}", ver_info.dwMajorVersion,
                                ver_info.dwMinorVersion, ver_info.dwBuildNumber)));
        } else {
            Log::log_always(
                    std::format(L"Failed to verify Windows version information. Error code: {}",
                                Str::system_error(GetLastError())));
        }
    }
}

bool match_argument_with_space(int &i, const int argc, const LPWSTR *argv, const wchar_t *arg_name,
                               const std::function<void(const wchar_t *)> &on_match,
                               const std::function<void()> &on_missing_value) {
    if (wcscmp(argv[i], arg_name) == 0) {
        i++;

        if (i >= argc) {
            on_missing_value();
            return false;// missing value / no match
        }

        on_match(argv[i]);
        return true;
    }
    return false;// no match
}

// The main thread that performs all the work. Interpret the commandline
// parameters and call the defragger library.
// DWORD WINAPI Defrag::defrag_thread(LPVOID) {
void DefragApp::defrag_thread() {
    int i;

    //DefragGui *gui = instance_->gui_;
    DefragRunner *defrag_lib = instance_->defrag_lib_;

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
    auto log = DefragLog::get_instance();

    // Scan the commandline arguments for "-l" and setup the logfile
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            // "-l logfile" argument separated by space
            match_argument_with_space(
                    i, argc, argv, L"-l", [&](const wchar_t *arg) { log->set_log_filename(arg); },
                    [&]() { Log::log_always(L"Missing logfile name after '-l'"); });
        }
    }

    // Show some standard information in the logfile
    Log::log_always(DefragApp::versiontext_);

    auto now = std::chrono::current_zone()->to_local(SystemClock::now());

    Log::log_always(std::format(L"Date: {:%Y-%m-%d %X}", now));

    log_windows_version();

    // Scan the commandline again for all the other arguments
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            // "-a actionmode" argument separated by space
            match_argument_with_space(
                    i, argc, argv, L"-a",
                    [&](const wchar_t *arg) {
                        optimize_mode = (OptimizeMode) _wtol(arg);

                        if (optimize_mode < OptimizeMode::AnalyzeOnly ||
                            optimize_mode >= OptimizeMode::Max) {
                            Log::log_always(L"Error: the number after the \"-a\" commandline "
                                            L"argument is invalid. "
                                            "Using the default DeprecatedAnalyzeFixupFull=3.");

                            optimize_mode = OptimizeMode::DeprecatedAnalyzeFixupFull;
                        }
                        Log::log_always(std::format(
                                L"Commandline argument '-a' accepted, optimizemode = {}",
                                (int) optimize_mode));
                    },
                    [&]() {
                        Log::log_always(L"Error: you have not specified a number after the \"-a\" "
                                        L"commandline argument.");
                    });

            // "-s slowdown" argument separated by space
            match_argument_with_space(
                    i, argc, argv, L"-s",
                    [&](const wchar_t *arg) {
                        speed = _wtol(arg);

                        if (speed < 5 || speed > 100) {
                            Log::log_always(L"Error: the number after the \"-s\" commandline "
                                            L"argument is invalid. "
                                            "Using the default 100.");
                            speed = 100;
                        }

                        Log::log_always(std::format(
                                L"Commandline argument '-s' accepted, slowing down to {}%", speed));
                    },
                    [&]() {
                        Log::log_always(L"Error: you have not specified a number after the \"-s\" "
                                        L"commandline argument.");
                    });

            // "-f freespace" argument separated by space
            match_argument_with_space(
                    i, argc, argv, L"-f",
                    [&](const wchar_t *arg) {
                        free_space = _wtof(arg);

                        if (free_space < 0 || free_space > 100) {
                            Log::log_always(L"Error: the number after the \"-f\" commandline "
                                            L"argument is invalid. "
                                            "Using the default value 1 (percent).");
                            free_space = 1;
                        }

                        Log::log_always(std::format(
                                L"Commandline argument '-f' accepted, freespace = {:.1f}%",
                                free_space));
                    },
                    [&]() {
                        Log::log_always(L"Error: you have not specified a number after the \"-f\" "
                                        L"commandline argument.");
                    });

            // "-d debuglevel" argument pair, separated by a space
            match_argument_with_space(
                    i, argc, argv, L"-d",
                    [&](const wchar_t *arg) {
                        instance_->debug_level_ = (DebugLevel) _wtol(arg);

                        if (instance_->debug_level_ < DebugLevel::AlwaysLog ||
                            instance_->debug_level_ > DebugLevel::Debug) {
                            Log::log_always(L"Error: the number after the \"-d\" commandline "
                                            L"argument is invalid. "
                                            "Using the default Warning=1.");
                            instance_->debug_level_ = DebugLevel::Warning;
                        }

                        Log::log_always(std::format(
                                L"Commandline argument '-d' accepted, debug level set to {}",
                                (int) instance_->debug_level_));
                    },
                    [&]() {
                        Log::log_always(L"Error: you have not specified a number after the \"-d\" "
                                        L"commandline argument.");
                    });


            // "-e excludemask" argument pair, separated by a space
            match_argument_with_space(
                    i, argc, argv, L"-e",
                    [&](const wchar_t *arg) {
                        excludes.emplace_back(arg);

                        Log::log_always(std::format(
                                L"Commandline argument '-e' accepted, added '{}' to the excludes",
                                arg));
                    },
                    [&]() {
                        Log::log_always(L"Error: you have not specified a mask after the \"-e\" "
                                        L"commandline argument.");
                    });
            match_argument_with_space(
                    i, argc, argv, L"-u",
                    [&](const wchar_t *arg) {
                        space_hogs.emplace_back(arg);

                        Log::log_always(std::format(
                                L"Commandline argument '-u' accepted, added '{}' to the spacehogs",
                                arg));
                    },
                    [&]() {
                        Log::log_always(L"Error: you have not specified a mask after the \"-u\" "
                                        L"commandline argument.");
                    });
            match_argument_with_space(
                    i, argc, argv, L"-q",
                    [&](const wchar_t *arg) {
                        quit_on_finish = true;

                        Log::log_always(L"Commandline argument '-q' accepted, quitonfinish = yes");
                    },
                    [&]() {
                        Log::log_always(L"Error: you have not specified a mask after the \"-q\" "
                                        L"commandline argument.");
                    });
        }
    }

    // Defragment all the paths that are specified on the commandline one by one
    bool do_all_volumes = true;

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (instance_->i_am_running_ != RunningState::RUNNING) break;

            if (wcscmp(argv[i], L"-a") == 0 || wcscmp(argv[i], L"-e") == 0 ||
                wcscmp(argv[i], L"-u") == 0 || wcscmp(argv[i], L"-s") == 0 ||
                wcscmp(argv[i], L"-f") == 0 || wcscmp(argv[i], L"-d") == 0 ||
                wcscmp(argv[i], L"-l") == 0) {
                i++;
                continue;
            }

            if (*argv[i] == '-') continue;
            if (*argv[i] == '\0') continue;

            defrag_lib->start_defrag_sync(argv[i], optimize_mode, speed, free_space, excludes,
                                          space_hogs, &instance_->running_state_);

            do_all_volumes = false;
        }
    }

    // If no paths are specified on the commandline then defrag all fixed harddisks
    if (do_all_volumes && instance_->i_am_running_ == RunningState::RUNNING) {
        defrag_lib->start_defrag_sync(nullptr, optimize_mode, speed, free_space, excludes,
                                      space_hogs, &instance_->running_state_);
    }

    // If the "-q" command line argument was specified then exit the program
    if (quit_on_finish) {
        log->log_always(L"Defrag thread ended, exiting the program");
        exit(EXIT_SUCCESS);
    }

    // End of this thread
    // return 0;
    log->log_always(L"Defrag thread ended");
}

bool DefragApp::is_already_running() const {
    // Get a process-snapshot from the kernel
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        auto err =
                std::format(L"Cannot get process snapshot: {}", Str::system_error(GetLastError()));
        gui_->show_always(std::move(err));
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
        auto s1 = std::format(L"Cannot find my own name in the process list: {}",
                              Str::from_char(my_name));
        gui_->show_always(std::move(s1));
        return true;
    }

    // Search for any other process with the same executable name as myself. If found then return true.
    Process32First(snapshot, &pe32);

    do {
        if (my_pid == pe32.th32ProcessID) continue;// Ignore myself

        if (_stricmp(pe32.szExeFile, my_name) == 0 ||
            _stricmp(pe32.szExeFile, EXECUTABLE_NAME) == 0 ||
            _stricmp(pe32.szExeFile, SCREENSAVER_NAME) == 0 ||
            _stricmp(pe32.szExeFile, CMD_EXECUTABLE_NAME) == 0) {
            CloseHandle(snapshot);

            auto s1 = std::format(L"I am already running: {}", Str::from_char(pe32.szExeFile));

            gui_->show_always(std::move(s1));
            return true;
        }
    } while (Process32Next(snapshot, &pe32));

    // Return false, not yet running
    CloseHandle(snapshot);
    return false;
}
