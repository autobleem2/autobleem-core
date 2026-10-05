//
// PadBatteryService: see pad_battery.h.
//
#include "pad_battery.h"
#include "environment.h"
#include "../main.h"

#include <ableem/engine/filesystem.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace std;

namespace {

// the whole of a small sysfs file's first line, trimmed; "" when the file is not there - so a driver that
// has not written a value yet (or a kernel without this file at all) just leaves the row unknown
string readFirstLine(const string &path) {
    ifstream in(path, ios::binary);
    if (!in)
        return "";
    string line;
    getline(in, line);
    return Strings::trim(line);
}

const vector<string> &batteryPrefixes() {
    static const vector<string> prefixes = {"sony_controller_battery_", "ps-controller-battery-"};
    return prefixes;
}

} // namespace

//*******************************
// PadBatteryService::defaultPowerSupplyDir
//*******************************
string PadBatteryService::defaultPowerSupplyDir() {
    return Env::padBatteryPowerSupplyDir();
}

//*******************************
// PadBatteryService::PadBatteryService
//*******************************
PadBatteryService::PadBatteryService(string powerSupplyDir) : root_(std::move(powerSupplyDir)) {}

//*******************************
// PadBatteryService::isPadBatteryEntry
//*******************************
bool PadBatteryService::isPadBatteryEntry(const string &folderName, string &addressOut) {
    for (const string &prefix : batteryPrefixes()) {
        if (folderName.size() > prefix.size() && folderName.compare(0, prefix.size(), prefix) == 0) {
            addressOut = folderName.substr(prefix.size());
            return true;
        }
    }
    return false;
}

//*******************************
// PadBatteryService::percentFromCapacityLevel
//*******************************
int PadBatteryService::percentFromCapacityLevel(const string &level) {
    if (level == "Full")
        return 100;
    if (level == "High")
        return 75;
    if (level == "Normal")
        return 50;
    if (level == "Low")
        return 15;
    if (level == "Critical")
        return 5;
    return -1; // "Unknown", or anything this list does not know
}

//*******************************
// PadBatteryService::parseFakeSpec / fakeBatteries
//*******************************
vector<int> PadBatteryService::parseFakeSpec(const string &spec) {
    vector<int> percents;
    size_t start = 0;
    while (start <= spec.size() && percents.size() < MaxFakePads) {
        size_t comma = spec.find(',', start);
        string token = Strings::trim(spec.substr(start, comma == string::npos ? string::npos : comma - start));
        char *end = nullptr;
        long value = strtol(token.c_str(), &end, 10);
        if (!token.empty() && end != token.c_str() && *end == '\0')
            percents.push_back(static_cast<int>(max(0L, min(100L, value))));
        if (comma == string::npos)
            break;
        start = comma + 1;
    }
    return percents;
}

vector<PadBatteryInfo> PadBatteryService::fakeBatteries(const string &spec) {
    vector<PadBatteryInfo> out;
    for (int percent : parseFakeSpec(spec)) {
        PadBatteryInfo info;
        const string n = to_string(out.size() + 1);
        info.address = "00:00:00:00:00:0" + n;
        info.sysfsName = "fake_battery_" + n;
        info.percent = percent;
        info.status = "Discharging";
        out.push_back(info);
    }
    return out;
}

//*******************************
// PadBatteryCharge::rect
//*******************************
constexpr int PadBatteryCharge::NubWidth;
constexpr int PadBatteryCharge::NubHeight;
constexpr int PadBatteryCharge::Inset;
constexpr int PadBatteryCharge::BodyWidth;
constexpr int PadBatteryCharge::BodyHeight;

PadBatteryCharge PadBatteryCharge::rect(int glyphX, int glyphY, int glyphWidth, int glyphHeight, int percent) {
    const int bodyWidth = glyphWidth - NubWidth;
    const int share = min(100, max(0, percent));
    PadBatteryCharge charge;
    charge.x = glyphX + Inset;
    charge.y = glyphY + Inset;
    charge.w = max(1, (bodyWidth - 2 * Inset) * share / 100);
    charge.h = glyphHeight - 2 * Inset;
    return charge;
}

//*******************************
// PadBatteryBolt::strips
//*******************************
constexpr int PadBatteryBolt::Outline;

vector<PadBatteryBolt::Strip> PadBatteryBolt::strips(int glyphX, int glyphY, int glyphWidth, int glyphHeight) {
    // the designer's polygon on the 58 x 26 canvas (the 29 x 13 icon at @2x)
    static const double poly[6][2] = {{30, 4.5}, {18.5, 14.5}, {25, 14.5}, {22.5, 21.5}, {34, 11.5}, {27.5, 11.5}};
    const double sx = glyphWidth / 58.0, sy = glyphHeight / 26.0;
    vector<Strip> out;
    for (int row = 0; row < glyphHeight; ++row) {
        const double yc = (row + 0.5) / sy; // the pixel row's centre, on the canvas
        vector<double> crossings;
        for (int i = 0; i < 6; ++i) {
            const double *a = poly[i], *b = poly[(i + 1) % 6];
            if ((a[1] <= yc && yc < b[1]) || (b[1] <= yc && yc < a[1]))
                crossings.push_back(a[0] + (yc - a[1]) / (b[1] - a[1]) * (b[0] - a[0]));
        }
        sort(crossings.begin(), crossings.end());
        for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const int from = static_cast<int>(crossings[i] * sx + 0.5);
            const int to = static_cast<int>(crossings[i + 1] * sx + 0.5);
            Strip strip;
            strip.x = glyphX + from;
            strip.y = glyphY + row;
            strip.w = max(1, to - from);
            out.push_back(strip);
        }
    }
    return out;
}

//*******************************
// PadBatteryFill::accentOrWhite
//*******************************
PadBatteryFill PadBatteryFill::accentOrWhite(bool accentSet, int accentR, int accentG, int accentB) {
    PadBatteryFill fill; // white
    if (accentSet) {
        fill.r = accentR;
        fill.g = accentG;
        fill.b = accentB;
    }
    return fill;
}

//*******************************
// PadBatteryService::list
//*******************************
vector<PadBatteryInfo> PadBatteryService::list() const {
    vector<PadBatteryInfo> out;
#ifdef AB_DEBUG_HOST
    // AB_FAKE_PAD_BATTERY=<percent>[,<percent>]: a dev host has no wireless pad - this fakes them (never read on
    // a console, a Pi or a PC stick)
    if (const char *fake = getenv("AB_FAKE_PAD_BATTERY")) {
        out = fakeBatteries(fake);
        if (!out.empty())
            return out;
    }
#endif
    if (root_.empty() || !DirEntry::exists(root_))
        return out;

    // each pad's battery is a sub-directory (capacity/status/... inside it), not a plain file - listNames()
    // filters directories out (it is built for a flat file listing, e.g. Named_Boxarts), so this needs
    // diru_DirsOnly() instead
    for (const DirEntry &entry : DirEntry::diru_DirsOnly(root_)) {
        string address;
        if (!isPadBatteryEntry(entry.name, address))
            continue;

        string dir = root_ + sep + entry.name;
        PadBatteryInfo info;
        info.sysfsName = entry.name;
        info.address = address;
        info.status = readFirstLine(dir + sep + "status");

        string capacity = readFirstLine(dir + sep + "capacity");
        if (!capacity.empty()) {
            char *end = nullptr;
            long percent = strtol(capacity.c_str(), &end, 10);
            if (end != capacity.c_str())
                info.percent = static_cast<int>(max(0L, min(100L, percent)));
        }
        if (info.percent < 0) {
            string level = readFirstLine(dir + sep + "capacity_level");
            if (!level.empty())
                info.percent = percentFromCapacityLevel(level);
        }
        out.push_back(info);
    }

    sort(out.begin(), out.end(),
         [](const PadBatteryInfo &a, const PadBatteryInfo &b) { return a.address < b.address; });
    return out;
}
