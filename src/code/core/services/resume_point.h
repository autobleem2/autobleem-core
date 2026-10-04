//
// ResumePointService: the save-state slots PCSX leaves in a game's !SaveStates folder.
//
#pragma once

#include "../model/ps_game.h"

#include <ctime>
#include <string>

//******************
// ResumePointService
//******************
// When PCSX exits it leaves a save state and a screenshot in the game's save-state folder. AutoBleem keeps
// up to SlotCount of those per game as numbered "resume points", so the player can pick one to continue
// from. The files involved, all under game.ssFolder:
//
//   filename.txt            written by PCSX on exit; its second line is the base name of the state
//   filename.txt.res        AutoBleem's kept copy of that
//   filename.<n>.txt.res    the same, per slot
//   sstates/<name>.000      the state PCSX just wrote
//   sstates/<name>.00<n>.res  the state kept for slot n
//   screenshots/<name>.png  the screenshot PCSX just wrote
//   screenshots/<name>.png.res      the picture for slot 0
//   screenshots/<name>.<n>.png.res  the picture for slot n
//   lastcdimg.txt / lastcdimg.<n>.txt   which disc image the slot was playing
//
// An App has none of this, so every call is a no-op for one. A game played through RetroArch (a playlist entry)
// has the same four slots, kept in the same layout under its own folder, <RetroArch dir>/ab-states/<core>/<game>/
// (a separate set per core - a state is bound to the core that wrote it): RetroArch itself writes the run's
// <game>.state.auto (+ .png) in raStatesDir() when the launcher tells it to (LaunchService::raStateSettings), and
// saveAfterLaunch() copies that into a slot. Resuming copies the slot back as .state.auto and
// `savestate_auto_load` says to load it (prepareRaLaunch). Only a core whose .info does not say
// `savestate = "false"` takes part (raSupportsStates).
//
// An emulator that takes $AB_EXIT_DIR (abfeatures: exitdir - docs/quiet-stick-plan.md) writes the run's
// four files - filename.txt, lastcdimg.txt, sstates/<name>.000, screenshots/<name>.png - there instead, in
// RAM: setExitDir() says where, and every "what the run just wrote" below looks there first. Only what the
// player keeps is then copied to the stick.
//
// Owned by App (App::resumePoints()).
class ResumePointService {
public:
    static const int SlotCount = 4;

    bool slotIsActive(const PsGame &game, int slot) const;
    std::string pictureForSlot(const PsGame &game, int slot) const;
    // when the slot's kept state file was written (its mtime), 0 when the slot has none (UIREV-37: the resume-slot
    // screen's date)
    time_t timeForSlot(const PsGame &game, int slot) const;
    // the slot kept last (the newest state file; the lower number when two are alike), -1 when the game has none
    int newestSlot(const PsGame &game) const;
    // the picture for whichever slot the game last stopped in, "" when there is none
    std::string lastPicture(const PsGame &game) const;

    void storePictureForSlot(const PsGame &game, int slot);
    void removeSlot(const PsGame &game, int slot);

    // true when PCSX wrote its filename.txt, i.e. the game was quit rather than killed
    bool exitedCleanly(const PsGame &game) const;

    // Around a PCSX launch: clear out what the last run left, and set up the slot being resumed from
    // (slot -1 means "start from the beginning"). Then keep whatever the run wrote as that slot.
    // loadInPlace (abfeatures: loadstate): the slot's kept state is not copied to slot 0 - its path is
    // returned, for $AB_LOAD_STATE; otherwise (and when there is none) "".
    std::string prepareForLaunch(const PsGame &game, int slot, bool loadInPlace = false);
    void saveAfterLaunch(const PsGame &game, int slot);

    // where the emulator leaves the run's files ($AB_EXIT_DIR); "" = the game's save-state folder
    void setExitDir(const std::string &dir) { exitDir_ = dir; }

    // --- games played through RetroArch ---
    // where RetroArch keeps its states while the launcher runs it (savestate_directory in ra-append.cfg, with the
    // sorting off, so the file is exactly <dir>/<game>.state.auto): <RetroArch dir>/savestates
    static std::string raStatesDir();
    // a RetroArch playlist entry that takes part: not an App, and its core's .info does not say savestate = false
    // (no .info, or no such key: it does)
    bool raSupportsStates(const PsGame &game) const;
    // <raStatesDir()>/<game>.state.auto, "" for a game that is not a RetroArch playlist entry
    std::string raAutoState(const PsGame &game) const;
    // true when the run that just ended left a state (prepareRaLaunch removed the older one first)
    bool raStateWritten(const PsGame &game) const;
    // true when the launch's retroarch.log (launch_rb.sh writes it fresh for every launch) holds RetroArch's own
    // `[State] Auto save state to "<...>/<game>.state.auto" failed.` for this game: a core that claims savestates in
    // its .info but cannot serialize. A crash or a kill prints nothing, so a missing state alone is never read as this.
    bool raAutoSaveFailed(const PsGame &game) const;
    // the decision on a log's text: any line saying the auto save of `stateFileName` failed
    static bool logSaysAutoSaveFailed(const std::string &logText, const std::string &stateFileName);
    // Before a RetroArch launch: the last run's .state.auto and picture go; for slot >= 0 the slot is copied in as
    // .state.auto (+ picture) and true says "load it" (savestate_auto_load); -1, or a slot that is not there,
    // is false - a fresh start. The slots themselves are never touched.
    bool prepareRaLaunch(const PsGame &game, int slot);

private:
    void saveRaSlot(const PsGame &game, int slot);
    // the run's own file `relative` (under the exit dir or the save-state folder, whichever it is in)
    std::string fresh(const PsGame &game, const std::string &relative) const;
    std::string exitDir_;
};
