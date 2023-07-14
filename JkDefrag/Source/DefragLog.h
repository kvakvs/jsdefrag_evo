#pragma once

class DefragLog
{
public:
	DefragLog();
	void log_message(WCHAR *format, ...);
	void set_log_filename(WCHAR *file_name);

	WCHAR my_name_[MAX_PATH];
	WCHAR my_short_name_[MAX_PATH];

	WCHAR *get_log_filename();

protected:
private:
	WCHAR log_file_[MAX_PATH];

	DefragLib *defrag_lib_;
};
