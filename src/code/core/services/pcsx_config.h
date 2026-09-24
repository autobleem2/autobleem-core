//
// PcsxConfig: which file speaks for a game's PCSX configuration.
//
#pragma once

#include "../model/ps_game.h"

#include <string>

//******************
// PcsxConfig
//******************
// A game's configuration for the two PS1 emulators has one source at a time (2026-09-24, the owner's
// design; the emulators' side is pcsx-abnxt's frontend/ab/ab_config.h and pcsx-ab's make_cfg_fname):
//
//   pcsx.cfg          AutoBleem's: the game folder's (a built-in game's is in its !SaveStates folder), the
//                     one the game editor writes and launch.sh copies next to the save states at every start
//   pcsx.custom.cfg   the game's own, in its !SaveStates folder: every save in the emulators' menus
//                     writes it. While it exists the game editor shows the emulator settings locked, with
//                     its values; unlock() deletes it and pcsx.cfg is the game's again, as it was
//
// The emulators load pcsx.cfg, then the custom file over it, so a key the custom file lacks keeps
// AutoBleem's value - value() reads a key the same way, for what the launcher passes on the command line
// (the filter) and hands RetroArch.
//
// migrateLegacy() folds in what older builds left: autobleem.cfg (the emulators' old "Save AutoBleem
// config", which the launcher copied over pcsx.cfg after the run) and the per-disc cfg/<label>-<id>.cfg
// (upstream's "Save cfg for loaded game") both become the custom config, the newest of them winning, with
// the BIOS put back to "SET_BY_PCSX" as the old copy-back did. The cfg/ folder is left empty.
class PcsxConfig {
public:
    static const char *const CustomName; // "pcsx.custom.cfg"

    // AutoBleem's pcsx.cfg: the game folder's, or the !SaveStates folder's for a built-in game
    static std::string launcherFile(const PsGame &game);
    // the game's own, <ssFolder>/pcsx.custom.cfg
    static std::string customFile(const PsGame &game);
    static bool isCustom(const PsGame &game);

    // a key's value as the emulator will see it: the custom file's line when it has one, else pcsx.cfg's;
    // "" when neither has it
    static std::string value(const PsGame &game, const std::string &key);

    // deletes the custom config (what the game editor's Unlock does); true when there is none afterwards
    static bool unlock(const PsGame &game);

    // what older builds left, as the custom config (see above); safe to call at every open and launch
    static void migrateLegacy(const PsGame &game);
};
