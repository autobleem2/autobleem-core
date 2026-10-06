//
// the launcher/main-loop state that used to live directly on the Gui singleton.
//
#pragma once

#include <memory>
#include <string>
#include "game_set.h"
#include "ps_game.h"

struct PackageEntry; // core/services/package_service.h

//******************
// menu / launcher selection constants
//******************
// what AutoBleem::run()'s loop does when the launcher closes - written into autobleem_cfg.sh's AB_SELECTION
// for rc/selection.sh to read after the process exits. The numeric values are part of that contract and
// must not change. IDLE is the resting default and also what a plain "close the app" leaves behind -
// selection.sh's own default (start_autobleem, i.e. come straight back here) is the right thing for that.
// UPDATE (2026-09-20) is the online update: the launcher has downloaded the new package(s) into
// System/Updates and leaves so the Pi's session loop can run autobleem-update over them. POWEROFF
// (2026-09-22) is the console's power off: the launcher leaves and rc/selection.sh unmounts the stick and
// suspends the console (what Sony's own power off does, with the stick unmounted so it can be pulled) -
// the power button wakes it and the launcher is started over. Nothing but the console uses it. DISPLAY
// (2026-09-28) is Options -> Display: a new mode to try (Session::pendingOutputMode). On the console the
// launcher leaves and rc/boot.sh restarts Weston in it before starting the launcher again (no reboot);
// elsewhere AutoBleem::run() remakes the window in-process. Either way it then asks to keep the mode.
enum MenuOption {
    MENU_OPTION_IDLE = 1,
    MENU_OPTION_RETRO = 4,
    MENU_OPTION_START = 5,
    MENU_OPTION_UPDATE = 6,
    MENU_OPTION_POWEROFF = 7,
    MENU_OPTION_DISPLAY = 8
};

// which emulator/launcher path to use for the game about to start
enum class EmuMode { Pcsx, RetroArch, Launcher };

//******************
// Session
//******************
// Everything that describes "where we are" across one run of the app: what App::run()'s outer loop should do
// next, what game (if any) was asked to start, and where the EvolutionUI carousel was so Start can bring it
// back. Owned by App; reached as `app.session()` from screens (via the GuiScreen shim) or `App::get().session()`
// from the few places that are not screens (UtilTime).
struct Session {
    MenuOption menuOption = MENU_OPTION_IDLE;

    // what GuiLauncher asked App::run()'s loop to do
    bool startingGame = false;
    PsGamePtr runningGame;
    EmuMode emuMode = EmuMode::Pcsx;
    int resumePoint = -1;
    // APPS-12: the game data an engine App (Uses=) was picked to run with; null when there is none to pick
    std::shared_ptr<const PackageEntry> package;
    bool resumingGui = false; // true right after a game exits: skip the classic menu and reopen the carousel

    // C11: Options -> "Swap Player 1 / Player 2" was on for a PS1 launch whose emulator's abfeatures had no
    // "padorder" - LaunchService::launch() sets this instead of silently sending nothing, since the screen
    // that asked for the launch is already gone by the time the emulator returns. GuiLauncher shows one
    // notification line for it the next time it comes up (right where it seeds the pad-assignment notice)
    // and clears the flag - so it fires once per such launch, not on every later re-show.
    bool padOrderUnsupportedNotice = false;

    // Options -> Display: the OutputMode token to try (MENU_OPTION_DISPLAY); "" when none
    std::string pendingOutputMode;

    // where the EvolutionUI carousel was, so Start brings it back to the same place.
    // GuiLauncher holds a copy of this and writes it back through GuiLauncher::rememberSelection().
    GameSetSelection launcher;
};
