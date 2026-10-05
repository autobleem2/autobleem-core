//
// The launcher's "battery low" notice for wireless pads, as a pure rule (no Gui, no clock): feed it each poll's
// PadBatteryService::list() and it says which pads to warn about and which warnings to take down again.
//
//  - a pad at or under LowPercent that is not on a charger gets ONE warning (Show);
//  - the warning is taken down (Dismiss) when the pad goes on a charger (status Charging or Full), when its level
//    climbs to ResetPercent or more (a margin above LowPercent - the hysteresis: a reading bouncing around the
//    threshold neither re-warns nor flaps the notice), or when the pad's battery node is gone;
//  - a pad that is charging never gets a warning, whatever its level; the warning for a pad that is low again after
//    the charger was pulled comes once more (it is low).
// Between LowPercent and ResetPercent, off the charger, nothing changes: the notice stays and is not repeated.
//
#pragma once

#include "../services/pad_battery.h"

#include <set>
#include <string>
#include <vector>

class PadBatteryAlert {
public:
    static constexpr int LowPercent = 15;   // at or under this: warn
    static constexpr int ResetPercent = 25; // at or over this: the warning is over (and may come again later)

    enum class Kind { Show, Dismiss };

    struct Event {
        Kind kind = Kind::Show;
        std::string address;
        size_t index = 0; // Show: the pad's position in the list handed to update(); Dismiss: unused (0)
    };

    // the pad is on a charger (charging, or full on one): never warned about, and a standing warning goes
    static bool onCharger(const PadBatteryInfo &pad) { return pad.charging() || pad.full(); }

    // one poll; the events to act on, in the list's order (Dismiss events of vanished pads last)
    std::vector<Event> update(const std::vector<PadBatteryInfo> &pads) {
        std::vector<Event> events;
        for (size_t i = 0; i < pads.size(); ++i) {
            const PadBatteryInfo &pad = pads[i];
            if (!pad.known())
                continue;
            const bool notified = notified_.count(pad.address) != 0;
            if (onCharger(pad) || pad.percent >= ResetPercent) {
                if (notified) {
                    notified_.erase(pad.address);
                    events.push_back({Kind::Dismiss, pad.address, 0});
                }
            } else if (pad.percent <= LowPercent && !notified) {
                notified_.insert(pad.address);
                events.push_back({Kind::Show, pad.address, i});
            }
        }
        // a pad that vanished (unplugged, or its battery node went away): its warning goes, and it may warn again
        // if it comes back low
        for (auto it = notified_.begin(); it != notified_.end();) {
            bool present = false;
            for (const PadBatteryInfo &pad : pads)
                if (pad.address == *it) {
                    present = true;
                    break;
                }
            if (present) {
                ++it;
            } else {
                events.push_back({Kind::Dismiss, *it, 0});
                it = notified_.erase(it);
            }
        }
        return events;
    }

    bool notified(const std::string &address) const { return notified_.count(address) != 0; }

private:
    std::set<std::string> notified_;
};
