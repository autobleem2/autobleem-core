//
// ProcessStats: the /proc parsers behind the performance overlay, and the load between two samples.
//
#include "doctest/doctest.h"

#include <ableem/engine/process_stats.h>

#include <string>

using ableem::ProcessSample;
using ableem::ProcessStats;
using std::string;

TEST_CASE("parseSelfStat counts the fields from the last bracket") {
    // a command name with a space and a bracket in it; utime 250, stime 50, num_threads 7
    const string stat = "1234 (auto bleem) gui) S 1 1234 1234 0 -1 4194560 100 0 0 0 250 50 0 0 20 0 7 0 "
                        "500 100000000 3000 18446744073709551615";
    uint64_t ticks = 0;
    int threads = 0;
    REQUIRE(ProcessStats::parseSelfStat(stat, ticks, threads));
    CHECK(ticks == 300);
    CHECK(threads == 7);
}

TEST_CASE("parseSelfStat refuses a short or odd line") {
    uint64_t ticks = 0;
    int threads = 0;
    CHECK_FALSE(ProcessStats::parseSelfStat("", ticks, threads));
    CHECK_FALSE(ProcessStats::parseSelfStat("1 (x) S 1 2 3", ticks, threads));
}

TEST_CASE("parseProcStat leaves idle and iowait out of busy") {
    uint64_t busy = 0, total = 0;
    REQUIRE(ProcessStats::parseProcStat("cpu  100 10 50 800 40 0 0 0 0 0\ncpu0 1 2 3 4\n", busy, total));
    CHECK(total == 1000);
    CHECK(busy == 160);
    CHECK_FALSE(ProcessStats::parseProcStat("intr 1 2 3", busy, total));
}

TEST_CASE("parseStatm takes the resident pages") {
    int64_t pages = 0;
    REQUIRE(ProcessStats::parseStatm("51200 3000 800 10 0 2000 0\n", pages));
    CHECK(pages == 3000);
}

TEST_CASE("cpuLoad is a share of the whole machine") {
    ProcessSample a, b;
    a.wallSeconds = 10;
    b.wallSeconds = 12;
    a.processCpuSeconds = 1;
    b.processCpuSeconds = 3; // one core busy for the 2 s
    a.systemBusy = 100;
    a.systemTotal = 1000;
    b.systemBusy = 400;
    b.systemTotal = 1800;
    const ProcessStats::Load load = ProcessStats::cpuLoad(a, b, 4);
    CHECK(load.process == doctest::Approx(25.0));
    CHECK(load.system == doctest::Approx(37.5));
}

TEST_CASE("cpuLoad says unknown when a sample cannot tell") {
    ProcessSample a, b;
    b.wallSeconds = 1;
    const ProcessStats::Load load = ProcessStats::cpuLoad(a, b, 4);
    CHECK(load.process < 0);
    CHECK(load.system < 0);
}

TEST_CASE("sample reads this process") {
    const ProcessSample s = ProcessStats::sample();
    CHECK(s.wallSeconds > 0);
#if defined(__linux__) || defined(_WIN32)
    CHECK(s.processCpuSeconds >= 0);
    CHECK(s.threads >= 1);
    CHECK(s.rssBytes > 0);
    CHECK(s.systemTotal > 0);
#endif
}
