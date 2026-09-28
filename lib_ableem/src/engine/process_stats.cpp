#include "ableem/engine/process_stats.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2 // K32GetProcessMemoryInfo, in kernel32 - no psapi.lib
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

namespace ableem {

namespace {
#ifndef _WIN32
std::string readSmallFile(const char *path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return "";
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
#endif

double monotonicSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

//*******************************
// ProcessStats parsers
//*******************************
bool ProcessStats::parseSelfStat(const std::string &text, uint64_t &cpuTicks, int &threads) {
    const size_t close = text.rfind(')');
    if (close == std::string::npos)
        return false;
    std::istringstream in(text.substr(close + 1));
    std::vector<std::string> fields; // fields[0] is the state, field 3 of the file
    std::string f;
    while (in >> f)
        fields.push_back(f);
    // utime and stime are fields 14 and 15, num_threads field 20
    if (fields.size() < 18)
        return false;
    try {
        cpuTicks = std::stoull(fields[11]) + std::stoull(fields[12]);
        threads = std::stoi(fields[17]);
    } catch (...) {
        return false;
    }
    return true;
}

bool ProcessStats::parseProcStat(const std::string &text, uint64_t &busy, uint64_t &total) {
    std::istringstream in(text);
    std::string label;
    if (!(in >> label) || label != "cpu")
        return false;
    uint64_t v = 0, sum = 0, idle = 0;
    int i = 0;
    while (i < 8 && in >> v) { // user nice system idle iowait irq softirq steal (guest is in user already)
        sum += v;
        if (i == 3 || i == 4)
            idle += v;
        i++;
    }
    if (i < 4)
        return false;
    total = sum;
    busy = sum - idle;
    return true;
}

bool ProcessStats::parseStatm(const std::string &text, int64_t &residentPages) {
    std::istringstream in(text);
    int64_t size = 0;
    if (!(in >> size >> residentPages))
        return false;
    return true;
}

//*******************************
// ProcessStats::sample
//*******************************
ProcessSample ProcessStats::sample() {
    ProcessSample s;
    s.wallSeconds = monotonicSeconds();
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        auto ticks = [](const FILETIME &t) {
            return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
        };
        s.processCpuSeconds = static_cast<double>(ticks(kernel) + ticks(user)) / 1e7; // 100 ns units
    }
    FILETIME idle, sysKernel, sysUser;
    if (GetSystemTimes(&idle, &sysKernel, &sysUser)) {
        auto ticks = [](const FILETIME &t) {
            return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
        };
        // kernel time includes the idle time
        s.systemTotal = ticks(sysKernel) + ticks(sysUser);
        s.systemBusy = s.systemTotal - ticks(idle);
    }
    PROCESS_MEMORY_COUNTERS mem;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &mem, sizeof(mem)))
        s.rssBytes = static_cast<int64_t>(mem.WorkingSetSize);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        const DWORD pid = GetCurrentProcessId();
        int count = 0;
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
            if (te.th32OwnerProcessID == pid)
                count++;
        CloseHandle(snap);
        s.threads = count;
    }
#else
    uint64_t ticks = 0;
    int threads = -1;
    if (parseSelfStat(readSmallFile("/proc/self/stat"), ticks, threads)) {
        const long hz = sysconf(_SC_CLK_TCK);
        if (hz > 0)
            s.processCpuSeconds = static_cast<double>(ticks) / hz;
        s.threads = threads;
    }
    uint64_t busy = 0, total = 0;
    if (parseProcStat(readSmallFile("/proc/stat"), busy, total)) {
        s.systemBusy = busy;
        s.systemTotal = total;
    }
    int64_t pages = 0;
    if (parseStatm(readSmallFile("/proc/self/statm"), pages))
        s.rssBytes = pages * sysconf(_SC_PAGESIZE);
    const std::string temp = readSmallFile("/sys/class/thermal/thermal_zone0/temp");
    if (!temp.empty()) {
        try {
            s.temperatureMilliC = std::stoi(temp);
        } catch (...) {
        }
    }
#endif
    return s;
}

//*******************************
// ProcessStats::cpuLoad
//*******************************
ProcessStats::Load ProcessStats::cpuLoad(const ProcessSample &before, const ProcessSample &after, int cores) {
    Load load;
    const double wall = after.wallSeconds - before.wallSeconds;
    if (wall > 0 && cores > 0 && before.processCpuSeconds >= 0 && after.processCpuSeconds >= 0)
        load.process = 100.0 * (after.processCpuSeconds - before.processCpuSeconds) / (wall * cores);
    if (after.systemTotal > before.systemTotal && after.systemBusy >= before.systemBusy)
        load.system = 100.0 * static_cast<double>(after.systemBusy - before.systemBusy) /
                      static_cast<double>(after.systemTotal - before.systemTotal);
    return load;
}

} // namespace ableem
