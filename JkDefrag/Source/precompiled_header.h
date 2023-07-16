#pragma once

#define _WIN32_WINNT 0x0500

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <time.h>
#include <sys/timeb.h>
#include <gdiplus.h>

using namespace Gdiplus;

#include <tlhelp32.h>                  /* CreateToolhelp32Snapshot() */
#include <string>
#include <memory>
#include <optional>
#include <cwctype>
#include <chrono>

#ifdef _DEBUG

#   include <dbghelp.h>                   /* SetUnhandledExceptionFilter() */

#endif

#include "tech/defrag_struct.h"
#include "tech/itemstruct.h"
#include "tech/defrag_data_struct.h"

#include "printing.h"
#include "tech/defrag_lib.h"
#include "defrag_log.h"
#include "tech/scan_fat.h"
#include "tech/scan_ntfs.h"
#include "gui/defrag_gui_colormap.h"
#include "gui/defrag_gui.h"
