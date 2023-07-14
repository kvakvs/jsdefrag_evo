#include "StdAfx.h"

#include "DefragStruct.h"

DefragStruct::DefragStruct()
{
	wcsncpy_s(versiontext_, L"JkDefrag 3.36", 100);
}

DefragStruct::~DefragStruct()
{
}