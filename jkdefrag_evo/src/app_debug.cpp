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

#include <app.h>

#ifdef _DEBUG

// Write a crash report to the log.
// To test the crash handler add something like this: char *p1; p1 = 0; *p1 = 0;
LONG __stdcall DefragApp::crash_report(EXCEPTION_POINTERS *exception_info) {
    IMAGEHLP_LINE64 source_line;
    DWORD line_displacement;
    STACKFRAME64 stack_frame;
    uint32_t image_type;
    char s1[BUFSIZ];

    const DefragRunner *defrag_lib = instance_->defrag_lib_;

    // Exit if we're running inside a debugger
    //  if (IsDebuggerPresent() == TRUE) return(EXCEPTION_EXECUTE_HANDLER);

    Log::log_always(L"I have crashed!");
    Log::log_always(std::format(L"  Command line: {}", GetCommandLineW()).c_str());

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

    Log::log_always(std::format(L"  Exception: {}", Str::from_char(s1)));

    // Try to show the linenumber of the sourcefile
    SymSetOptions(SymGetOptions() || SYMOPT_LOAD_LINES);
    BOOL result = SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    if (result == FALSE) {
        Log::log_always(std::format(L"  Failed to initialize SymInitialize(): {}",
                                    Str::system_error(GetLastError())));
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
                Log::log_always(
                        std::format(L"  Frame {}. At line {} in '{}'", frame_number, source_line.LineNumber,
                                    Str::from_char(source_line.FileName)));
            } else {
                Log::log_always(
                        std::format(L"  Frame {}. At line (unknown) in (unknown)", frame_number));
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
