//
// RaOptionsService: the per-game options of a RetroArch game (EMU-25) and what they mean for RetroArch.
//
#pragma once

#include "../model/ps_game.h"

#include <ableem/engine/config_file_editor.h>

#include <map>
#include <string>

//******************
// RaGameOptions
//******************
// What the game editor of a RetroArch game offers besides the core. Every row starts at its first value, which
// means "as it is today": no line is written for it, so a game nobody edited launches exactly as before.
struct RaGameOptions {
    enum Aspect { AspectDefault, AspectCore, Aspect43, AspectFull, AspectPixel, AspectCount };
    enum Tri { TriDefault, TriOn, TriOff, TriCount }; // a switch: as today / forced on / forced off
    enum Scanlines { ScanDefault, ScanOff, ScanLight, ScanStrong, ScanCount };
    enum Resume { ResumeAsk, ResumeLast, ResumeNever, ResumeCount };

    int aspect = AspectDefault;
    int integerScaling = TriDefault;
    int smoothing = TriDefault;
    int scanlines = ScanDefault;
    int showFps = TriDefault;
    int analogAsDpad = TriDefault;
    // ask: the resume icon picks a slot, Play starts from the beginning. last: Play continues from the newest slot
    // there is. never: the game's state is not written when it ends (and so no slot is offered)
    int resume = ResumeAsk;

    bool isDefault() const { return *this == RaGameOptions(); }
    bool operator==(const RaGameOptions &o) const;

    // "0,2,0,..." - seven numbers, in the order above; anything else (or a number out of its row's range) is
    // the default for that row
    std::string encode() const;
    static RaGameOptions decode(const std::string &text);
};

//******************
// RaOptionsService
//******************
// Remembered in System/ra-game-options.txt, one line per edited game: "<options>\t<image path>" (a RetroArch game
// has no ini of its own - the same way LightgunService keeps its list). A game whose options are all the default
// has no line. Owned by App (App::raOptions()); LaunchService reads it when it starts RetroArch.
class RaOptionsService {
public:
    RaOptionsService();

    RaGameOptions get(const PsGame &game) const;
    void set(const PsGame &game, const RaGameOptions &options); // saves the file when it changed

    // The retroarch.cfg lines the options mean, over `lines` (a key already there is replaced, else added):
    // only what is not the default. analog_dpad: player 1's left stick as the D-pad.
    static void apply(const RaGameOptions &options, ableem::ConfigFileEditor::CfgLines &lines);
    // the value of aspect_ratio_index for an Aspect row (RetroArch 1.22's gfx/video_defines.h), "" for the default
    static std::string aspectIndexFor(int aspect);

    static std::string optionsFile(); // System/ra-game-options.txt

private:
    void load();
    void save() const;

    std::map<std::string, RaGameOptions> byPath_;
};
