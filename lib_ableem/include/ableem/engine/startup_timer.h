// lib_ableem - engine: how long each startup step takes, one grep-able log line per step:
//
//   startup: <step> <ms> ms
//
// The clock is std::chrono::steady_clock: the Pi has no RTC, its wall clock jumps when NTP syncs in the first
// seconds of boot, and a duration taken from it would be nonsense (or negative). StartupTimer::milestone() logs
// the time since the first use of this header in the process (main() touches it first thing).
#pragma once

#include <chrono>
#include <string>
#include <utility>

#include "log.h"

namespace ableem {

//******************
// StartupTimer
//******************
// RAII: logs the step's duration when it goes out of scope (or at stop(), once).
class StartupTimer {
public:
    using Clock = std::chrono::steady_clock;

    explicit StartupTimer(std::string step) : step_(std::move(step)), begin_(Clock::now()) {}
    StartupTimer(const StartupTimer &) = delete;
    StartupTimer &operator=(const StartupTimer &) = delete;
    ~StartupTimer() { stop(); }

    // logs now instead of at the end of the scope; returns the milliseconds
    long long stop() {
        if (stopped_)
            return ms_;
        stopped_ = true;
        ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin_).count();
        PLOG_INFO << line(step_, ms_);
        return ms_;
    }

    // "startup: <step> <ms> ms"
    static std::string line(const std::string &step, long long ms) {
        return "startup: " + step + " " + std::to_string(ms) + " ms";
    }

    // the process-wide origin, set the first time it is asked for
    static Clock::time_point origin() {
        static const Clock::time_point t0 = Clock::now();
        return t0;
    }
    // logs "startup: <name> <ms since origin> ms" - a point in time rather than a duration (menu-visible ...)
    static long long milestone(const std::string &name) {
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - origin()).count();
        PLOG_INFO << line("at " + name, ms);
        return ms;
    }

private:
    std::string step_;
    Clock::time_point begin_;
    bool stopped_ = false;
    long long ms_ = 0;
};

} // namespace ableem
