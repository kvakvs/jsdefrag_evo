#pragma once

#define _WIN32_WINNT 0x0500

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

#include "tree.h"
#include "defrag_struct.h"
#include "itemstruct.h"
#include "defrag_data_struct.h"
#include "printing.h"
#include "defrag_lib.h"
#include "defrag_log.h"
#include "scan_fat.h"
#include "scan_ntfs.h"
#include "colormap.h"
#include "defrag_gui.h"
