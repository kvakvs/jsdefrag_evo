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

#ifdef _DEBUG

#   include <dbghelp.h>                   /* SetUnhandledExceptionFilter() */

#endif

#include "defrag_struct.h"
#include "itemstruct.h"
#include "defrag_data_struct.h"

#include "printing.h"
#include "defrag_lib.h"
#include "defrag_log.h"
#include "scan_fat.h"
#include "scan_ntfs.h"
#include "defrag_gui_colormap.h"
#include "defrag_gui.h"
