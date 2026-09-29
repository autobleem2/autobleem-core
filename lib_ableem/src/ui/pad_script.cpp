//
// PadScript: padsim's words for the DebugDriver's virtual pads. See the header.
//
#include "ableem/ui/pad_script.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

using namespace std;

namespace ableem {
namespace PadScript {

namespace {

// the ids padsim gives its uinput pads (the real pads'); the generic one's are ids no pad has
const Profile profiles[] = {
    {"x360", "Microsoft X-Box 360 pad", 0x045e, 0x028e, false, false, true},
    {"ds4", "Wireless Controller", 0x054c, 0x09cc, true, true, true},
    {"generic", "AutoBleem Test Pad", 0x1209, 0xab01, true, true, false},
};

// SDL's standard layout (SDL_GameControllerButton order) - a game controller's buttons
const char *const controllerButtons[] = {"a",  "b",  "x",  "y",  "select", "guide", "start", "l3",
                                         "r3", "l1", "r1", "up", "down",   "left",  "right"};
// padsim's DualShock layout (hid-playstation's code order: South East North West TL TR TL2 TR2 Select Start
// Mode ThumbL ThumbR) - the generic pad's buttons
const char *const rawButtons[] = {"a", "b", "y", "x", "l1", "r1", "l2", "r2", "select", "start", "guide", "l3", "r3"};

const char *const padWords[] = {"release", "hold", "tap",    "stick",   "trigger", "dpad",
                                "profile", "plug", "unplug", "battery", "cable",   "reset"};

int indexOf(const char *const *names, size_t count, const string &name) {
    for (size_t i = 0; i < count; i++) {
        if (name == names[i])
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace

bool findProfile(const string &name, Profile &out) {
    for (const Profile &p : profiles) {
        if (p.name == name) {
            out = p;
            return true;
        }
    }
    return false;
}

VirtualPadSpec specFor(const Profile &profile) {
    VirtualPadSpec spec;
    spec.name = profile.deviceName;
    spec.vendor = profile.vendor;
    spec.product = profile.product;
    spec.gameController = profile.gameController;
    if (profile.gameController) {
        // room for any mapping SDL may choose: its xpad-shaped one for an Xbox pad (b0..b10, a0..a5, the d-pad
        // on hat 0) or the one it builds from this layout (the d-pad as buttons 11..14)
        spec.buttons = 15;
        spec.axes = 6;
        spec.hats = 1;
        spec.triggerAxes[0] = 4;
        spec.triggerAxes[1] = 5;
    } else {
        spec.buttons = static_cast<int>(sizeof(rawButtons) / sizeof(rawButtons[0]));
        spec.axes = 6; // X Y Z RX RY RZ, the triggers on Z and RZ
        spec.hats = 1;
        spec.triggerAxes[0] = 2;
        spec.triggerAxes[1] = 5;
    }
    return spec;
}

bool parse(const string &line, Step &step, string &error) {
    step = Step();
    error.clear();
    istringstream in(line);
    string word;
    if (!(in >> word))
        return false;
    if (word[0] == '@') {
        step.explicitPad = true;
        const int n = atoi(word.c_str() + 1);
        if (word.size() != 2 || n < 1 || n > Input::VirtualPadSlots) {
            error = "pad: @1..@4";
            return true;
        }
        step.pad = n - 1;
        if (!(in >> word)) {
            error = "a command after the pad";
            return true;
        }
    } else if (indexOf(padWords, sizeof(padWords) / sizeof(padWords[0]), word) < 0) {
        return false; // not one of padsim's words - `press` alone is the driver's own
    }
    step.verb = word;
    while (in >> word)
        step.args.push_back(word);
    return true;
}

bool buttonTarget(bool gameController, const string &name, Target &target) {
    target = Target();
    if (gameController) {
        if (name == "l2" || name == "r2") {
            target.axis = name == "l2" ? 4 : 5; // a trigger: its axis at full
            return true;
        }
        // the d-pad has its own command; its buttons are 11..14
        const int i = indexOf(controllerButtons, 11, name);
        if (i < 0)
            return false;
        target.button = i;
        return true;
    }
    const int i = indexOf(rawButtons, sizeof(rawButtons) / sizeof(rawButtons[0]), name);
    if (i < 0)
        return false;
    target.button = i;
    if (name == "l2" || name == "r2")
        target.axis = name == "l2" ? 2 : 5; // hid-playstation reports both
    return true;
}

bool stickAxes(bool gameController, const string &name, int &axisX, int &axisY) {
    if (name == "left") {
        axisX = 0;
        axisY = 1;
        return true;
    }
    if (name == "right") {
        axisX = gameController ? 2 : 3;
        axisY = gameController ? 3 : 4;
        return true;
    }
    return false;
}

bool triggerTarget(bool gameController, const string &name, Target &target) {
    target = Target();
    if (name != "l2" && name != "r2")
        return false;
    const bool left = name == "l2";
    if (gameController) {
        target.axis = left ? 4 : 5;
    } else {
        target.axis = left ? 2 : 5;
        target.button = left ? 6 : 7;
    }
    return true;
}

int triggerValue(int value) {
    value = std::max(0, std::min(255, value));
    return -32768 + (value * 65535) / 255;
}

bool dpadHat(const string &direction, int &hat) {
    if (direction == "center" || direction == "centre") {
        hat = 0;
        return true;
    }
    int bits = 0;
    const bool up = direction.find("up") != string::npos;
    const bool down = direction.find("down") != string::npos;
    const bool left = direction.find("left") != string::npos;
    const bool right = direction.find("right") != string::npos;
    if ((up && down) || (left && right))
        return false;
    if (up)
        bits |= 1;
    if (right)
        bits |= 2;
    if (down)
        bits |= 4;
    if (left)
        bits |= 8;
    // nothing but the direction words and one dash between two of them
    string rest = direction;
    for (const char *w : {"up", "down", "left", "right"}) {
        size_t at = rest.find(w);
        if (at != string::npos)
            rest.erase(at, string(w).size());
    }
    if (bits == 0 || !(rest.empty() || rest == "-"))
        return false;
    hat = bits;
    return true;
}

void dpadButtons(int hat, bool down[4]) {
    down[0] = (hat & 1) != 0; // up
    down[1] = (hat & 4) != 0; // down
    down[2] = (hat & 8) != 0; // left
    down[3] = (hat & 2) != 0; // right
}

string batteryNode(int pad) {
    char name[64];
    snprintf(name, sizeof(name), "ps-controller-battery-aa:bb:cc:00:ab:%02x", pad + 1);
    return name;
}

string batteryStatus(int level, bool cable) {
    if (!cable)
        return "Discharging";
    return level >= 100 ? "Full" : "Charging";
}

} // namespace PadScript
} // namespace ableem
