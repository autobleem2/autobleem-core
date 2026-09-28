//
// PadScript: the words of the test VM's padsim (tools/vm/padsim.c in the launcher repository), for the
// DebugDriver's virtual pads - so a test script reads the same whether a pad in the VM (padsim, a uinput
// device) or a pad inside the process (Input::plugVirtualPad) plays it. Pure: parsing, the profiles and
// the layouts, no SDL - the DebugDriver does the rest.
//
//   [@<n>] profile <x360|ds4|generic> [usb|bt] | plug | unplug | battery <0..100>|off | cable in|out
//   [@<n>] press|release <btn> | hold <btn> <ms> | tap <btn> [ms] | stick <left|right> <x> <y>
//   [@<n>] trigger <l2|r2> <0..255> | dpad <up|down|left|right|up-left|...|center> | reset
//
// n is 1..4 (pad 1 without it). Buttons have the Xbox names on every profile: a b x y (Cross Circle Square
// Triangle on a DualShock) l1 r1 l2 r2 select start guide l3 r3. `press` belongs to the DebugDriver's own
// logical pad too (`press x [ms]`: a Cross, down and up) - so without an @ it stays that; only `@n press`
// is the virtual pad's.
//
#pragma once

#include "input.h"

#include <string>
#include <vector>

namespace ableem {

namespace PadScript {

struct Profile {
    std::string name;       // x360, ds4, generic
    std::string deviceName; // what the kernel driver calls the real pad
    unsigned short vendor = 0, product = 0;
    bool bluetooth = false;     // it exists over Bluetooth too
    bool hasBattery = false;    // a wireless pad (an x360 is wired)
    bool gameController = true; // SDL maps it; false: a pad SDL has no mapping for (the wizard's case)
};

// false for an unknown name
ABLEEM_API bool findProfile(const std::string &name, Profile &out);
// what Input::plugVirtualPad is given for it: the standard layout for a game controller, padsim's DualShock
// layout (13 buttons, X Y Z RX RY RZ, one hat) for the generic pad
ABLEEM_API VirtualPadSpec specFor(const Profile &profile);

struct Step {
    int pad = 0; // 0-based
    std::string verb;
    std::vector<std::string> args;
    bool explicitPad = false; // it began with @n
};

// true when the line is a pad step (an @n, or one of padsim's words other than `press`); `error` is set
// when it is one but cannot be (a bad @n, nothing after it)
ABLEEM_API bool parse(const std::string &line, Step &step, std::string &error);

// the pad's input a button name moves: on a game controller SDL's standard button (SDL_GameControllerButton) or
// axis (SDL_GameControllerAxis - the triggers, put at full) for Input::setVirtualPadControl*; on the generic pad
// its raw button index and trigger axis (a DualShock's L2 is both); false for an unknown name. The same holds
// for the stick and trigger functions below.
struct Target {
    int button = -1;
    int axis = -1;
};
ABLEEM_API bool buttonTarget(bool gameController, const std::string &name, Target &target);
// the axes of a stick (left/right), false for another name
ABLEEM_API bool stickAxes(bool gameController, const std::string &name, int &axisX, int &axisY);
// a trigger's axis and (the generic pad's) button
ABLEEM_API bool triggerTarget(bool gameController, const std::string &name, Target &target);
// 0..255 (padsim's range) -> SDL's -32768..32767, 0 at rest = -32768
ABLEEM_API int triggerValue(int value);
// a d-pad direction: SDL's hat bits (1 up, 2 right, 4 down, 8 left; 0 center); false for an unknown word
ABLEEM_API bool dpadHat(const std::string &direction, int &hat);
// a game controller's d-pad buttons (11 up, 12 down, 13 left, 14 right) for those hat bits, as down/up
ABLEEM_API void dpadButtons(int hat, bool down[4]);

// the power_supply node padsim and the Sony drivers name a pad's battery by: pad n's MAC is
// aa:bb:cc:00:ab:0n
ABLEEM_API std::string batteryNode(int pad);
// its `status` file: Discharging without a cable, Charging with one, Full at 100 on one
ABLEEM_API std::string batteryStatus(int level, bool cable);

} // namespace PadScript

} // namespace ableem
