//
// PadBatteryAlert: when a wireless pad's "battery low" notice is shown, and when it is taken down again.
//
#include "doctest/doctest.h"

#include "core/model/pad_battery_alert.h"

#include <string>
#include <vector>

using std::string;
using std::vector;

namespace {

PadBatteryInfo pad(const string &address, int percent, const string &status = "Discharging") {
    PadBatteryInfo info;
    info.address = address;
    info.percent = percent;
    info.status = status;
    return info;
}

size_t count(const vector<PadBatteryAlert::Event> &events, PadBatteryAlert::Kind kind) {
    size_t n = 0;
    for (const PadBatteryAlert::Event &event : events)
        if (event.kind == kind)
            ++n;
    return n;
}

} // namespace

TEST_CASE("the status words: Charging and Full are a pad on a charger, nothing else is") {
    CHECK(pad("a", 50, "Charging").charging());
    CHECK(pad("a", 100, "Full").full());
    CHECK(PadBatteryAlert::onCharger(pad("a", 5, "Charging")));
    CHECK(PadBatteryAlert::onCharger(pad("a", 100, "Full")));
    CHECK_FALSE(PadBatteryAlert::onCharger(pad("a", 5, "Discharging")));
    CHECK_FALSE(PadBatteryAlert::onCharger(pad("a", 5, "Not charging")));
    CHECK_FALSE(PadBatteryAlert::onCharger(pad("a", 5, "Unknown")));
    CHECK_FALSE(PadBatteryAlert::onCharger(pad("a", 5, "")));
}

TEST_CASE("a low pad is warned about once, however many polls it stays low") {
    PadBatteryAlert alert;
    auto first = alert.update({pad("a", 12)});
    REQUIRE(first.size() == 1);
    CHECK(first[0].kind == PadBatteryAlert::Kind::Show);
    CHECK(first[0].address == "a");
    CHECK(first[0].index == 0);
    for (int i = 0; i < 5; ++i)
        CHECK(alert.update({pad("a", 12)}).empty());
    CHECK(alert.update({pad("a", 11)}).empty());
}

TEST_CASE("the warning threshold is inclusive, above it nothing") {
    PadBatteryAlert alert;
    CHECK(alert.update({pad("a", 16)}).empty());
    CHECK(alert.update({pad("a", 15)}).size() == 1);
}

TEST_CASE("charging starts: the warning is dismissed, and a charging pad is not warned about") {
    PadBatteryAlert alert;
    REQUIRE(alert.update({pad("a", 10)}).size() == 1);
    auto events = alert.update({pad("a", 10, "Charging")});
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == PadBatteryAlert::Kind::Dismiss);
    CHECK(events[0].address == "a");
    CHECK_FALSE(alert.notified("a"));
    // the level climbing through the low zone while it charges: never a new warning
    for (int percent : {10, 11, 12, 14, 15, 15, 13}) {
        CHECK(alert.update({pad("a", percent, "Charging")}).empty());
    }
    // a pad plugged in already low from the start is not warned about either
    PadBatteryAlert fresh;
    CHECK(fresh.update({pad("b", 3, "Charging")}).empty());
    CHECK(fresh.update({pad("b", 100, "Full")}).empty());
}

TEST_CASE("the level recovers over the reset margin: the warning goes; inside the margin it stays, silently") {
    PadBatteryAlert alert;
    REQUIRE(alert.update({pad("a", 14)}).size() == 1);
    // 16..24 off the charger: the notice stays, nothing is repeated, nothing flaps
    for (int percent : {16, 20, 24, 15, 16, 24})
        CHECK(alert.update({pad("a", percent)}).empty());
    auto events = alert.update({pad("a", 25)});
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == PadBatteryAlert::Kind::Dismiss);
    // low again later: a new warning
    auto again = alert.update({pad("a", 14)});
    REQUIRE(again.size() == 1);
    CHECK(again[0].kind == PadBatteryAlert::Kind::Show);
}

TEST_CASE("a reading bouncing around the threshold does not re-warn") {
    PadBatteryAlert alert;
    size_t shows = 0;
    for (int percent : {15, 16, 15, 14, 16, 15, 17, 14, 15, 16, 20, 15})
        shows += count(alert.update({pad("a", percent)}), PadBatteryAlert::Kind::Show);
    CHECK(shows == 1);
}

TEST_CASE("charger pulled while still low: the warning comes once more") {
    PadBatteryAlert alert;
    REQUIRE(alert.update({pad("a", 8)}).size() == 1);
    REQUIRE(alert.update({pad("a", 9, "Charging")}).size() == 1); // dismissed
    auto events = alert.update({pad("a", 9)});
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == PadBatteryAlert::Kind::Show);
}

TEST_CASE("a pad that vanishes loses its warning, and may be warned again when it comes back low") {
    PadBatteryAlert alert;
    REQUIRE(alert.update({pad("a", 8), pad("b", 60)}).size() == 1);
    auto events = alert.update({pad("b", 60)});
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == PadBatteryAlert::Kind::Dismiss);
    CHECK(events[0].address == "a");
    CHECK(alert.update({pad("a", 8), pad("b", 60)}).size() == 1);
}

TEST_CASE("pads are independent, and an unknown reading changes nothing") {
    PadBatteryAlert alert;
    auto events = alert.update({pad("a", 5), pad("b", 5)});
    REQUIRE(events.size() == 2);
    CHECK(events[1].index == 1);
    // b goes on a charger, a stays low
    events = alert.update({pad("a", 5), pad("b", 6, "Charging")});
    REQUIRE(events.size() == 1);
    CHECK(events[0].address == "b");
    CHECK(alert.notified("a"));
    // a reading that is unknown (-1) leaves the standing warning alone
    PadBatteryInfo unknown = pad("a", -1);
    CHECK(alert.update({unknown, pad("b", 6, "Charging")}).empty());
    CHECK(alert.notified("a"));
}
