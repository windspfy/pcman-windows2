#pragma once

#include <string>
#include "RFC854.h"

namespace KeepAlive
{
	// Persisted values: keep stable. Missing keys default to Auto so old
	// configurations retain their custom bytes on non-PTT hosts.
	enum Mode { Auto = 0, TimingMark = 1, Custom = 2 };
	const unsigned int DefaultInterval = 180;
	const unsigned int MinInterval = 30;
	const unsigned int MaxInterval = 86400;
	const unsigned char Request[] = { IAC, DO, TIMING_MARK };

	inline int NormalizeMode(int mode)
	{
		return mode >= Auto && mode <= Custom ? mode : Auto;
	}

	inline bool ValidInterval(unsigned long seconds)
	{
		return seconds >= MinInterval && seconds <= MaxInterval;
	}

	inline unsigned long NormalizeInterval(unsigned long seconds)
	{
		return ValidInterval(seconds) ? seconds : DefaultInterval;
	}

	inline bool ParseInterval(const char* text, unsigned long& seconds)
	{
		if (!text || !*text) return false;
		unsigned long value = 0;
		for (; *text; ++text)
		{
			if (*text < '0' || *text > '9') return false;
			value = value * 10 + (*text - '0');
			if (value > MaxInterval) return false;
		}
		if (!ValidInterval(value)) return false;
		seconds = value;
		return true;
	}

	// Receives the parsed host, not a URL: exact DNS suffix boundaries only.
	inline bool IsPttHost(std::string host)
	{
		for (char& c : host)
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if (!host.empty() && host.back() == '.') host.pop_back();
		for (const char* domain : { "ptt.cc", "ptt2.cc" })
		{
			const std::string suffix = std::string(".") + domain;
			if (host == domain || (host.size() > suffix.size() &&
				host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0))
				return true;
		}
		return false;
	}

	inline bool UseTimingMark(int mode, const char* host)
	{
		mode = NormalizeMode(mode);
		return mode == TimingMark || (mode == Auto && IsPttHost(host ? host : ""));
	}

	// Called once per second while connected. Send() resets idleSeconds on
	// ordinary outgoing activity. No modulo/division and no catch-up bursts.
	inline bool Tick(bool enabled, unsigned long interval, unsigned long& idleSeconds)
	{
		if (!enabled) { idleSeconds = 0; return false; }
		interval = NormalizeInterval(interval);
		if (idleSeconds < interval) ++idleSeconds;
		if (idleSeconds < interval) return false;
		idleSeconds = 0;
		return true;
	}

	inline bool IsTimingMarkReply(unsigned char command, unsigned char option)
	{
		return option == TIMING_MARK && (command == WILL || command == WONT);
	}
}
