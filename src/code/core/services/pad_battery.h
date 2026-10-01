//
// PadBatteryService: the battery level of a wireless pad, from the kernel's power_supply sysfs tree - the
// same place PSC-Bios's pairing screen already reads it from (BluezClient::readSysfsBattery, in
// autobleem-console-tools). Moved here so the launcher and a later emulator overlay (E15) can show the same
// number without a second copy of the parsing. SDL-free; nothing here is a Gui.
//
// The console's hid-sony (DualShock 3/4) and hid-playstation (DualSense, and DualShock 4 on newer kernels)
// drivers each publish one power_supply node per paired pad, named "sony_controller_battery_<mac>" /
// "ps-controller-battery-<mac>" (mac lower-case, colon-separated - the same folder BlueZ's own Battery1
// falls back to when a driver has not reported yet). A wired pad, and any pad on a build with no such
// driver, simply has no entry here - list() answers empty and the caller shows nothing for it.
//
#pragma once

#include <string>
#include <vector>

//******************
// PadBatteryInfo
//******************
struct PadBatteryInfo {
    std::string address;   // "aa:bb:cc:dd:ee:ff", parsed from the sysfs folder name
    std::string sysfsName; // the folder name itself ("sony_controller_battery_aa:bb:cc:dd:ee:ff")
    int percent = -1;      // 0-100, -1 = nothing known
    std::string status;    // "Charging" / "Discharging" / "Full" / "Not charging" / "Unknown"; "" = not reported

    bool known() const { return percent >= 0; }
};

//******************
// PadBatteryCharge
//******************
// Where the launcher fills a pad's charge into the battery glyph - pure numbers, so the code-drawn outline and the
// theme's `battery` icon (docs/ab-gui-evoui-art-spec.md, 3.) share one rule. The glyph is a body plus a nub
// `NubWidth` wide on its right; the charge sits `Inset` px inside the body (spec: x 2..24, y 2..11 of the 29 x 13
// icon), is `bodyWidth - 2 * Inset` wide at 100% (at least 1 px) and `height - 2 * Inset` tall.
struct PadBatteryCharge {
    static constexpr int NubWidth = 3;
    static constexpr int NubHeight = 7;
    static constexpr int Inset = 2;
    static constexpr int BodyWidth = 26; // the code-drawn body's width; an icon is its own width less NubWidth
    static constexpr int BodyHeight = 13;

    int x = 0, y = 0, w = 0, h = 0;

    // the charge's rect for a glyph of `glyphWidth` x `glyphHeight` (body + nub) whose top-left is (glyphX, glyphY)
    static PadBatteryCharge rect(int glyphX, int glyphY, int glyphWidth, int glyphHeight, int percent);
};

//******************
// PadBatteryFill
//******************
// The colour of the charge fill above the low-battery threshold: the theme's accent (its `selection` colour, the
// one the carousel glow and the dialogs' rules take) when the theme sets one, white when it does not. Plain ints so
// this header stays SDL-free; the launcher turns it into its renderer colour.
struct PadBatteryFill {
    int r = 255, g = 255, b = 255;

    static PadBatteryFill accentOrWhite(bool accentSet, int accentR, int accentG, int accentB);
};

//******************
// PadBatteryService
//******************
// Stateless but for the root it reads from: list() does a handful of small file reads, cheap enough to call
// a few times a minute (the launcher polls it, never every frame - see evoui_launcher_screen.cpp).
class PadBatteryService {
public:
    // `powerSupplyDir` defaults to Env::padBatteryPowerSupplyDir() (core/services/environment.h): the real
    // sysfs tree on a Linux target, "" (list() then answers empty) on Windows unless AB_PAD_BATTERY_DIR
    // points a dev host at a fake tree for testing.
    explicit PadBatteryService(std::string powerSupplyDir = defaultPowerSupplyDir());

    std::vector<PadBatteryInfo> list() const;

    static std::string defaultPowerSupplyDir();

    // the folder-name rule, pure so the tests can feed it strings directly: true + the mac (address) when
    // `folderName` is one of the two drivers' battery nodes, false (address left alone) otherwise
    static bool isPadBatteryEntry(const std::string &folderName, std::string &addressOut);

    // "capacity_level"'s words -> an approximate percent, for a kernel that has no plain "capacity" file;
    // -1 for anything unrecognised (including "Unknown")
    static int percentFromCapacityLevel(const std::string &level);

    // the dev-host hook: AB_FAKE_PAD_BATTERY="<percent>[,<percent>...]" fakes one pad per value (at most
    // MaxFakePads, each 0-100 - out of range is clamped, a token that is not a number is skipped). Pure, so the
    // tests feed it strings; the pads are named "fake_battery_<n>", Discharging, addresses 00:00:00:00:00:0<n>.
    static constexpr size_t MaxFakePads = 4;
    static std::vector<int> parseFakeSpec(const std::string &spec);
    static std::vector<PadBatteryInfo> fakeBatteries(const std::string &spec);

private:
    std::string root_;
};
