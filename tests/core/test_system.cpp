//
// System: the OS helpers that can be exercised without a console - the disk space and the shell.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/temp_dir.h"

#include "core/services/system.h"

#include <cstdint>
#include <string>

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
