#pragma once

//#define _WIN32_WINNT 0x0500

#include <Windows.h>

#include <stdio.h>
#include <wchar.h>
#include <time.h>
#include <sys/timeb.h>
#include <gdiplus.h>

using namespace Gdiplus;

#include <TlHelp32.h>                  // CreateToolhelp32Snapshot()
#include <string>
#include <memory>
#include <optional>
#include <cwctype>
#include <chrono>

#ifdef _DEBUG

#   include <DbgHelp.h>                   // SetUnhandledExceptionFilter()

#endif

#include "colormap.h"
#include "constants.h"
#include "defrag_gui.h"
#include "defrag_lib.h"
#include "defrag_log.h"
#include "defrag_state.h"
#include "defrag_struct.h"
#include "itemstruct.h"
#include "mem_util.h"
#include "printing.h"
#include "scan_fat.h"
#include "scan_ntfs.h"
#include "time_util.h"
#include "tree.h"
#include "types.h"
