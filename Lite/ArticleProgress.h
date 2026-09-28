#pragma once
#include <cstddef>
#include <climits>

// Screen bytes only, never attributes or adjacent rows. No NUL is required.
namespace ArticleProgress
{
    enum State { More = 0, Complete = 1, Unavailable = 2 };
    struct Progress
    {
        State state = Unavailable;
        int first = 0;
        int last = 0;
    };
    inline bool Digit(char c) { return c >= '0' && c <= '9'; }
    inline bool Number(const char* s, std::size_t begin, std::size_t end, int& value)
    {
        if (begin == end) return false;
        value = 0;
        for (std::size_t i = begin; i < end; ++i)
        {
            if (!Digit(s[i]) || value > (INT_MAX - (s[i] - '0')) / 10) return false;
            value = value * 10 + s[i] - '0';
        }
        return true;
    }
    inline Progress Parse(const char* s, std::size_t width)
    {
        Progress result;
        if (!s) return result;
        std::size_t size = 0;
        while (size < width && s[size]) ++size;
        int percent = -1;
        std::size_t afterPercent = 0;
        for (std::size_t i = 0; i < size; ++i)
        {
            if (s[i] != '%') continue;
            std::size_t begin = i;
            while (begin && Digit(s[begin - 1])) --begin;
            if (begin == i) continue; // e.g. the right-hand (X/%) shortcut
            int value = 0;
            if (percent != -1 || !Number(s, begin, i, value) || value > 100)
                return result;
            std::size_t open = begin;
            while (open && s[open - 1] == ' ') --open;
            if (!open || s[open - 1] != '(' || i + 1 >= size || s[i + 1] != ')')
                return result;
            percent = value;
            afterPercent = i + 2;
            break; // Right-hand hints are not part of progress information.
        }
        if (percent < 0) return result;
        for (std::size_t i = afterPercent; i < size; ++i)
        {
            if (s[i] == '%') return Progress();
            if (s[i] != '~') continue;
            std::size_t begin = i, end = i + 1;
            while (begin > afterPercent && s[begin - 1] == ' ') --begin;
            const std::size_t leftEnd = begin;
            while (begin > afterPercent && Digit(s[begin - 1])) --begin;
            while (end < size && s[end] == ' ') ++end;
            const std::size_t rightBegin = end;
            while (end < size && Digit(s[end])) ++end;
            if (!begin || s[begin - 1] != ' ' || end >= size || s[end] != ' ' ||
                !Number(s, begin, leftEnd, result.first) ||
                !Number(s, rightBegin, end, result.last) ||
                result.first < 1 || result.last < result.first)
                return Progress();
            // Require the complete line unit, not an unfinished numeric prefix.
            while (end < size && s[end] == ' ') ++end;
            const bool big5Line = size - end >= 2 &&
                static_cast<unsigned char>(s[end]) == 0xa6 &&
                static_cast<unsigned char>(s[end + 1]) == 0xe6;
            const bool utf8Line = size - end >= 3 &&
                static_cast<unsigned char>(s[end]) == 0xe8 &&
                static_cast<unsigned char>(s[end + 1]) == 0xa1 &&
                static_cast<unsigned char>(s[end + 2]) == 0x8c;
            if (!big5Line && !utf8Line) return Progress();
            result.state = percent == 100 ? Complete : More;
            return result; // Ignore changing right-hand hints (and their bytes).
        }
        return result;
    }

    // One Down normally reveals one line; some servers reveal two. Do not
    // accept jumps/backtracking or copy the same footer twice after split input.
    inline int NewLines(const Progress& p, int previous, int visibleRows)
    {
        if (p.state == Unavailable || previous < 1 || visibleRows < 1 ||
            p.last - p.first >= visibleRows || p.last <= previous) return 0;
        const int delta = p.last - previous;
        return delta <= 2 && delta <= visibleRows ? delta : 0;
    }
}
