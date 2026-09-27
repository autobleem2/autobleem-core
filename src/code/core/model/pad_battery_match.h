//
// Matching a wireless pad's sysfs battery reading (PadBatteryService, core/services/pad_battery.h) to the
// specific SDL pad the launcher shows as "Player 1" / "Player 2" (C12, polish on C8/C9) - so a battery icon
// and a low-battery notice can say which pad they are about instead of a generic "Wireless pad 1/2" built
// from address-sort order alone.
//
// PadBatteryInfo::address is the MAC out of the sysfs folder name ("sony_controller_battery_<mac>" /
// "ps-controller-battery-<mac>"), unchanged by C12 - see pad_battery.h. ableem::PadInfo::serial
// (lib_ableem, added for this) is SDL_JoystickGetSerial(): for a DualShock 4 / DualSense read through
// Linux's hidraw backend that is the same MAC, in the same shape. Header-only, like pad_assignment.h next
// to it - no SDL here, just string comparison, so it is usable (and tested) without a real pad or a real
// sysfs tree.
//
#pragma once

#include "../main.h"
#include "../services/pad_battery.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// One connected pad's identity for matching, as much as a caller can give without pulling in SDL here:
// `index` is its position in ableem::Input::pads() (0-based - Player 1 is index 0, Player 2 index 1, see
// psPlayerSlot() in pad_assignment.h), `serial` is ableem::PadInfo::serial ("" when SDL has none).
struct PadBatterySource {
    int index = 0;
    std::string serial;
};

// A PadBatteryInfo together with the pads() index it was matched to, or -1 when nothing matched (no pad
// reported that address as its serial - unplugged since, a driver that has not published one yet, a wired
// pad with a battery node left behind, or simply an SDL/pad combination that never reports a serial at
// all - a Windows dev host among them).
struct MatchedPadBattery {
    PadBatteryInfo battery;
    int padIndex = -1;
};

// Lower-cased, with ':' and '-' separators stripped, so "AA:BB:..", "aa-bb-.." and "aabb.." all compare
// equal - PadBatteryService's address and SDL's serial have both been seen in more than one of these
// shapes across kernels/backends, and neither side is a promise to keep one.
inline std::string normalizePadAddress(const std::string &address) {
    std::string out;
    out.reserve(address.size());
    for (char c : address) {
        if (c == ':' || c == '-')
            continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// `batteries` is PadBatteryService::list()'s result, in its own (address-sorted) order - every entry
// appears exactly once in the result, in that same order. `pads` is every currently connected pad's
// index + serial (any order; only entries with a non-empty serial can ever match).
inline std::vector<MatchedPadBattery> matchPadBatteries(const std::vector<PadBatteryInfo> &batteries,
                                                        const std::vector<PadBatterySource> &pads) {
    std::vector<MatchedPadBattery> result;
    result.reserve(batteries.size());
    for (const PadBatteryInfo &battery : batteries) {
        MatchedPadBattery matched;
        matched.battery = battery;
        const std::string wanted = normalizePadAddress(battery.address);
        if (!wanted.empty()) {
            auto it = std::find_if(pads.begin(), pads.end(), [&](const PadBatterySource &pad) {
                return !pad.serial.empty() && normalizePadAddress(pad.serial) == wanted;
            });
            if (it != pads.end())
                matched.padIndex = it->index;
        }
        result.push_back(matched);
    }
    return result;
}

// PadBatteryInfo::status, translated: the kernel's power_supply "status" file only ever holds a handful of
// English words (see Linux's power_supply_sysfs.c) - literal _() calls for the ones actually seen from a
// pad's battery ("Unknown" included, even though it says nothing worth showing on its own - a caller
// checks for "" rather than testing the untranslated English word). Anything this list does not recognise
// (a status a newer/other kernel reports under a different name) is passed through untranslated rather
// than silently dropped, so it is still visible as *something* if never as a translated one. Shared here,
// not duplicated as a static per-file copy like psPlayerSlotLabel, so both the launcher's icon row and
// GuiHardwareInfo's battery row (and anything else that ever shows a status) read the one list.
inline std::string batteryStatusText(const std::string &status) {
    if (status == "Charging")
        return _("Charging");
    if (status == "Discharging")
        return _("Discharging");
    if (status == "Full")
        return _("Full");
    if (status == "Not charging")
        return _("Not charging");
    if (status == "Unknown")
        return "";
    return status;
}
