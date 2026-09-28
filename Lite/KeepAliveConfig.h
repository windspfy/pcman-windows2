#pragma once
#include "ConfigFile.h"
#include "KeepAlive.h"

// Strict parsing for externally editable INI values. Do not let atoi overflow
// turn a malformed setting into an apparently valid interval/mode.
class KeepAliveIntervalSetting : public CConfigFile::ConfigHandler
{
public:
	explicit KeepAliveIntervalSetting(DWORD& value) : value_(value) {}
	void Load(char* text) override
	{
		CString trimmed(text);
		trimmed.Trim();
		unsigned long seconds = KeepAlive::DefaultInterval;
		KeepAlive::ParseInterval(trimmed, seconds);
		value_ = seconds;
	}
	void Save(CString& text) override { text.Format("%lu", KeepAlive::NormalizeInterval(value_)); }
private:
	DWORD& value_;
};

class KeepAliveModeSetting : public CConfigFile::ConfigHandler
{
public:
	explicit KeepAliveModeSetting(int& value) : value_(value) {}
	void Load(char* text) override
	{
		CString trimmed(text);
		trimmed.Trim();
		value_ = trimmed == "1" ? KeepAlive::TimingMark :
			trimmed == "2" ? KeepAlive::Custom : KeepAlive::Auto;
	}
	void Save(CString& text) override { text.Format("%d", KeepAlive::NormalizeMode(value_)); }
private:
	int& value_;
};
