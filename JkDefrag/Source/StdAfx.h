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

#include "DefragStruct.h"
#include "DefragLib.h"
#include "DefragLog.h"
#include "ScanFat.h"
#include "ScanNtfs.h"
#include "DefragGui.h"
