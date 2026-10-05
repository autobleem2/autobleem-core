//
// GameSettingsService: what the game editor edits - a game's Game.ini flags and its pcsx.cfg values.
//
#pragma once

#include "../model/ps_game.h"
#include "resume_point.h"

#include <ableem/engine/game_library.h>
#include <ableem/engine/ini_file.h>

#include <string>
#include <vector>

//******************
// PcsxSettings
//******************
// The per-game emulator values the editor shows, as read back from the game's pcsx.cfg. They are ints
// because the editor steps them with the d-pad; how each one is encoded in the file is the service's
// business (see the setters below). The Display rows are pcsx-abnxt's in-game menu's Picture rows, with
// its keys and values.
struct PcsxSettings {
    int highres = 0; // pcsx.cfg gpu_neon.enhancement_enable, the Resolution row: 0 1x, 1 2x
    int noSeams = 1; // pcsx.cfg gpu_neon.enhancement_no_seams, Remove seams (with 2x only); no line = on
    int speedhack = 0;
    int clock = 0;
    int frameskip = 0; // pcsx.cfg frameskip3, the emulators' setting: 0 Auto, 1 Off, 2..4 skip 1..3 frames
    int dither = 1;    // pcsx.cfg dithering2, pcsx-abnxt's Dithering: 0 off, 1 where the game asks (no line), 2 always
    int scanlines = 0; // pcsx.cfg scanlines: 0 off, 1..3 how thick (the classic pcsx-ab: anything but 0 is on)
    int scanlineLevel = 0;
    int interpolation = 0;
    int bootLogo = 1;  // pcsx.cfg SlowBoot: the BIOS boot logo shown before the game; no line = shown
    int smoothing = 0; // pcsx.cfg soft_filter, pcsx-abnxt's Smoothing: 0 none, 1 scale2x, 2 eagle2x, 3 hq2x, 4 hq3x
    int sonyHacks = 0; // pcsx.cfg sonyhacks, pcsx-abnxt only: Sony's per-title overrides for the disc's serial
    // pcsx.cfg plat_target.hwfilter, pcsx-abnxt's own numbering (ab_filter_names): 0 Nearest, 1 Linear, 2 Sharp,
    // 3 Sharp (simple), 4 Quilez, 5 CRT (fast), 6 CRT-Pi
    int filter = 0;
    std::string gpu;
};

//******************
// GameSettings
//******************
// Everything the editor holds for one game while it is open. For a USB game `ini` is the game's Game.ini,
// loaded from its folder, with `ini.entry` set to the folder's name (the key ConfigFileEditor uses to find
// the !SaveStates copies). For an internal game there is no Game.ini: `ini` is filled from the database
// record so the screen can show the same fields, its `path` stays empty and it is never saved - the flags
// go to internal.db instead.
struct GameSettings {
    PsGamePtr game;
    bool internal = false;
    // the game has its own config, saved in an emulator's menu (PcsxConfig): `pcsx` shows its values and
    // the pcsx.cfg setters below do nothing until unlock()
    bool custom = false;
    ableem::IniFile ini;
    PcsxSettings pcsx;
    int resume = ResumePointService::Ask; // the Resume row (ResumePointService::Mode)
};

//******************
// GameSettingsService
//******************
// GuiEditor used to do all of this inline, over an IniFile member that both of its callers seeded by hand.
// It is here so it can be tested against a temp tree and so the editor is only a screen.
//
// Owned by App (App::gameSettings()).
class GameSettingsService {
public:
    explicit GameSettingsService(ableem::GameLibrary &library) : library_(library) {}

    // Loads the game's Game.ini (or, for an internal game, fills one in from the record) and reads its
    // pcsx.cfg - or its own config over it, when it has one (`custom`; what older builds left is folded in
    // first, PcsxConfig::migrateLegacy). A memory-card set the ini names but which no longer exists is
    // shown as the console's own card - in memory only; the file is not touched until something else is
    // saved.
    GameSettings open(PsGamePtr game) const;

    // re-reads the pcsx values as the emulator will see them; the setters below do this after writing
    void refreshPcsx(GameSettings &s) const;

    // Deletes the game's own config, so pcsx.cfg - untouched while it existed - is the game's again, and
    // re-reads the values. False if the file could not be removed (`custom` then stays true).
    bool unlock(GameSettings &s);

    // --- Game.ini for a USB game, internal.db for an internal one ---
    void setFavorite(GameSettings &s, bool on);
    void setPlayUsingRa(GameSettings &s, bool on);
    // A light-gun game plays in RetroArch's pcsx_rearmed (guncon), so switching it on also switches Play
    // using RA on; off leaves Play using RA as it is. The editor keeps the Play using RA row locked while
    // the flag is on. RetroArch games are flagged elsewhere (LightgunService) - they have no Game.ini.
    void setLightgun(GameSettings &s, bool on);

    // --- the Resume row (EMU-26): kept in the game's !SaveStates folder (resume.txt: "last" or "never"; ask, the
    // default, has no file), so it works the same for a USB and an internal game and the scanner's Game.ini rewrites
    // cannot lose it. What each value means: ResumePointService::Mode ---
    static int resumeModeOf(const PsGame &game); // what the launcher asks at Play
    void setResume(GameSettings &s, int mode);   // clamped to 0..ModeCount-1

    // --- Game.ini only; a no-op for an internal game ---
    // "Locked" is Automation=0: the user edited the ini, the scanner must not rewrite it. Only flips the
    // flag from its opposite value - an ini with no Automation key at all is left as it is (the scanner
    // always writes one, so this is not reached in practice, but it is what the editor did).
    void setLocked(GameSettings &s, bool on);
    // The ini's Memcard value only. MemcardService::setCardForGame also writes regional.db; the editor
    // never did, and a launch reads the ini, so nothing depends on the column being current.
    void setMemcard(GameSettings &s, const std::string &name);
    // A rename also unlocks the ini (Automation=0) so the scanner keeps the new title. Internal games are
    // renamed in the database by the caller, after the editor closes - see GuiLauncher's use of lastName.
    void rename(GameSettings &s, const std::string &title);

    // --- pcsx.cfg: the game folder's copy plus the one under !SaveStates (ConfigFileEditor::replace) ---
    // Each one rewrites the line, then re-reads all the values, so what the caller sees is what the file
    // says (nothing, if the game has no pcsx.cfg). The 0/1 flags are written in decimal, the levels in
    // hex - that is what PCSX reads. Levels are clamped to their ranges here. All of them do nothing while
    // the game has its own config (`custom`): the emulator's file speaks for it until unlock().
    // gpu_neon.enhancement_enable, the Resolution row (1x/2x): the built-in NEON GPU only (neonGpuFor)
    void setHighres(GameSettings &s, bool on); // also remembered in the Game.ini as Highres
    // gpu_neon.enhancement_no_seams: no 1-pixel gaps between the parts of a picture at 2x
    void setNoSeams(GameSettings &s, bool on);
    // dithering2 (pcsx-abnxt's pl_rearmed_cbs.dithering, upstream's versioned key): 0 off, 1 where the game
    // asks, 2 always; clamped. The classic pcsx-ab has its own (gpu_peops.iUseDither) and ignores it;
    // RetroArch gets it as pcsx_rearmed_dithering (LaunchService)
    void setDithering(GameSettings &s, int mode);
    void setSpeedhack(GameSettings &s, bool on);
    // scanlines: 0 off, 1..3 how thick (pcsx-abnxt); clamped. A 1 from before is the thinnest
    void setScanlines(GameSettings &s, int mode);
    void setScanlineLevel(GameSettings &s, int level); // 0..100
    void setClock(GameSettings &s, int clock);         // 0..100
    void setFrameskip(GameSettings &s, int frames);    // 0..FrameskipCount-1 (PcsxSettings::frameskip); clamped
    void setInterpolation(GameSettings &s, int mode);  // 0..3
    // SlowBoot: off skips the BIOS shell (a homebrew's custom logo can crash pcsx-ab's boot); RetroArch
    // gets it as pcsx_rearmed_show_bios_bootlogo (LaunchService)
    void setBootLogo(GameSettings &s, bool on);
    // soft_filter: pcsx-abnxt's software scaler on the PSX frame (its menu's "Smoothing"); the values a
    // platform offers are smoothingsFor(). The editor greys the row with the classic pcsx-ab selected
    void setSmoothing(GameSettings &s, int mode); // 0..SmoothingCount-1, see SmoothingNames
    // sonyhacks: pcsx-abnxt applies Sony's per-title configuration overrides (the ones the console's
    // emulator had, by the disc's serial) over the cfg; a lever for a game that misbehaves, off by default
    void setSonyHacks(GameSettings &s, bool on);
    // plat_target.hwfilter: how the picture is scaled to the screen - the key pcsx-abnxt saves from its own
    // menu, so a change made in the emulator shows here and the next launch keeps it. LaunchService passes
    // it as -filter; the classic pcsx-ab knows only nearest and bilinear, and everything but Linear is
    // nearest there (LaunchService::pcsxAbFilter). The values a platform offers are filtersFor().
    void setFilter(GameSettings &s, int mode);                     // 0..FilterCount-1 (PcsxSettings::filter); clamped
    void setGpuPlugin(GameSettings &s, const std::string &plugin); // USB only: "builtin_gpu" or "gpu_peops.so"

    // --- what a platform offers (Env::platformName(): psc, rpi, pcusb, win, pc) ---
    // The Filter row's values, in order: pcsx-abnxt's ab_filter_names, all seven on every target today (its
    // GL pipeline has the same passes everywhere; on the console at 1080p CRT-Pi stays, the emulator's help
    // says it is too heavy there)
    static std::vector<int> filtersFor(const std::string &platform);
    // The Smoothing row's values: None/Scale2x/Eagle2x on the console, HQ2x/HQ3x (CPU scalers, 30 fps on
    // the console) too everywhere else - pcsx-abnxt's men_ab_smooth_psc / men_ab_smooth
    static std::vector<int> smoothingsFor(const std::string &platform);
    // whether the built-in NEON GPU is there, for the Resolution and Remove seams rows: pcsx-abnxt builds it on
    // every target (C SIMD off ARM); the classic pcsx-ab only on the ARM ones
    static bool neonGpuFor(const std::string &platform, bool nxtEmulator);
    // the value `step` places from `current` in `values`, held at the ends; a current value not in the list
    // moves to the first
    static int stepIn(const std::vector<int> &values, int current, int step);

    static const char *const BuiltinGpu;
    static const char *const PeopsGpu;
    static const int SmoothingCount = 5;
    static const char *const SmoothingNames[SmoothingCount]; // "None", "Scale2x", "Eagle2x", "HQ2x", "HQ3x"
    static const int FrameskipCount = 5; // the emulators' men_frameskip: Auto, Off, 1, 2, 3
    static const int FrameskipOff = 1;
    static const int FilterCount = 7;                      // pcsx-abnxt's AB_FILTER_COUNT
    static const int DitheringCount = 3;
    static const int ScanlineModes = 4; // off, 1, 2, 3

private:
    // where the pcsx.cfg is: the game's folder, or the !SaveStates folder for an internal game
    static std::string cfgFolder(const GameSettings &s);
    static void fallBackToSonyCardIfSetIsGone(GameSettings &s);
    void saveIni(GameSettings &s) const;
    void replaceCfgLine(GameSettings &s, const std::string &property, const std::string &value);

    ableem::GameLibrary &library_;
};
