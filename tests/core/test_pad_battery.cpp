//
// PadBatteryService over a fake sysfs tree, and the pure parsers directly.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/temp_dir.h"

#include "core/services/pad_battery.h"

#include <algorithm>
#include <string>
#include <vector>

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

TEST_CASE("the fake-battery hook's spec: percents, commas, clamping, junk skipped, at most four pads") {
    using V = std::vector<int>;
    CHECK(PadBatteryService::parseFakeSpec("60") == V{60});
    CHECK(PadBatteryService::parseFakeSpec("80,12") == V{80, 12});
    CHECK(PadBatteryService::parseFakeSpec(" 80 , 12 ") == V{80, 12});
    CHECK(PadBatteryService::parseFakeSpec("150,-5") == V{100, 0});
    CHECK(PadBatteryService::parseFakeSpec("abc,40,,7x,9") == V{40, 9});
    CHECK(PadBatteryService::parseFakeSpec("").empty());
    CHECK(PadBatteryService::parseFakeSpec("x").empty());
    CHECK(PadBatteryService::parseFakeSpec("1,2,3,4,5,6") == V{1, 2, 3, 4});
}

TEST_CASE("the fake batteries are known pads with distinct addresses, in the spec's order") {
    std::vector<PadBatteryInfo> pads = PadBatteryService::fakeBatteries("80,5");
    REQUIRE(pads.size() == 2);
    CHECK(pads[0].percent == 80);
    CHECK(pads[1].percent == 5);
    CHECK(pads[0].known());
    CHECK(pads[0].address != pads[1].address);
    CHECK(pads[0].status == "Discharging");
    CHECK(PadBatteryService::fakeBatteries("").empty());
}

TEST_CASE("the charge rect: a fixed inset inside the body, the nub left out, at least 1 px wide") {
    // the art spec's 29 x 13 icon: body 26 wide + 3 nub, charge x 2..24, y 2..11 at 100%
    PadBatteryCharge full = PadBatteryCharge::rect(10, 20, 29, 13, 100);
    CHECK(full.x == 12);
    CHECK(full.y == 22);
    CHECK(full.w == 22);
    CHECK(full.h == 9);
    CHECK(PadBatteryCharge::rect(0, 0, 29, 13, 50).w == 11);
    CHECK(PadBatteryCharge::rect(0, 0, 29, 13, 0).w == 1);
    CHECK(PadBatteryCharge::rect(0, 0, 29, 13, -7).w == 1);
    CHECK(PadBatteryCharge::rect(0, 0, 29, 13, 400).w == 22);
    // a bigger icon keeps the inset and scales the charge
    PadBatteryCharge big = PadBatteryCharge::rect(0, 0, 43, 17, 100);
    CHECK(big.w == 36);
    CHECK(big.h == 13);
}

TEST_CASE("the charge rect of the code-drawn glyph is what the old drawing computed") {
    // iconW 26 + nub 3, iconH 13: fill = max(1, 22 * pct / 100), 9 tall at +2, +2
    const int iconW = PadBatteryCharge::BodyWidth, iconH = PadBatteryCharge::BodyHeight;
    for (int pct = 0; pct <= 100; ++pct) {
        PadBatteryCharge c = PadBatteryCharge::rect(5, 6, iconW + PadBatteryCharge::NubWidth, iconH, pct);
        CHECK(c.w == std::max(1, (iconW - 4) * pct / 100));
        CHECK(c.h == iconH - 4);
        CHECK(c.x == 7);
        CHECK(c.y == 8);
    }
}

TEST_CASE("Env::padBatteryPowerSupplyDir is settable and restored by EnvFixture") {
    EnvFixture env;
    env.setPadBatteryPowerSupplyDir("/tmp/fake-power-supply");
    CHECK(Env::padBatteryPowerSupplyDir() == "/tmp/fake-power-supply");
}
