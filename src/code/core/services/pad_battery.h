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

private:
    std::string root_;
};
