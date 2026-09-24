//
// ResumePointService: the save-state slots PCSX leaves in a game's !SaveStates folder.
//
#pragma once

#include "../model/ps_game.h"

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
// A foreign (RetroArch or App) entry has none of this, so every call is a no-op for one.
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

private:
    // the run's own file `relative` (under the exit dir or the save-state folder, whichever it is in)
    std::string fresh(const PsGame &game, const std::string &relative) const;
    std::string exitDir_;
};
