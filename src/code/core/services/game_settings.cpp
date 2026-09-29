//
// GameSettingsService: what the game editor edits - a game's Game.ini flags and its pcsx.cfg values.
//
#include "game_settings.h"
#include "../main.h"
#include "environment.h"
#include "pcsx_config.h"

#include <ableem/engine/config_file_editor.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>

using namespace std;

const char *const GameSettingsService::BuiltinGpu = "builtin_gpu";
const char *const GameSettingsService::PeopsGpu = "gpu_peops.so";
const int GameSettingsService::SmoothingCount;
const char *const GameSettingsService::SmoothingNames[SmoothingCount] = {"None", "Scale2x", "Eagle2x", "HQ2x", "HQ3x"};
const int GameSettingsService::FilterCount;
const int GameSettingsService::DitheringCount;
const int GameSettingsService::ScanlineModes;

namespace {

// the levels are stored in hex in pcsx.cfg (100 -> "64")
string toHex(int value) {
    stringstream ss;
    ss << hex << value;
    return ss.str();
}

int clampTo(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

// 0, 1, ... n-1
vector<int> firstValues(int n) {
    vector<int> values;
    for (int i = 0; i < n; i++)
        values.push_back(i);
    return values;
}

bool listed(const vector<int> &values, int value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

//*******************************
// GameSettingsService::filtersFor / smoothingsFor / neonGpuFor / stepIn
//*******************************
vector<int> GameSettingsService::filtersFor(const string &platform) {
    // pcsx-abnxt's hwfilters per target: its GL pipeline (plat_autobleem.c, hwfilters = ab_filter_names) has
    // every pass on every one today. 0 Nearest, 1 Linear, 2 Sharp, 3 Sharp (simple), 4 Quilez, 5 CRT (fast),
    // 6 CRT-Pi. A target that loses a pass drops it from its own line.
    static const map<string, vector<int>> perPlatform = {
        {"psc", {0, 1, 2, 3, 4, 5, 6}}, {"rpi", {0, 1, 2, 3, 4, 5, 6}}, {"pcusb", {0, 1, 2, 3, 4, 5, 6}},
        {"win", {0, 1, 2, 3, 4, 5, 6}}, {"pc", {0, 1, 2, 3, 4, 5, 6}},
    };
    auto it = perPlatform.find(platform);
    return it != perPlatform.end() ? it->second : firstValues(FilterCount);
}

vector<int> GameSettingsService::smoothingsFor(const string &platform) {
    // pcsx-abnxt's men_ab_smooth_psc on the console (it takes hq2x/hq3x for none there), men_ab_smooth elsewhere
    static const map<string, vector<int>> perPlatform = {
        {"psc", {0, 1, 2}},       {"rpi", {0, 1, 2, 3, 4}}, {"pcusb", {0, 1, 2, 3, 4}},
        {"win", {0, 1, 2, 3, 4}}, {"pc", {0, 1, 2, 3, 4}},
    };
    auto it = perPlatform.find(platform);
    return it != perPlatform.end() ? it->second : firstValues(SmoothingCount);
}

bool GameSettingsService::neonGpuFor(const string &platform, bool nxtEmulator) {
    return nxtEmulator || platform == "psc" || platform == "rpi";
}

int GameSettingsService::stepIn(const vector<int> &values, int current, int step) {
    if (values.empty())
        return current;
    auto it = std::find(values.begin(), values.end(), current);
    if (it == values.end())
        return values.front();
    int at = clampTo(static_cast<int>(it - values.begin()) + step, 0, static_cast<int>(values.size()) - 1);
    return values[at];
}

//*******************************
// GameSettingsService::cfgFolder
//*******************************
string GameSettingsService::cfgFolder(const GameSettings &s) {
    return s.internal ? s.game->ssFolder : s.game->folder;
}

//*******************************
// GameSettingsService::fallBackToSonyCardIfSetIsGone
//*******************************
void GameSettingsService::fallBackToSonyCardIfSetIsGone(GameSettings &s) {
    if (s.ini.values["memcard"] != "SONY") {
        string cardpath = Env::getPathToMemCardsDir() + sep + s.ini.values["memcard"];
        if (!DirEntry::exists(cardpath)) {
            s.ini.values["memcard"] = "SONY";
        }
    }
}

//*******************************
// GameSettingsService::open
//*******************************
GameSettings GameSettingsService::open(PsGamePtr game) const {
    GameSettings s;
    s.game = game;
    s.internal = game->internal;

    if (!s.internal) {
        s.ini.load(game->folder + sep + GAME_INI);
        // change "/media/Games/Racing/Driver 2" to "Driver 2"
        string folderNoLast = DirEntry::removeSeparatorFromEndOfPath(game->folder);
        s.ini.entry = DirEntry::getFileNameFromPath(folderNoLast);
    } else {
        // recover ini
        s.ini.values["title"] = game->title;
        s.ini.values["publisher"] = game->publisher;
        s.ini.values["year"] = to_string(game->year);
        s.ini.values["players"] = to_string(game->players);
        s.ini.values["memcard"] = game->memcard;
    }
    fallBackToSonyCardIfSetIsGone(s);

    PcsxConfig::migrateLegacy(*game);
    s.custom = PcsxConfig::isCustom(*game);
    refreshPcsx(s);
    return s;
}

//*******************************
// GameSettingsService::refreshPcsx
//*******************************
// The values as the emulator will see them: the game's own config over pcsx.cfg (PcsxConfig::value).
void GameSettingsService::refreshPcsx(GameSettings &s) const {
    const PsGame &game = *s.game;
    auto value = [&game](const char *key) { return PcsxConfig::value(game, key); };
    // what pcsx-abnxt writes back from its own menu is hex, "0x" and all past 7 (menu.c's write_u32_value)
    auto hexValue = [&value](const char *key, int fallback) {
        string v = value(key);
        return v.empty() ? fallback : static_cast<int>(strtol(v.c_str(), nullptr, 16));
    };
    const string platform = Env::platformName();
    PcsxSettings &p = s.pcsx;
    p.highres = atoi(value("gpu_neon.enhancement_enable").c_str());
    p.noSeams = hexValue("gpu_neon.enhancement_no_seams", 1) != 0; // no line = on, the emulator's default
    p.speedhack = atoi(value("gpu_neon.enhancement_no_main").c_str());
    p.clock = strtol(value("psx_clock").c_str(), nullptr, 16);
    p.gpu = value("gpu3");
    // the emulators' frameskip setting, not a frame count: 0 Auto, 1 Off, 2..4 skip 1..3 (no line = Off, their
    // default; the shipped pcsx.cfg says 0, Auto)
    string skip = value("frameskip3");
    p.frameskip = skip.empty() ? FrameskipOff : clampTo(strtol(skip.c_str(), nullptr, 16), 0, FrameskipCount - 1);
    // pcsx-abnxt's menu shows anything past "always" as "on"; no line = on, the emulator's default
    int dither = hexValue("dithering2", 1);
    p.dither = dither < 0 || dither >= DitheringCount ? 1 : dither;
    p.scanlines = clampTo(hexValue("scanlines", 0), 0, ScanlineModes - 1); // the emulator draws past 3 as 3
    p.scanlineLevel = hexValue("scanline_level", 80);                      // 80% by default
    p.interpolation = strtol(value("spu_config.iUseInterpolation").c_str(), nullptr, 16);
    string slowBoot = value("SlowBoot");
    p.bootLogo = slowBoot.empty() ? 1 : atoi(slowBoot.c_str()); // pcsx-ab's own default is 1
    // no line = none; a scaler this platform does not offer is none, as the emulator takes it (the console)
    int smoothing = hexValue("soft_filter", 0);
    p.smoothing = listed(smoothingsFor(platform), smoothing) ? smoothing : 0;
    p.sonyHacks = atoi(value("sonyhacks").c_str()) != 0; // no line = off
    // no line = Nearest; a value the platform does not list is Nearest too, as LaunchService passes it
    int filter = hexValue("plat_target.hwfilter", 0);
    p.filter = listed(filtersFor(platform), filter) ? filter : 0;
}

//*******************************
// GameSettingsService::unlock
//*******************************
bool GameSettingsService::unlock(GameSettings &s) {
    bool ok = PcsxConfig::unlock(*s.game);
    s.custom = PcsxConfig::isCustom(*s.game);
    refreshPcsx(s);
    return ok;
}

//*******************************
// GameSettingsService::saveIni
//*******************************
void GameSettingsService::saveIni(GameSettings &s) const {
    if (!s.internal) {
        s.ini.save(s.ini.path);
    }
}

//*******************************
// GameSettingsService::replaceCfgLine
//*******************************
void GameSettingsService::replaceCfgLine(GameSettings &s, const string &property, const string &value) {
    if (s.custom) {
        return; // locked: the emulator's own config speaks for the game, pcsx.cfg waits for an unlock
    }
    ConfigFileEditor processor;
    processor.replace(s.ini.entry, cfgFolder(s), property, property + " = " + value, s.internal);
    refreshPcsx(s);
}

//*******************************
// GameSettingsService::setFavorite
//*******************************
void GameSettingsService::setFavorite(GameSettings &s, bool on) {
    s.game->favorite = on; // the record in hand says what was just written, whichever store it went to
    if (s.internal) {
        library_.internalGames().updateFavorite(s.game->gameId, s.game->favorite);
    } else {
        s.ini.values["favorite"] = on ? "1" : "0";
        saveIni(s);
    }
}

//*******************************
// GameSettingsService::setPlayUsingRa
//*******************************
void GameSettingsService::setPlayUsingRa(GameSettings &s, bool on) {
    s.game->play_using_ra = on;
    if (s.internal) {
        library_.internalGames().updatePlayUsingRA(s.game->gameId, s.game->play_using_ra);
    } else {
        s.ini.values["play_using_ra"] = on ? "true" : "false";
        saveIni(s);
    }
}

//*******************************
// GameSettingsService::setLightgun
//*******************************
void GameSettingsService::setLightgun(GameSettings &s, bool on) {
    s.game->lightgun = on;
    if (s.internal) {
        library_.internalGames().updateLightgun(s.game->gameId, on ? 1 : 0);
    } else {
        s.ini.values["lightgun"] = on ? "1" : "0";
        saveIni(s);
    }
    if (on && !s.game->play_using_ra)
        setPlayUsingRa(s, true);
}

//*******************************
// GameSettingsService::setLocked
//*******************************
void GameSettingsService::setLocked(GameSettings &s, bool on) {
    if (s.internal) {
        return;
    }
    string &automation = s.ini.values["automation"];
    if (on) {
        if (automation == "1")
            automation = "0";
    } else {
        if (automation == "0")
            automation = "1";
    }
    saveIni(s);
}

//*******************************
// GameSettingsService::setMemcard
//*******************************
void GameSettingsService::setMemcard(GameSettings &s, const string &name) {
    if (s.internal) {
        return;
    }
    s.ini.values["memcard"] = name;
    saveIni(s);
}

//*******************************
// GameSettingsService::rename
//*******************************
void GameSettingsService::rename(GameSettings &s, const string &title) {
    if (s.internal) {
        return;
    }
    s.ini.values["title"] = title;
    s.ini.values["automation"] = "0";
    saveIni(s);
}

//*******************************
// GameSettingsService::setHighres
//*******************************
void GameSettingsService::setHighres(GameSettings &s, bool on) {
    if (s.custom) {
        return; // the emulator's own config speaks for the game (PcsxConfig)
    }
    s.ini.values["highres"] = to_string(on ? 1 : 0);
    replaceCfgLine(s, "gpu_neon.enhancement_enable", s.ini.values["highres"]);
    saveIni(s);
}

//*******************************
// GameSettingsService::setNoSeams
//*******************************
void GameSettingsService::setNoSeams(GameSettings &s, bool on) {
    replaceCfgLine(s, "gpu_neon.enhancement_no_seams", to_string(on ? 1 : 0));
}

//*******************************
// GameSettingsService::setDithering
//*******************************
// pcsx-abnxt's CE_INTVAL_PV(dithering, 2): "dithering2", read in hex and written as write_u32_value does -
// 0..2 is the same either way
void GameSettingsService::setDithering(GameSettings &s, int mode) {
    replaceCfgLine(s, "dithering2", toHex(clampTo(mode, 0, DitheringCount - 1)));
}

//*******************************
// GameSettingsService::setSpeedhack
//*******************************
void GameSettingsService::setSpeedhack(GameSettings &s, bool on) {
    replaceCfgLine(s, "gpu_neon.enhancement_no_main", to_string(on ? 1 : 0));
}

//*******************************
// GameSettingsService::setScanlines
//*******************************
void GameSettingsService::setScanlines(GameSettings &s, int mode) {
    replaceCfgLine(s, "scanlines", toHex(clampTo(mode, 0, ScanlineModes - 1)));
}

//*******************************
// GameSettingsService::setScanlineLevel
//*******************************
void GameSettingsService::setScanlineLevel(GameSettings &s, int level) {
    replaceCfgLine(s, "scanline_level", toHex(clampTo(level, 0, 100)));
}

//*******************************
// GameSettingsService::setClock
//*******************************
void GameSettingsService::setClock(GameSettings &s, int clock) {
    replaceCfgLine(s, "psx_clock", toHex(clampTo(clock, 0, 100)));
}

//*******************************
// GameSettingsService::setFrameskip
//*******************************
void GameSettingsService::setFrameskip(GameSettings &s, int frames) {
    replaceCfgLine(s, "frameskip3", toHex(clampTo(frames, 0, FrameskipCount - 1)));
}

//*******************************
// GameSettingsService::setInterpolation
//*******************************
void GameSettingsService::setInterpolation(GameSettings &s, int mode) {
    replaceCfgLine(s, "spu_config.iUseInterpolation", toHex(clampTo(mode, 0, 3)));
}

//*******************************
// GameSettingsService::setBootLogo
//*******************************
void GameSettingsService::setBootLogo(GameSettings &s, bool on) {
    replaceCfgLine(s, "SlowBoot", to_string(on ? 1 : 0));
}

//*******************************
// GameSettingsService::setSmoothing
//*******************************
void GameSettingsService::setSmoothing(GameSettings &s, int mode) {
    replaceCfgLine(s, "soft_filter", toHex(clampTo(mode, 0, SmoothingCount - 1)));
}

//*******************************
// GameSettingsService::setSonyHacks
//*******************************
void GameSettingsService::setSonyHacks(GameSettings &s, bool on) {
    replaceCfgLine(s, "sonyhacks", to_string(on ? 1 : 0));
}

//*******************************
// GameSettingsService::setFilter
//*******************************
void GameSettingsService::setFilter(GameSettings &s, int mode) {
    replaceCfgLine(s, "plat_target.hwfilter", toHex(clampTo(mode, 0, FilterCount - 1)));
}

//*******************************
// GameSettingsService::setGpuPlugin
//*******************************
void GameSettingsService::setGpuPlugin(GameSettings &s, const string &plugin) {
    if (s.internal) {
        return;
    }
    replaceCfgLine(s, "Gpu3", plugin);
}
