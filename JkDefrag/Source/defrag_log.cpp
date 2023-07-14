#include "std_afx.h"

DefragLog::DefragLog()
{
	WCHAR *p1;

	defrag_lib_ = DefragLib::get_instance();

	GetModuleFileNameW(nullptr,my_name_,MAX_PATH);
	GetShortPathNameW(my_name_,my_short_name_,MAX_PATH);
	GetLongPathNameW(my_short_name_,my_name_,MAX_PATH);

	/* Determine default path to logfile. */
	swprintf_s(log_file_,MAX_PATH,L"%s",my_name_);

	p1 = defrag_lib_->stristr_w(log_file_,L".exe");

	if (p1 == nullptr) p1 = defrag_lib_->stristr_w(log_file_,L".scr");

	if (p1 != nullptr)
	{
		*p1 = '\0';

		wcscat_s(log_file_,MAX_PATH,L".log");
		_wunlink(log_file_);

	}
	else
	{
		*log_file_ = '\0';
	}
}

void DefragLog::set_log_filename(WCHAR *file_name)
{
	/* Determine default path to logfile. */
	swprintf_s(log_file_,MAX_PATH,L"%s",file_name);
	_wunlink(log_file_);
}

WCHAR *DefragLog::get_log_filename()
{
	return log_file_;
}

/* Write a text to the logfile. The parameters are the same as for the "printf"
functions, a Format string and a series of parameters. */
void DefragLog::log_message(WCHAR *format, ...)
{
	va_list VarArgs;
	FILE *Fout;
	int Result;
	time_t Now;
    tm NowTm;

	/* If there is no message then return. */
	if (format == nullptr) return;

	/* If there is no logfile then return. */
	if (*log_file_ == '\0') return;

	/* Open the logfile. */
	Result = _wfopen_s(&Fout,log_file_,L"a, ccs=UTF-8");
	if (Result != 0 || Fout == nullptr) return;

	/* Write the string to the logfile. */
	time(&Now);
	Result = localtime_s(&NowTm,&Now);
	fwprintf_s(Fout,L"%02lu:%02lu:%02lu ",NowTm.tm_hour,NowTm.tm_min,NowTm.tm_sec);
	va_start(VarArgs,format);
	vfwprintf_s(Fout,format,VarArgs);
	va_end(VarArgs);
	fwprintf_s(Fout,L"\n");

	/* Close the logfile. */
	fflush(Fout);
	fclose(Fout);
}

