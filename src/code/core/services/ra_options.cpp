//
// RaOptionsService: the per-game options of a RetroArch game (EMU-25) and what they mean for RetroArch.
//
#include "ra_options.h"
#include "environment.h"
#include "../main.h"

#include <ableem/engine/log.h>

#include <fstream>
#include <sstream>

using namespace std;

namespace {

// how many numbers the text holds, and each row's number of values
const int RowCount = 7;
const int RowValues[RowCount] = {RaGameOptions::AspectCount, RaGameOptions::TriCount, RaGameOptions::TriCount,
                                 RaGameOptions::ScanCount,   RaGameOptions::TriCount, RaGameOptions::TriCount,
                                 RaGameOptions::ResumeCount};

} // namespace

//*******************************
// RaGameOptions
//*******************************
bool RaGameOptions::operator==(const RaGameOptions &o) const {
    return aspect == o.aspect && integerScaling == o.integerScaling && smoothing == o.smoothing &&
           scanlines == o.scanlines && showFps == o.showFps && analogAsDpad == o.analogAsDpad && resume == o.resume;
}

string RaGameOptions::encode() const {
    ostringstream out;
    out << aspect << ',' << integerScaling << ',' << smoothing << ',' << scanlines << ',' << showFps << ','
        << analogAsDpad << ',' << resume;
    return out.str();
}

RaGameOptions RaGameOptions::decode(const string &text) {
    int values[RowCount] = {};
    istringstream in(text);
    for (int i = 0; i < RowCount; i++) {
        string part;
        if (!getline(in, part, ','))
            break;
        char *end = nullptr;
        const long v = strtol(part.c_str(), &end, 10);
        if (end != part.c_str() && *end == '\0' && v >= 0 && v < RowValues[i])
            values[i] = static_cast<int>(v);
    }
    RaGameOptions o;
    o.aspect = values[0];
    o.integerScaling = values[1];
    o.smoothing = values[2];
    o.scanlines = values[3];
    o.showFps = values[4];
    o.analogAsDpad = values[5];
    o.resume = values[6];
    return o;
}

//*******************************
// RaOptionsService
//*******************************
RaOptionsService::RaOptionsService() {
    load();
}

string RaOptionsService::optionsFile() {
    return Env::getPathToSystemDir() + sep + "ra-game-options.txt";
}

RaGameOptions RaOptionsService::get(const PsGame &game) const {
    if (!game.foreign || game.app)
        return RaGameOptions();
    auto it = byPath_.find(game.image_path);
    return it == byPath_.end() ? RaGameOptions() : it->second;
}

void RaOptionsService::set(const PsGame &game, const RaGameOptions &options) {
    if (!game.foreign || game.app || game.image_path.empty())
        return;
    if (get(game) == options)
        return;
    if (options.isDefault())
        byPath_.erase(game.image_path);
    else
        byPath_[game.image_path] = options;
    save();
}

void RaOptionsService::load() {
    byPath_.clear();
    ifstream in(optionsFile().c_str());
    string line;
    while (in.is_open() && getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == string::npos || tab + 1 >= line.size())
            continue;
        const RaGameOptions options = RaGameOptions::decode(line.substr(0, tab));
        if (!options.isDefault())
            byPath_[line.substr(tab + 1)] = options;
    }
}

void RaOptionsService::save() const {
    const string path = optionsFile();
    if (byPath_.empty()) {
        if (DirEntry::exists(path))
            DirEntry::removeFile(path);
        return;
    }
    string text;
    for (const auto &entry : byPath_)
        text += entry.second.encode() + '\t' + entry.first + '\n';
    DirEntry::createDirs(Env::getPathToSystemDir());
    DirEntry::writeFileIfChanged(path, text);
}

//*******************************
// RaOptionsService::aspectIndexFor
//*******************************
// RetroArch 1.22.2, gfx/video_defines.h: ASPECT_RATIO_4_3 0 ... ASPECT_RATIO_CONFIG 20, SQUARE 21 (square pixels),
// CORE 22 (what the core reports), CUSTOM 23 (the viewport launch.cpp sets), FULL 24 (stretched to the screen)
string RaOptionsService::aspectIndexFor(int aspect) {
    switch (aspect) {
    case RaGameOptions::AspectCore:
        return "22";
    case RaGameOptions::Aspect43:
        return "0";
    case RaGameOptions::AspectFull:
        return "24";
    case RaGameOptions::AspectPixel:
        return "21";
    default:
        return "";
    }
}

//*******************************
// RaOptionsService::apply
//*******************************
void RaOptionsService::apply(const RaGameOptions &options, ableem::ConfigFileEditor::CfgLines &lines) {
    auto set = [&lines](const string &key, const string &value) {
        const string line = key + " = \"" + value + "\"";
        for (auto &existing : lines) {
            if (existing.first == key) {
                existing.second = line;
                return;
            }
        }
        lines.emplace_back(key, line);
    };
    auto onOff = [&set](const char *key, int tri) {
        if (tri != RaGameOptions::TriDefault)
            set(key, tri == RaGameOptions::TriOn ? "true" : "false");
    };

    const string aspect = aspectIndexFor(options.aspect);
    if (!aspect.empty())
        set("aspect_ratio_index", aspect);
    onOff("video_scale_integer", options.integerScaling);
    onOff("video_smooth", options.smoothing);
    onOff("fps_show", options.showFps);
    if (options.analogAsDpad != RaGameOptions::TriDefault) // ANALOG_DPAD_NONE 0, ANALOG_DPAD_LSTICK 1
        set("input_player1_analog_dpad_mode", options.analogAsDpad == RaGameOptions::TriOn ? "1" : "0");
    switch (options.scanlines) {
    case RaGameOptions::ScanOff:
        set("input_overlay_enable", "false");
        break;
    case RaGameOptions::ScanLight:
    case RaGameOptions::ScanStrong:
        set("input_overlay", ":/overlay/scanlines.cfg");
        set("input_overlay_enable", "true");
        set("input_overlay_opacity", options.scanlines == RaGameOptions::ScanLight ? "0.250000" : "0.600000");
        break;
    default:
        break;
    }
}
