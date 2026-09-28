//
// PadScript (pad_script.h): padsim's words for the DebugDriver's virtual pads - the parsing, the profiles and
// the two layouts. Pure, no SDL.
//
#include "doctest/doctest.h"

#include "ableem/ui/pad_script.h"

#include <string>

using namespace std;
using namespace ableem;

TEST_CASE("parse: @n picks the pad, padsim's words are pad steps, a bare press stays the driver's") {
    PadScript::Step s;
    string error;

    REQUIRE(PadScript::parse("@2 press a", s, error));
    CHECK(error.empty());
    CHECK(s.pad == 1);
    CHECK(s.explicitPad);
    CHECK(s.verb == "press");
    REQUIRE(s.args.size() == 1);
    CHECK(s.args[0] == "a");

    REQUIRE(PadScript::parse("hold b 300", s, error));
    CHECK(s.pad == 0);
    CHECK_FALSE(s.explicitPad);
    CHECK(s.verb == "hold");
    CHECK(s.args.size() == 2);

    REQUIRE(PadScript::parse("profile ds4 bt", s, error));
    CHECK(s.verb == "profile");

    // the driver's own logical press, key, shot ... are not pad steps
    CHECK_FALSE(PadScript::parse("press x", s, error));
    CHECK_FALSE(PadScript::parse("key escape", s, error));
    CHECK_FALSE(PadScript::parse("shot a.png", s, error));
    CHECK_FALSE(PadScript::parse("", s, error));
}

TEST_CASE("parse: a bad pad number or a pad with no command is a pad step with an error") {
    PadScript::Step s;
    string error;
    REQUIRE(PadScript::parse("@5 press a", s, error));
    CHECK_FALSE(error.empty());
    REQUIRE(PadScript::parse("@0 plug", s, error));
    CHECK_FALSE(error.empty());
    REQUIRE(PadScript::parse("@12 plug", s, error));
    CHECK_FALSE(error.empty());
    REQUIRE(PadScript::parse("@3", s, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("profiles: padsim's three, with the real pads' ids; only the generic one has no mapping") {
    PadScript::Profile p;
    REQUIRE(PadScript::findProfile("x360", p));
    CHECK(p.vendor == 0x045e);
    CHECK(p.product == 0x028e);
    CHECK_FALSE(p.bluetooth);
    CHECK_FALSE(p.hasBattery);
    CHECK(p.gameController);

    REQUIRE(PadScript::findProfile("ds4", p));
    CHECK(p.vendor == 0x054c);
    CHECK(p.product == 0x09cc);
    CHECK(p.bluetooth);
    CHECK(p.hasBattery);

    REQUIRE(PadScript::findProfile("generic", p));
    CHECK_FALSE(p.gameController);
    VirtualPadSpec spec = PadScript::specFor(p);
    CHECK_FALSE(spec.gameController);
    CHECK(spec.buttons == 13);
    CHECK(spec.hats == 1);
    CHECK(spec.triggerAxes[0] == 2);
    CHECK(spec.triggerAxes[1] == 5);

    REQUIRE(PadScript::findProfile("x360", p));
    spec = PadScript::specFor(p);
    CHECK(spec.gameController);
    CHECK(spec.buttons == 15);
    CHECK(spec.axes == 6);
    CHECK(spec.name == "Microsoft X-Box 360 pad");

    CHECK_FALSE(PadScript::findProfile("dualsense", p));
}

TEST_CASE("buttonTarget: the Xbox names on both layouts, the triggers as axes") {
    PadScript::Target t;
    // a game controller: SDL's standard order
    REQUIRE(PadScript::buttonTarget(true, "a", t));
    CHECK(t.button == 0);
    CHECK(t.axis == -1);
    REQUIRE(PadScript::buttonTarget(true, "y", t));
    CHECK(t.button == 3);
    REQUIRE(PadScript::buttonTarget(true, "start", t));
    CHECK(t.button == 6);
    REQUIRE(PadScript::buttonTarget(true, "r1", t));
    CHECK(t.button == 10);
    REQUIRE(PadScript::buttonTarget(true, "l2", t));
    CHECK(t.button == -1);
    CHECK(t.axis == 4);
    CHECK_FALSE(PadScript::buttonTarget(true, "up", t)); // the d-pad has its own command
    CHECK_FALSE(PadScript::buttonTarget(true, "cross", t));

    // the generic pad: padsim's DualShock order, L2/R2 a button and an axis
    REQUIRE(PadScript::buttonTarget(false, "y", t));
    CHECK(t.button == 2);
    REQUIRE(PadScript::buttonTarget(false, "x", t));
    CHECK(t.button == 3);
    REQUIRE(PadScript::buttonTarget(false, "r2", t));
    CHECK(t.button == 7);
    CHECK(t.axis == 5);
}

TEST_CASE("sticks and triggers") {
    int x = -1, y = -1;
    REQUIRE(PadScript::stickAxes(true, "right", x, y));
    CHECK(x == 2);
    CHECK(y == 3);
    REQUIRE(PadScript::stickAxes(false, "right", x, y));
    CHECK(x == 3);
    CHECK(y == 4);
    CHECK_FALSE(PadScript::stickAxes(true, "middle", x, y));

    PadScript::Target t;
    REQUIRE(PadScript::triggerTarget(false, "l2", t));
    CHECK(t.axis == 2);
    CHECK(t.button == 6);
    CHECK(PadScript::triggerValue(0) == -32768);
    CHECK(PadScript::triggerValue(255) == 32767);
    CHECK(PadScript::triggerValue(999) == 32767);
    CHECK(PadScript::triggerValue(-5) == -32768);
}

TEST_CASE("dpad: padsim's directions as hat bits and d-pad buttons") {
    int hat = -1;
    REQUIRE(PadScript::dpadHat("up", hat));
    CHECK(hat == 1);
    REQUIRE(PadScript::dpadHat("down-left", hat));
    CHECK(hat == (4 | 8));
    REQUIRE(PadScript::dpadHat("center", hat));
    CHECK(hat == 0);
    CHECK_FALSE(PadScript::dpadHat("up-down", hat));
    CHECK_FALSE(PadScript::dpadHat("sideways", hat));
    CHECK_FALSE(PadScript::dpadHat("upward", hat));

    bool down[4];
    PadScript::dpadButtons(2 | 1, down); // up-right
    CHECK(down[0]);
    CHECK_FALSE(down[1]);
    CHECK_FALSE(down[2]);
    CHECK(down[3]);
}

TEST_CASE("the battery node and its status, as padsim writes them") {
    CHECK(PadScript::batteryNode(0) == "ps-controller-battery-aa:bb:cc:00:ab:01");
    CHECK(PadScript::batteryNode(3) == "ps-controller-battery-aa:bb:cc:00:ab:04");
    CHECK(PadScript::batteryStatus(50, false) == "Discharging");
    CHECK(PadScript::batteryStatus(50, true) == "Charging");
    CHECK(PadScript::batteryStatus(100, true) == "Full");
    CHECK(PadScript::batteryStatus(100, false) == "Discharging");
}
