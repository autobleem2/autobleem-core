//
// PadBatteryService over a fake sysfs tree, and the pure parsers directly.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/temp_dir.h"

#include "core/services/pad_battery.h"

#include <string>

using std::string;

TEST_CASE("isPadBatteryEntry recognises both drivers' folder names, and nothing else") {
    string address;
    CHECK(PadBatteryService::isPadBatteryEntry("sony_controller_battery_aa:bb:cc:dd:ee:ff", address));
    CHECK(address == "aa:bb:cc:dd:ee:ff");

    CHECK(PadBatteryService::isPadBatteryEntry("ps-controller-battery-11:22:33:44:55:66", address));
    CHECK(address == "11:22:33:44:55:66");

    CHECK_FALSE(PadBatteryService::isPadBatteryEntry("BAT0", address));
    CHECK_FALSE(PadBatteryService::isPadBatteryEntry("sony_controller_battery_", address)); // no mac after it
    CHECK_FALSE(PadBatteryService::isPadBatteryEntry("", address));
}

TEST_CASE("percentFromCapacityLevel maps the known words, else -1") {
    CHECK(PadBatteryService::percentFromCapacityLevel("Full") == 100);
    CHECK(PadBatteryService::percentFromCapacityLevel("High") == 75);
    CHECK(PadBatteryService::percentFromCapacityLevel("Normal") == 50);
    CHECK(PadBatteryService::percentFromCapacityLevel("Low") == 15);
    CHECK(PadBatteryService::percentFromCapacityLevel("Critical") == 5);
    CHECK(PadBatteryService::percentFromCapacityLevel("Unknown") == -1);
    CHECK(PadBatteryService::percentFromCapacityLevel("") == -1);
}

// The real folder names have colon-separated MACs ("...battery_e4:17:d8:aa:bb:cc") - NTFS refuses a colon
// in a name, so the on-disk fixtures below use a MAC with no separators instead; isPadBatteryEntry's own
// test above covers the colon form directly, and list() does not care what shape the suffix is.
TEST_CASE("list() reads capacity and status from a fake power_supply tree, ignores unrelated entries") {
    TempDir tmp("pad_battery");
    tmp.makeSubDir("sony_controller_battery_e417d8aabbcc");
    tmp.writeFile("sony_controller_battery_e417d8aabbcc/capacity", "72\n");
    tmp.writeFile("sony_controller_battery_e417d8aabbcc/status", "Discharging\n");

    tmp.makeSubDir("ps-controller-battery-001bdc0f1122");
    tmp.writeFile("ps-controller-battery-001bdc0f1122/capacity", "5\n");
    tmp.writeFile("ps-controller-battery-001bdc0f1122/status", "Charging\n");

    tmp.makeSubDir("BAT0"); // the machine's own battery, if it has one - never a pad
    tmp.writeFile("BAT0/capacity", "88\n");

    PadBatteryService service(tmp.path());
    auto pads = service.list();
    REQUIRE(pads.size() == 2);
    // sorted by address
    CHECK(pads[0].address == "001bdc0f1122");
    CHECK(pads[0].percent == 5);
    CHECK(pads[0].status == "Charging");
    CHECK(pads[0].known());

    CHECK(pads[1].address == "e417d8aabbcc");
    CHECK(pads[1].percent == 72);
    CHECK(pads[1].status == "Discharging");
}

TEST_CASE("list() falls back to capacity_level when capacity is missing") {
    TempDir tmp("pad_battery_level");
    tmp.makeSubDir("sony_controller_battery_aaaaaaaaaaaa");
    tmp.writeFile("sony_controller_battery_aaaaaaaaaaaa/capacity_level", "Low\n");

    PadBatteryService service(tmp.path());
    auto pads = service.list();
    REQUIRE(pads.size() == 1);
    CHECK(pads[0].percent == 15);
}

TEST_CASE("list() answers empty for a missing or empty root") {
    CHECK(PadBatteryService("").list().empty());
    TempDir tmp("pad_battery_empty");
    CHECK(PadBatteryService(tmp.path()).list().empty());
    CHECK(PadBatteryService(tmp.path() + "/does-not-exist").list().empty());
}

TEST_CASE("Env::padBatteryPowerSupplyDir is settable and restored by EnvFixture") {
    EnvFixture env;
    env.setPadBatteryPowerSupplyDir("/tmp/fake-power-supply");
    CHECK(Env::padBatteryPowerSupplyDir() == "/tmp/fake-power-supply");
}
