//
// AB_TRACE_ABOUT=1: the ms of each stage of opening the About screen and of the surprise game's first frames, to the
// log (off by default; one getenv, read once)
//
#pragma once

#include <chrono>
#include <cstdlib>
#include <cstring>

// PLOG_INFO comes with ../gui.h, which the including .cpp has already

namespace about_trace {

inline bool on() {
    static const bool enabled = [] {
        const char *v = getenv("AB_TRACE_ABOUT");
        return v && *v && strcmp(v, "0") != 0;
    }();
    return enabled;
}

// logs the time since its construction, under `name`, when it goes out of scope
class Stage {
public:
    explicit Stage(const char *stageName) : name(stageName), begin(std::chrono::steady_clock::now()) {}
    ~Stage() {
        if (on()) {
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            PLOG_INFO << "AB_TRACE_ABOUT " << name << ": " << ms << " ms";
        }
    }
    Stage(const Stage &) = delete;
    Stage &operator=(const Stage &) = delete;

private:
    const char *name;
    std::chrono::steady_clock::time_point begin;
};

} // namespace about_trace
