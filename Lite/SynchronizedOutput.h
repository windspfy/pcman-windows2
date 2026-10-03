#pragma once
#include <cstdint>
#include <vector>

// Per-connection presentation barrier. The terminal model keeps consuming
// input; only the previously visible rows (including attributes) are retained.
class SynchronizedOutput
{
public:
    static const unsigned int TimeoutMs = 2000;
    bool Active() const { return active; }
    bool CopyReady() const { return !active && !incomplete; }
    bool Begin(std::uint64_t now, char* const* rows, int count, int stride)
    {
        if (active) return false; // Mode set is idempotent, not a nesting stack.
        std::vector<std::vector<char>> next;
        for (int i = 0; i < count; ++i)
            next.emplace_back(rows[i], rows[i] + stride);
        snapshot.swap(next);
        started = now;
        active = true;
        return true;
    }
    bool End()
    {
        const bool changed = active || incomplete;
        Reset();
        return changed;
    }
    bool Expire(std::uint64_t now)
    {
        if (!active || now - started < TimeoutMs) return false;
        Abort();
        return true;
    }
    void Abort()
    {
        active = false;
        incomplete = true;
        snapshot.clear();
    }
    void Reset()
    {
        active = incomplete = false;
        started = 0;
        snapshot.clear();
    }
    char* Row(int index) { return snapshot.at(index).data(); }
private:
    bool active = false;
    bool incomplete = false;
    std::uint64_t started = 0;
    std::vector<std::vector<char>> snapshot;
};
