#pragma once

class DefragLog
{
public:
	DefragLog();
	void log_message(const wchar_t* format, ...) const;
	void set_log_filename(const wchar_t* file_name);

	wchar_t my_name_[MAX_PATH];
	wchar_t my_short_name_[MAX_PATH];

	wchar_t *get_log_filename();

protected:
private:
	wchar_t log_file_[MAX_PATH];

	DefragLib *defrag_lib_;
};
