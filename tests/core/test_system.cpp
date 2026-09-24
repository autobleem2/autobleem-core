//
// System: the OS helpers that can be exercised without a console - the disk space and the shell.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/temp_dir.h"

#include "core/services/system.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using std::string;

TEST_CASE("defaultRouteIn finds a default route that is up, on anything but the loopback") {
    const string header = "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n";
    // a console on WiFi: the default route via the router, and the LAN
    CHECK(System::defaultRouteIn(header + "wlan0\t00000000\t0101A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n"
                                          "wlan0\t0001A8C0\t00000000\t0001\t0\t0\t600\t00FFFFFF\t0\t0\t0\n"));
    // the AutoBleem kernel's USB network alone: a link to the PC, no way out
    CHECK_FALSE(System::defaultRouteIn(header + "rndis0\t0002A8C0\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n"));
    // a default route that is down, and one on the loopback
    CHECK_FALSE(System::defaultRouteIn(header + "eth0\t00000000\t0101A8C0\t0002\t0\t0\t0\t00000000\t0\t0\t0\n"));
    CHECK_FALSE(System::defaultRouteIn(header + "lo\t00000000\t00000000\t0001\t0\t0\t0\t00000000\t0\t0\t0\n"));
    // a stock console: nothing but the header
    CHECK_FALSE(System::defaultRouteIn(header));
    CHECK_FALSE(System::defaultRouteIn(""));
}

TEST_CASE("diskSpace reports the filesystem a path is on, and nothing for a path that is not there") {
    TempDir tmp("disk_space");
    uint64_t freeBytes = 0, totalBytes = 0;
    REQUIRE(System::diskSpace(tmp.path(), freeBytes, totalBytes));
    CHECK(totalBytes > 0);
    CHECK(freeBytes <= totalBytes);

    CHECK_FALSE(System::diskSpace(tmp.at("no/such/dir"), freeBytes, totalBytes));
}

TEST_CASE("getAvailableSpace is the USB root's filesystem as 'N GB / M GB (P%)'") {
    TempDir tmp("available_space");
    EnvFixture env;
    ableem::Environment::setUsbRoot(tmp.path());
    const string text = System::getAvailableSpace();
    CHECK(text.find(" GB / ") != string::npos);
    CHECK(text.find("%)") != string::npos);

    ableem::Environment::setUsbRoot(tmp.at("gone"));
    CHECK(System::getAvailableSpace().empty());
}

TEST_CASE("runShellCommand runs a line through the shell and returns its exit status") {
    TempDir tmp("shell");
#ifdef _WIN32
    const string touch = "echo hi > \"" + tmp.at("made.txt") + "\"";
#else
    const string touch = "echo hi > '" + tmp.at("made.txt") + "'";
#endif
    CHECK(System::runShellCommand(touch) == 0);
    CHECK(ableem::DirEntry::exists(tmp.at("made.txt")));
    CHECK(System::runShellCommand("exit 3") == 3);
}

TEST_CASE("runShellCommand with a cancel: runs to the end when not asked, stops the command when asked") {
    TempDir tmp("shell");
#ifdef _WIN32
    const string touch = "echo hi > \"" + tmp.at("made.txt") + "\"";
    const string slow = "ping -n 30 127.0.0.1 > nul";
#else
    const string touch = "echo hi > '" + tmp.at("made.txt") + "'";
    const string slow = "sleep 30";
#endif
    CHECK(System::runShellCommand(touch, [] { return false; }) == 0);
    CHECK(ableem::DirEntry::exists(tmp.at("made.txt")));
    CHECK(System::runShellCommand("exit 4", [] { return false; }) == 4);

    auto start = std::chrono::steady_clock::now();
    int asked = 0;
    CHECK(System::runShellCommand(slow, [&asked] { return ++asked > 3; }) == -2);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));
}

TEST_CASE("runStreaming hands each line over as it comes, stdout and stderr apart, and returns the exit code") {
    TempDir tmp("streaming");
    tmp.writeFile("ps1.txt", "#Starting - helper\n!err a warning\n10\r\n!sleep 50\n100\n#DONE\n!exit 3\n");
    std::vector<std::pair<string, bool>> lines;
    int code = System::runStreaming(
        AB_PROC_HELPER, {"--start", "--ps1", tmp.path()}, tmp.path(),
        {{"AB_PROCESSOR_PROTOCOL", "1"}, {"AB_TMP", tmp.path()}},
        [&lines](const string &line, bool fromStderr) { lines.push_back({line, fromStderr}); }, [] { return false; });
    CHECK(code == 3);
    std::vector<string> out;
    string err;
    for (const auto &l : lines) {
        if (l.second)
            err += l.first;
        else
            out.push_back(l.first);
    }
    CHECK(out == std::vector<string>{"#Starting - helper", "10", "100", "#DONE"});
    CHECK(err == "a warning");
    // the arguments and the environment reached it
    CHECK(tmp.readFile("calls.txt").find("--start|--ps1|") == 0);
    CHECK(tmp.readFile("calls.txt").find("protocol=1|tmp=yes") != string::npos);
}

TEST_CASE("runStreaming stops a child when asked, and says -1 for a program that is not there") {
    TempDir tmp("streaming_stop");
    tmp.writeFile("ps1.txt", "#Starting - helper\n#Working\n!sleep 30000\n#DONE\n");
    auto start = std::chrono::steady_clock::now();
    bool sawStage = false;
    int code = System::runStreaming(
        AB_PROC_HELPER, {"--start", "--ps1", tmp.path()}, tmp.path(), {},
        [&sawStage](const string &line, bool) { sawStage = sawStage || line == "#Working"; },
        [&sawStage] { return sawStage; });
    CHECK(code == -2);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));

    CHECK(System::runStreaming(tmp.at("no-such-program"), {}, "", {}, nullptr, nullptr) == -1);
}
