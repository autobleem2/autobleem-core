// lib_ableem - engine: what this process and the machine are doing right now - CPU time, threads, memory,
// the CPU temperature - for the performance overlay (Renderer::setPerfOverlay). Linux reads /proc and /sys,
// Windows its process and system APIs; anything a platform cannot tell stays at its "unknown" value.
#pragma once

#include <cstdint>
#include <string>

namespace ableem {

//******************
// ProcessSample
//******************
// One reading. The CPU figures are running totals: a load is the difference between two samples
// (ProcessStats::cpuLoad).
struct ProcessSample {
    double wallSeconds = 0;        // a monotonic clock
    double processCpuSeconds = -1; // user + system time of this process; -1 unknown
    uint64_t systemBusy = 0;       // the whole machine, in the platform's own units
    uint64_t systemTotal = 0;      // 0 unknown
    int threads = -1;              // this process's threads; -1 unknown
    int64_t rssBytes = -1;         // resident memory; -1 unknown
    int temperatureMilliC = -1;    // the first thermal zone; -1 unknown (always on Windows)
};

//******************
// ProcessStats
//******************
struct ProcessStats {
    static ProcessSample sample();

    // the load between two samples in percent of the whole machine (all cores): this process's and the
    // system's. -1 when either sample cannot tell, or no time has passed.
    struct Load {
        double process = -1;
        double system = -1;
    };
    static Load cpuLoad(const ProcessSample &before, const ProcessSample &after, int cores);

    // the parsers, fed the files' text (tested on their own):
    // /proc/self/stat - utime + stime in clock ticks, and num_threads; the command name in brackets may hold
    // spaces and brackets, so the fields are counted from the last ')'
    static bool parseSelfStat(const std::string &text, uint64_t &cpuTicks, int &threads);
    // /proc/stat's first line ("cpu  user nice system idle iowait irq softirq steal ...") - busy is all but
    // idle and iowait
    static bool parseProcStat(const std::string &text, uint64_t &busy, uint64_t &total);
    // /proc/self/statm - the resident pages (the second number)
    static bool parseStatm(const std::string &text, int64_t &residentPages);
};

} // namespace ableem
