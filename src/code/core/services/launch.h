//
// LaunchService: starting a game - PCSX, RetroArch or a launchable App - and everything around it.
//
#pragma once

#include <utility>
#include <ableem/engine/config_file_editor.h>
#include "../model/ps_game.h"
#include "../model/session.h"
#include "app_manifest.h"
#include "config.h"
#include "memcard.h"
#include "package_service.h"
#include "process_runner.h"
#include "ra_options.h"
#include "resume_point.h"

#include <ableem/engine/game_library.h>

#include <string>
#include <vector>

//******************
// LaunchService
//******************
// Was App::launchGame plus the three EmuInterceptors (PcsxInterceptor, RetroArchInterceptor,
// LaunchInterceptor) - the argv for rc/launch.sh and rc/launch_rb.sh, the memory-card and resume-point
// work around a run, the RetroArch config transfer. What was an #ifdef AB_DEBUG_HOST in each interceptor is
// now which ProcessRunner the composition root passes in, which is also what makes the argv assertable.
//
// Two ways to start an emulator, the platform ini's launch_mode (Env::directLaunch()):
//   script  rc/launch.sh / rc/launch_rb.sh with the arguments those scripts read - the console, the Pi, the
//           PC stick, the dev host
//   direct  the emulator itself, no shell: pcsx-ab from Env::pcsxDir() with its -dotdir/-biosdir options,
//           RetroArch from the first Env::retroArchBinaries() that exists, both full screen - the Windows
//           product, where there is no shell to run a script in. No selection script is written either
//           (writeSelectionScript() is a no-op there).
// planPcsx()/planRetroArch() build the LaunchPlan for either; launch() runs it.
//
// Owned by App (App::launcher()).
struct OutputMode;

class LaunchService {
public:
    LaunchService(Config &config, Session &session, ableem::GameLibrary &library, MemcardService &memcards,
                  ResumePointService &resumePoints, ProcessRunner &runner)
        : config_(config), session_(session), library_(library), memcards_(memcards), resumePoints_(resumePoints),
          runner_(runner) {}

    // where writeSelectionScript() writes: <runtime>/autobleem_cfg.sh - RAM, what selection.sh (console) or
    // autobleem-session.sh (a Pi, the PC stick) sources after the launcher has left (was <rc>/, on the
    // stick, until the quiet-stick plan; before that config.ini's Cfg= key)
    static std::string selectionScriptFile();
    // AB_SELECTION/AB_THEME/AB_PCSX for the script that runs after the launcher: written when the launcher
    // leaves (RetroArch, the update, the power off). No file, or any other selection, is a crash to the
    // scripts - which is why nothing is written around a game any more.
    void writeSelectionScript();

    // the per-game options of RetroArch games (the game editor's rows); none set = every game as it always was
    void setRaOptions(const RaOptionsService *options) { raOptions_ = options; }

    // The launch itself, start to finish: the emulator is chosen from the game and the mode, the game's
    // memory cards go in and its resume point is prepared, the launcher script runs and is waited for, the
    // cards come back out. `game` is the record the launcher handed over; PCSX's path normalises its
    // ssFolder in place, as it always has.
    // `package`: the game data the player picked for an App with Uses= (docs/packages.md 5): its AB_PKG_* variables and
    // {package} placeholders. A Packages-row entry (PsGame::package) is never launched - it is refused here.
    void launch(PsGamePtr &game, EmuMode mode, int resumePoint, const PackageEntry *package = nullptr);

    // where the launcher scripts are
    static std::string pcsxLauncherScript();      // rc/launch.sh
    static std::string retroArchLauncherScript(); // rc/launch_rb.sh
    // the direct-launch programs: the PS1 emulator config.ini's "emulator" names - <pcsx dir>/pcsx-ab(.exe)
    // for pcsx-ab, <pcsxnxt dir>/pcsx-ab(.exe) for pcsx-abnxt - falling back to the other when that folder
    // has no binary, as launch.sh does ("" when neither has one: RetroArch's PS1 core is next); and the
    // first RetroArch binary that exists ("" when none does)
    std::string pcsxExecutable() const;
    static std::string retroArchExecutable();
    // <dir>/pcsx-ab(.exe)
    static std::string pcsxBinaryIn(const std::string &dir);
    // a direct launch of the old pcsx-ab, which knows only the run directory launch.sh lays out (.pcsx,
    // bios and plugins as links next to the working directory): <System>/runpcsx, made with directory
    // links before the run and cleared after. pcsx-abnxt takes -dotdir/-biosdir instead and needs none.
    static std::string pcsxRunDir();
    // the classic pcsx-ab's -filter for a filter mode (pcsx-abnxt's numbering, PcsxSettings::filter): its
    // numbering is the other way round - 0 is bilinear, 1 nearest - and it has nothing else, so Linear is
    // its bilinear and every other filter (Sharp and on) its nearest
    static std::string pcsxAbFilter(int mode);
    // the game's filter mode from its pcsx.cfg (the game folder's; the save-state folder's for an internal
    // game): 0 Nearest, 1 Linear, 2 Sharp, 3 Sharp (simple), 4 Quilez, 5 CRT (fast), 6 CRT-Pi; 0 when the
    // cfg has no line or a value past those
    static int filterModeFor(const PsGame &game);

    // what a launch runs, for either mode. The pcsx plan: the save-state folder, the disc image, the
    // language id, the resume slot (-1 = none), config.ini's aspect flag as "0"/"1" and the game's filter
    // (its pcsx.cfg plat_target.hwfilter) as "0".."6" - pcsx-abnxt's -filter as is.
    // launch.sh converts it for the classic pcsx-ab itself (it may fall back to that emulator); a direct
    // launch of pcsx-ab gets pcsxAbFilter()'s value.
    // The RetroArch plan: the file and the core - "NEON"/"PEOPS" for one of our PS1 games (the platform's
    // PS1 core in direct mode), else a core path.
    LaunchPlan planPcsx(const PsGame &game, const std::string &discImage, const std::string &lang, int resumePoint,
                        const std::string &aspect, const std::string &filter) const;
    static LaunchPlan planRetroArch(const std::string &file, const std::string &core);
    static LaunchPlan planApp(const PsGame &game, const PackageEntry *package = nullptr);
    // the generic script a multi-platform App without a run.sh of its own is started through (rc/app_run.sh)
    static std::string appRunScript();
    // what a multi-platform App is started with: AB_ROOT, AB_APP_DIR/EXEC/ARGS/LIB/KEY, AB_PLATFORM,
    // AB_PLATFORM_KEYS (space separated), AB_APP_VIRTUAL_PAD (1/0, the ini's VirtualPad=), AB_APP_PAD_MODE (the
    // player's choice from AppSettings, else the ini's PadMode=, else empty), AB_APP_DPAD2ANALOG and
    // AB_APP_ANALOG2DPAD ("1"/"0": the choice, else the ini's Dpad2Analog=/Analog2Dpad=, else empty), then the ini's
    // Env=
    // With a `package` (the pick of an App with Uses=, docs/packages.md 5.2, 5.3): AB_PKG_DIR/FILE/KIND/TITLE/ID/GAME,
    // AB_PKG_STARTS (a game with Start programs), AB_PKG_SET_<NAME> and AB_PKG_MAPPER (a dos-game's), and the
    // {package} placeholders replaced in AB_APP_ARGS and in the ini's Env= values. Without one none of it is set and
    // nothing is replaced.
    static std::vector<std::pair<std::string, std::string>> appEnvironment(const AppManifest &manifest,
                                                                           const PackageEntry *package = nullptr);
    // `text` with {package} (the main file), {package_dir}, {package_kind}, {package_title}, {package_id} and
    // {package_game} replaced; any other {name} stays as it is
    static std::string expandPackage(const std::string &text, const PackageEntry &package);
    // an Args= line with the placeholders replaced per argument, the quoting of the line kept: an argument that had
    // a placeholder and now holds a blank (or a shell character) is wrapped in double quotes - what rc/app_run.sh's
    // eval and AppManifest::splitArgs both honour. The arguments with no placeholder are copied as they were.
    static std::string expandPackageArgs(const std::string &args, const PackageEntry &package);
    // RetroArch with nothing loaded - its own menu, full screen (what the system menu's RetroArch item
    // means in direct mode; the console and the Pi leave the launcher and their rc/retroarch.sh does it)
    static LaunchPlan planRetroArchMenu();
    void launchRetroArchMenu();

private:
    enum class Path { Pcsx, RetroArch, App };
    static Path pathFor(const PsGame &game, EmuMode mode);

    // copy a cfg file forcing LF line endings (the emulator reads it in text mode; CRLF breaks it)
    static void copyCfgAsLf(const std::string &src, const std::string &dst);
    // the "last played" time, for the three launch paths: skipped when Env::clockIsSet() says the console's
    // clock has never been set (the AutoBleem kernel sets it once a network is up) - a launch before then
    // must not overwrite a valid earlier time with the epoch. The play-history ranking
    // (GameCatalogService::recordGamePlayed) is unaffected and runs regardless.
    void recordLastPlayed(PsGame &game);

    // the disc image handed to an emulator: <folder>/<base>, with .cue appended unless it is a .pbp or .chd
    static std::string discImageFor(const PsGame &game);
    // the image's base name with a .pbp extension dropped - what RetroArch names its saves after
    static std::string raBaseNameFor(const PsGame &game);

    // --- PCSX ---
    // env: what the emulator is handed on top (AB_EXIT_DIR, AB_MEMCARD_DIR, AB_LOAD_STATE)
    void launchPcsx(PsGame &game, int resumePoint, const LaunchPlan::Env &env);
    // (a config saved in the emulator is the game's own pcsx.custom.cfg, which the emulators write and
    // read themselves - PcsxConfig; the autobleem.cfg this used to copy back after the run is gone)

    // --- RetroArch ---
    // resumePoint: the slot to continue from, -1 = from the beginning (a game with save-state slots only)
    void launchRetroArch(PsGame &game, int resumePoint);
    // this launch's save-state settings for RetroArch (prepareRaAppend writes them): the game's core can save
    // states (active), and the slot to continue from was put in place (load)
    struct RaStates {
        bool active = false;
        bool save = true; // RetroArch writes the game's state when it ends (the game's Resume option is not "never")
        bool load = false;
    };
    RaStates raStates_;
    RaGameOptions
        raGameOptions_; // this launch's per-game options (EMU-25), the default for anything but a RetroArch game
    // the game's card1.mcd goes to RetroArch's saves dir as <base>.srm for the run and comes back after
    void raMemcardIn(PsGame &game);
    void raMemcardOut(PsGame &game);
    // What RetroArch is started with on top of its own retroarch.cfg - --appendconfig <runtime>/ra-append.cfg,
    // RAM (docs/quiet-stick-plan.md): config.ini's rapersist as config_save_on_exit (Options -> "Persist
    // RetroArch config") and, for a game with config.ini raconfig=true, the game's pcsx.cfg settings - the
    // core options in a copy of RetroArch's in RAM, named by core_options_path. retroarch.cfg itself is not
    // written. A RetroArch that saves its config (on exit, or "Save Current Configuration") writes the
    // appended values into it too, so afterwards every one of them that is still what we appended is put
    // back to what the file had before (restoreAppended) - what the player changed in RetroArch stays.
    // game == nullptr: RetroArch's own menu, config_save_on_exit alone.
    void prepareRaAppend(PsGame *game);
    void restoreAppended();
    // the CRT 4:3 mode (720x480): the aspect, shader, refresh and message-margin lines over `lines`
    void raCrtSettings(ableem::ConfigFileEditor::CfgLines &lines);
    // every mode: the menu's pixel aspect, safe margin and scale for the tube, a VGA 4:3 mode or HD over `lines`
    void raMenuSettings(const OutputMode &mode, ableem::ConfigFileEditor::CfgLines &lines);
    // the game's pcsx.cfg settings as retroarch.cfg lines and core-option lines
    void raSettingsFor(PsGame &game, ableem::ConfigFileEditor::CfgLines &raConfig,
                       ableem::ConfigFileEditor::CfgLines &coreOptions);
    // a retroarch.cfg.bak / core-options .bak a launcher before the quiet-stick plan left behind (it
    // backed both up for every run and was killed before it put them back): put back, once
    static void restoreLegacyRaBackup();
    ableem::ConfigFileEditor::CfgLines raAppended_;               // what the last prepareRaAppend appended
    std::vector<std::pair<std::string, std::string>> raOriginal_; // and what retroarch.cfg had ("" line: none)

public:
    // The PS1 emulator a launch would run, and what it takes from us through the environment: the words of
    // the abfeatures file next to its binary (exitdir, memcarddir, loadstate - pcsx-abnxt's
    // frontend/ab/ab_config.h). An emulator without the file takes none of them and runs as it always did.
    std::string pcsxDirForLaunch() const;
    std::vector<std::string> pcsxFeatures() const;
    // C11: whether the PS1 emulator this launch would use declares "padorder" in its abfeatures file - the
    // launcher's Options row and its pad-assignment notice use this to decide whether to say the swap
    // applies, instead of silently doing nothing when an older/other emulator does not understand it.
    bool pcsxSupportsPadOrder() const;
    // <runtime>/exit: where an emulator with "exitdir" leaves the run's resume point (RAM)
    static std::string pcsxExitDir();

    static std::string raSavesDir();
    static std::string raConfigFile();      // retroarch.cfg
    static std::string raCoreOptionsFile(); // config/retroarch-core-options.cfg
    static std::string raAppendFile();      // <runtime>/ra-append.cfg
    // the scanlines overlay's cfg: <resources>/overlay/scanlines.cfg when it is there, else ":/overlay/scanlines.cfg"
    static std::string raScanlinesOverlay();
    static std::string raRuntimeCoreOptionsFile(); // <runtime>/ra-core-options.cfg

private:
    // --- Apps ---
    void launchApp(PsGame &game, const PackageEntry *package);

    Config &config_;
    Session &session_;
    ableem::GameLibrary &library_;
    MemcardService &memcards_;
    ResumePointService &resumePoints_;
    ProcessRunner &runner_;
    const RaOptionsService *raOptions_ = nullptr;
};
