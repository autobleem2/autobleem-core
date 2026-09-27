//
// matchPadBatteries / normalizePadAddress (core/model/pad_battery_match.h) - pure string matching, no SDL
// and no filesystem, so no fixture is needed.
//
#include "doctest/doctest.h"

#include "core/model/pad_battery_match.h"

using std::string;
using std::vector;

TEST_CASE("normalizePadAddress lower-cases and strips ':' and '-'") {
    CHECK(normalizePadAddress("AA:BB:CC:DD:EE:FF") == "aabbccddeeff");
    CHECK(normalizePadAddress("aa-bb-cc-dd-ee-ff") == "aabbccddeeff");
    CHECK(normalizePadAddress("aabbccddeeff") == "aabbccddeeff");
    CHECK(normalizePadAddress("") == "");
}

TEST_CASE("matchPadBatteries matches by address regardless of separator shape") {
    PadBatteryInfo b1;
    b1.address = "aa:bb:cc:dd:ee:ff";
    b1.percent = 72;
    PadBatteryInfo b2;
    b2.address = "11-22-33-44-55-66";
    b2.percent = 10;
    vector<PadBatteryInfo> batteries{b1, b2};

    // pad 0's serial matches b2 (dash form vs colon form), pad 1 matches b1 exactly
    vector<PadBatterySource> pads{{0, "11:22:33:44:55:66"}, {1, "aa:bb:cc:dd:ee:ff"}};

    vector<MatchedPadBattery> result = matchPadBatteries(batteries, pads);
    REQUIRE(result.size() == 2);
    CHECK(result[0].battery.address == b1.address);
    CHECK(result[0].padIndex == 1);
    CHECK(result[1].battery.address == b2.address);
    CHECK(result[1].padIndex == 0);
}

TEST_CASE("matchPadBatteries leaves padIndex at -1 when nothing matches") {
    PadBatteryInfo unmatched;
    unmatched.address = "aa:bb:cc:dd:ee:ff";
    unmatched.percent = 50;

    SUBCASE("no pads at all") {
        auto result = matchPadBatteries({unmatched}, {});
        REQUIRE(result.size() == 1);
        CHECK(result[0].padIndex == -1);
    }
    SUBCASE("pads with no matching serial") {
        auto result = matchPadBatteries({unmatched}, {{0, "11:22:33:44:55:66"}, {1, ""}});
        REQUIRE(result.size() == 1);
        CHECK(result[0].padIndex == -1);
    }
    SUBCASE("an empty battery address never matches, even against an empty serial") {
        PadBatteryInfo noAddress;
        noAddress.percent = 20;
        auto result = matchPadBatteries({noAddress}, {{0, ""}});
        REQUIRE(result.size() == 1);
        CHECK(result[0].padIndex == -1);
    }
}

TEST_CASE("matchPadBatteries keeps the batteries vector's own order, one entry per battery") {
    PadBatteryInfo a;
    a.address = "aa:aa:aa:aa:aa:aa";
    PadBatteryInfo b;
    b.address = "bb:bb:bb:bb:bb:bb";
    PadBatteryInfo c;
    c.address = "cc:cc:cc:cc:cc:cc";
    auto result = matchPadBatteries({a, b, c}, {{0, "bb:bb:bb:bb:bb:bb"}});
    REQUIRE(result.size() == 3);
    CHECK(result[0].battery.address == a.address);
    CHECK(result[0].padIndex == -1);
    CHECK(result[1].battery.address == b.address);
    CHECK(result[1].padIndex == 0);
    CHECK(result[2].battery.address == c.address);
    CHECK(result[2].padIndex == -1);
}
