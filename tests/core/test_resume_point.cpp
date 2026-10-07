//
// ResumePointService: the save-state slots under a game's !SaveStates folder.
//
#include "doctest/doctest.h"

#include "../support/string_maker.h"
#include "../support/temp_dir.h"

#include "core/services/resume_point.h"

#include <ableem/engine/environment.h>
#include <ableem/engine/filesystem.h>

#include <memory>
#include <string>
#include <utime.h>

using std::string;

namespace {

// A game with the folder layout PCSX leaves behind. Nothing here needs a database - a resume point is
// entirely a matter of files under game.ssFolder.
struct Resume {
    Resume() : tmp("resume") {
        tmp.makeSubDir("Games/Tekken 3");
        tmp.makeSubDir("Games/!SaveStates/Tekken 3/sstates");
        tmp.makeSubDir("Games/!SaveStates/Tekken 3/screenshots");

        game = std::make_shared<PsGame>();
        game->gameId = 1;
        game->title = "Tekken 3";
        game->folder = tmp.at("Games/Tekken 3");
        game->ssFolder = tmp.at("Games/!SaveStates/Tekken 3");
    }

    string ss(const string &relative) const { return game->ssFolder + ableem::sep + relative; }
    bool exists(const string &relative) const { return ableem::DirEntry::exists(ss(relative)); }

    // what PCSX writes on a clean exit: a filename file whose second line names the state, plus the state
    // and screenshot themselves
    void pcsxExitsHavingWritten(const string &stateName) {
        tmp.writeFile("Games/!SaveStates/Tekken 3/filename.txt",
                      "/media/Games/Tekken 3/Tekken 3.cue\n" + stateName + "\n");
        tmp.writeFile("Games/!SaveStates/Tekken 3/sstates/" + stateName + ".000", "the state");
        tmp.writeFile("Games/!SaveStates/Tekken 3/screenshots/" + stateName + ".png", "the screenshot");
        tmp.writeFile("Games/!SaveStates/Tekken 3/lastcdimg.txt", "/media/Games/Tekken 3/Tekken 3.cue\n");
    }

    TempDir tmp;
    PsGamePtr game;
    ResumePointService service;
};

} // namespace

TEST_CASE("a game with nothing saved has no active slots and no picture") {
    Resume r;

    for (int slot = 0; slot < ResumePointService::SlotCount; slot++) {
        CHECK_FALSE(r.service.slotIsActive(*r.game, slot));
        CHECK(r.service.pictureForSlot(*r.game, slot) == "");
    }
    CHECK(r.service.lastPicture(*r.game) == "");
}

TEST_CASE("saving after a run keeps the state, the filename file and the disc note as that slot") {
    Resume r;
    r.pcsxExitsHavingWritten("TEKKEN3");

    r.service.saveAfterLaunch(*r.game, 2);

    CHECK(r.exists("sstates/TEKKEN3.002.res")); // the state, kept for slot 2
    CHECK(r.exists("filename.txt.res"));        // the shared kept copy
    CHECK(r.exists("filename.2.txt.res"));      // and slot 2's own
    CHECK(r.exists("lastcdimg.2.txt"));         // which disc it was playing

    // what PCSX left is moved, not copied
    CHECK_FALSE(r.exists("sstates/TEKKEN3.000"));
    CHECK_FALSE(r.exists("filename.txt"));
    CHECK_FALSE(r.exists("lastcdimg.txt"));
}

TEST_CASE("a run that did not exit cleanly saves nothing") {
    Resume r;
    // no filename.txt: PCSX was killed rather than quit
    r.tmp.writeFile("Games/!SaveStates/Tekken 3/sstates/TEKKEN3.000", "the state");

    CHECK_FALSE(r.service.exitedCleanly(*r.game));

    r.service.saveAfterLaunch(*r.game, 0);

    CHECK_FALSE(r.exists("filename.txt.res"));
    CHECK_FALSE(r.exists("sstates/TEKKEN3.000.res"));
}

TEST_CASE("exitedCleanly is true once PCSX has written its filename file") {
    Resume r;
    CHECK_FALSE(r.service.exitedCleanly(*r.game));

    r.pcsxExitsHavingWritten("TEKKEN3");
    CHECK(r.service.exitedCleanly(*r.game));
}

TEST_CASE("storing the picture makes the slot active and moves the screenshot into place") {
    Resume r;
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 1);

    CHECK_FALSE(r.service.slotIsActive(*r.game, 1)); // the state is kept, but no picture yet

    r.service.storePictureForSlot(*r.game, 1);

    CHECK(r.service.slotIsActive(*r.game, 1));
    CHECK(r.service.pictureForSlot(*r.game, 1) == r.ss("screenshots/TEKKEN3.1.png.res"));
    CHECK_FALSE(r.exists("screenshots/TEKKEN3.png")); // moved, not copied
}

TEST_CASE("timeForSlot is the kept state file's time, 0 for a slot with none") {
    Resume r;
    CHECK(r.service.timeForSlot(*r.game, 1) == 0);

    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 1);

    const time_t t = r.service.timeForSlot(*r.game, 1);
    CHECK(t > 0);
    CHECK(r.service.timeForSlot(*r.game, 2) == 0);
}

TEST_CASE("slot 0's picture is the one without a number in its name") {
    Resume r;
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 0);
    r.service.storePictureForSlot(*r.game, 0);

    // the naming is not uniform across slots, and callers rely on it
    CHECK(r.service.pictureForSlot(*r.game, 0) == r.ss("screenshots/TEKKEN3.png.res"));
}

TEST_CASE("slots are independent of each other") {
    Resume r;

    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 0);
    r.service.storePictureForSlot(*r.game, 0);

    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 3);
    r.service.storePictureForSlot(*r.game, 3);

    CHECK(r.service.slotIsActive(*r.game, 0));
    CHECK(r.service.slotIsActive(*r.game, 3));
    CHECK_FALSE(r.service.slotIsActive(*r.game, 1));
    CHECK_FALSE(r.service.slotIsActive(*r.game, 2));
}

TEST_CASE("removing a slot clears its state and its picture") {
    Resume r;
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 2);
    r.service.storePictureForSlot(*r.game, 2);
    REQUIRE(r.service.slotIsActive(*r.game, 2));

    r.service.removeSlot(*r.game, 2);

    CHECK_FALSE(r.service.slotIsActive(*r.game, 2));
    CHECK_FALSE(r.exists("sstates/TEKKEN3.002.res"));
    CHECK_FALSE(r.exists("screenshots/TEKKEN3.2.png.res"));
}

TEST_CASE("preparing for a launch puts the slot's state back where PCSX will find it") {
    Resume r;
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 1);
    REQUIRE_FALSE(r.exists("sstates/TEKKEN3.000"));

    r.service.prepareForLaunch(*r.game, 1);

    CHECK(r.exists("sstates/TEKKEN3.000")); // PCSX loads this
    CHECK(r.exists("lastcdimg.1.txt"));     // pointing at the disc image in the game's own folder

    // The note is rewritten to point into this game's folder rather than wherever the state was recorded.
    // Compared by prefix: the file is written in text mode, so the line ending is the platform's.
    string note = r.tmp.readFile("Games/!SaveStates/Tekken 3/lastcdimg.1.txt");
    string expected = r.game->folder + ableem::sep + "Tekken 3.cue";
    CHECK(note.compare(0, expected.size(), expected) == 0);
}

TEST_CASE("preparing clears what the previous run left behind") {
    Resume r;
    r.pcsxExitsHavingWritten("TEKKEN3");
    // a crash leaves these lying around, and PCSX will not overwrite them
    REQUIRE(r.exists("filename.txt"));
    REQUIRE(r.exists("sstates/TEKKEN3.000"));
    REQUIRE(r.exists("screenshots/TEKKEN3.png"));

    r.service.prepareForLaunch(*r.game, -1);

    CHECK_FALSE(r.exists("filename.txt"));
    CHECK_FALSE(r.exists("sstates/TEKKEN3.000"));
    CHECK_FALSE(r.exists("screenshots/TEKKEN3.png"));
}

TEST_CASE("preparing slot -1 starts from the beginning and restores nothing") {
    Resume r;
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 0);

    r.service.prepareForLaunch(*r.game, -1);

    CHECK_FALSE(r.exists("sstates/TEKKEN3.000")); // nothing put back for PCSX to load
    CHECK(r.exists("sstates/TEKKEN3.000.res"));   // but the slot itself is untouched
}

TEST_CASE("a full save, resume and re-save round trip keeps the slot") {
    Resume r;

    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 1);
    r.service.storePictureForSlot(*r.game, 1);
    REQUIRE(r.service.slotIsActive(*r.game, 1));

    // resume from it, play, and quit again
    r.service.prepareForLaunch(*r.game, 1);
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 1);
    r.service.storePictureForSlot(*r.game, 1);

    CHECK(r.service.slotIsActive(*r.game, 1));
    CHECK(r.exists("sstates/TEKKEN3.001.res"));
}

TEST_CASE("a game saved only in slots 1 and 2 still has a last picture and active slots (BUG-37)") {
    Resume r;
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 1);
    r.service.storePictureForSlot(*r.game, 1);
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 2);
    r.service.storePictureForSlot(*r.game, 2);

    CHECK_FALSE(r.service.slotIsActive(*r.game, 0));
    CHECK(r.service.slotIsActive(*r.game, 1)); // what the "No resume points" decision asks, slot by slot
    CHECK(r.service.slotIsActive(*r.game, 2));
    CHECK(r.service.lastPicture(*r.game) == r.ss("screenshots/TEKKEN3.1.png.res"));
}

TEST_CASE("the last picture is slot 0's when it is kept") {
    Resume r;
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 2);
    r.service.storePictureForSlot(*r.game, 2);
    r.pcsxExitsHavingWritten("TEKKEN3");
    r.service.saveAfterLaunch(*r.game, 0);
    r.service.storePictureForSlot(*r.game, 0);

    CHECK(r.service.lastPicture(*r.game) == r.ss("screenshots/TEKKEN3.png.res"));
}

TEST_CASE("an App has no resume points at all") {
    Resume r;
    r.game->foreign = true;
    r.game->app = true;
    r.pcsxExitsHavingWritten("TEKKEN3");

    CHECK(r.service.exitedCleanly(*r.game)); // nothing writes one, so nothing can be missing
    CHECK(r.service.lastPicture(*r.game) == "");
    CHECK_FALSE(r.service.slotIsActive(*r.game, 0));

    // and the calls that write are no-ops rather than touching a folder it does not own
    r.service.storePictureForSlot(*r.game, 0);
    CHECK(r.exists("screenshots/TEKKEN3.png"));
    r.service.removeSlot(*r.game, 0);
}

TEST_CASE("an emulator with an exit dir: the run's files are read there, and only what is kept reaches the folder") {
    Resume r;
    const string exitDir = r.tmp.at("run/exit");
    r.service.setExitDir(exitDir);

    // what pcsx-abnxt writes with $AB_EXIT_DIR: the same four files, under the exit dir
    r.tmp.writeFile("run/exit/filename.txt", "/media/Games/Tekken 3/Tekken 3.cue\nTEKKEN3\n");
    r.tmp.writeFile("run/exit/sstates/TEKKEN3.000", "the state");
    r.tmp.writeFile("run/exit/screenshots/TEKKEN3.png", "the screenshot");
    r.tmp.writeFile("run/exit/lastcdimg.txt", "/media/Games/Tekken 3/Tekken 3.cue\n");
    CHECK(r.service.exitedCleanly(*r.game));

    r.service.saveAfterLaunch(*r.game, 2);
    r.service.storePictureForSlot(*r.game, 2);
    CHECK(r.service.slotIsActive(*r.game, 2));
    CHECK(r.tmp.readFile("Games/!SaveStates/Tekken 3/sstates/TEKKEN3.002.res") == "the state");
    CHECK(r.tmp.readFile("Games/!SaveStates/Tekken 3/lastcdimg.2.txt") == "/media/Games/Tekken 3/Tekken 3.cue\n");
    CHECK_FALSE(r.exists("filename.txt")); // nothing of the run's own left in the folder
    CHECK_FALSE(r.exists("sstates/TEKKEN3.000"));
    CHECK_FALSE(r.exists("lastcdimg.txt"));

    // the next launch clears the exit dir (RAM) and, resuming in place, copies nothing
    r.tmp.writeFile("run/exit/sstates/leftover.000", "a run nobody kept");
    string load = r.service.prepareForLaunch(*r.game, 2, true);
    CHECK(load == r.ss("sstates/TEKKEN3.002.res"));
    CHECK_FALSE(r.exists("sstates/TEKKEN3.000"));
    CHECK_FALSE(ableem::DirEntry::exists(exitDir));
}

// --- games played through RetroArch ---

namespace {

// A playlist entry with RetroArch's tree under the temp dir: the states RetroArch leaves are in
// RetroArch/bin/savestates, the slots under RetroArch/bin/ab-states/<core>/<game>.
struct RaResume {
    RaResume() : tmp("raresume") {
        ableem::Environment::setRetroarchDir(tmp.at("RetroArch/bin"));
        tmp.makeSubDir("RetroArch/bin/savestates");
        tmp.makeSubDir("RetroArch/bin/info");
        game = std::make_shared<PsGame>();
        game->foreign = true;
        game->title = "Sonic";
        game->image_path = tmp.at("RetroArch/roms/Sonic The Hedgehog.md");
        game->core_path = tmp.at("RetroArch/bin/cores/picodrive_libretro.so");
    }
    ~RaResume() { ableem::Environment::setRetroarchDir(""); }

    string slots(const string &relative) const {
        return tmp.at("RetroArch/bin/ab-states/picodrive_libretro/Sonic The Hedgehog/" + relative);
    }
    string autoState() const { return tmp.at("RetroArch/bin/savestates/Sonic The Hedgehog.state.auto"); }
    // what RetroArch leaves when the game ends with savestate_auto_save on
    void raExitsHavingWritten(const string &state, bool picture = true) {
        tmp.writeFile("RetroArch/bin/savestates/Sonic The Hedgehog.state.auto", state);
        if (picture)
            tmp.writeFile("RetroArch/bin/savestates/Sonic The Hedgehog.state.auto.png", "pic of " + state);
    }

    TempDir tmp;
    PsGamePtr game;
    ResumePointService service;
};

} // namespace

TEST_CASE("RetroArch: its state is named after the game file and sits in the pinned savestates folder") {
    RaResume r;
    CHECK(r.service.raAutoState(*r.game) == r.autoState());
    CHECK(ResumePointService::raStatesDir() == r.tmp.at("RetroArch/bin/savestates"));
    CHECK_FALSE(r.service.raStateWritten(*r.game));

    r.raExitsHavingWritten("state one");
    CHECK(r.service.raStateWritten(*r.game));

    // an App has none, and neither does one of our own games
    PsGame app;
    app.foreign = true;
    app.app = true;
    CHECK(r.service.raAutoState(app) == "");
    PsGame ps1;
    CHECK(r.service.raAutoState(ps1) == "");
}

TEST_CASE("RetroArch: saving after a run keeps the state and its picture as the slot, in the PS1 layout") {
    RaResume r;
    r.raExitsHavingWritten("state one");

    r.service.saveAfterLaunch(*r.game, 2);

    CHECK(r.tmp.readFile(
              "RetroArch/bin/ab-states/picodrive_libretro/Sonic The Hedgehog/sstates/Sonic The Hedgehog.002.res") ==
          "state one");
    CHECK(r.service.slotIsActive(*r.game, 2));
    CHECK(r.service.pictureForSlot(*r.game, 2) == r.slots("screenshots/Sonic The Hedgehog.2.png.res"));
    CHECK(
        r.tmp.readFile(
            "RetroArch/bin/ab-states/picodrive_libretro/Sonic The Hedgehog/screenshots/Sonic The Hedgehog.2.png.res") ==
        "pic of state one");
    CHECK(r.service.timeForSlot(*r.game, 2) > 0);
    CHECK_FALSE(r.service.slotIsActive(*r.game, 1));
    // RetroArch's own files are taken away, so the next run's state cannot be mistaken for this one
    CHECK_FALSE(ableem::DirEntry::exists(r.autoState()));
    CHECK_FALSE(ableem::DirEntry::exists(r.autoState() + ".png"));
    // and the picture step of the launcher's picker has nothing more to do
    r.service.storePictureForSlot(*r.game, 2);
    CHECK(r.service.slotIsActive(*r.game, 2));
}

TEST_CASE("RetroArch: a slot is active without a picture, and a state that was never written saves nothing") {
    RaResume r;
    r.service.saveAfterLaunch(*r.game, 1);
    CHECK_FALSE(r.service.slotIsActive(*r.game, 1));

    r.raExitsHavingWritten("state", false);
    r.service.saveAfterLaunch(*r.game, 1);
    CHECK(r.service.slotIsActive(*r.game, 1));
    CHECK(r.service.pictureForSlot(*r.game, 1) == "");

    // an empty file (a write cut short) is not a state
    RaResume empty;
    empty.raExitsHavingWritten("", false);
    CHECK_FALSE(empty.service.raStateWritten(*empty.game));
    empty.service.saveAfterLaunch(*empty.game, 0);
    CHECK_FALSE(empty.service.slotIsActive(*empty.game, 0));
}

TEST_CASE("RetroArch: slots are separate per core and game, and one slot replaces the other's state") {
    RaResume r;
    r.raExitsHavingWritten("first");
    r.service.saveAfterLaunch(*r.game, 0);
    r.raExitsHavingWritten("second");
    r.service.saveAfterLaunch(*r.game, 3);
    r.raExitsHavingWritten("third");
    r.service.saveAfterLaunch(*r.game, 0); // slot 0 again: replaced
    CHECK(r.tmp.readFile(
              "RetroArch/bin/ab-states/picodrive_libretro/Sonic The Hedgehog/sstates/Sonic The Hedgehog.000.res") ==
          "third");
    CHECK(r.service.lastPicture(*r.game) == r.slots("screenshots/Sonic The Hedgehog.png.res"));

    // the same file played by another core is another set
    PsGame other = *r.game;
    other.core_path = r.tmp.at("RetroArch/bin/cores/genesis_plus_gx_libretro.so");
    CHECK_FALSE(r.service.slotIsActive(other, 0));
    CHECK_FALSE(r.service.slotIsActive(other, 3));
}

TEST_CASE("RetroArch: preparing a launch clears the last run's state and, for a slot, puts it in place to load") {
    RaResume r;
    r.raExitsHavingWritten("slot one state");
    r.service.saveAfterLaunch(*r.game, 1);

    // a crash left this one: it must not be offered as the next run's state
    r.raExitsHavingWritten("crashed run");
    CHECK_FALSE(r.service.prepareRaLaunch(*r.game, -1)); // start fresh
    CHECK_FALSE(ableem::DirEntry::exists(r.autoState()));
    CHECK_FALSE(ableem::DirEntry::exists(r.autoState() + ".png"));
    CHECK(r.service.slotIsActive(*r.game, 1)); // the slot itself is never touched by starting fresh

    CHECK(r.service.prepareRaLaunch(*r.game, 1)); // resume slot 1
    CHECK(r.tmp.readFile("RetroArch/bin/savestates/Sonic The Hedgehog.state.auto") == "slot one state");

    // a slot that is not there: a fresh start, nothing loaded
    CHECK_FALSE(r.service.prepareRaLaunch(*r.game, 2));
    CHECK_FALSE(ableem::DirEntry::exists(r.autoState()));
}

TEST_CASE("RetroArch: a zipped game's state and slot carry the entry's name, not the archive's (BUG-53)") {
    for (const char *archive : {"Pack Name.zip", "Pack Name.7z"}) {
        RaResume r;
        r.game->image_path = r.tmp.at(string("RetroArch/roms/") + archive + "#Entry Name.md");
        CHECK(r.service.raAutoState(*r.game) == r.tmp.at("RetroArch/bin/savestates/Entry Name.state.auto"));

        r.tmp.writeFile("RetroArch/bin/savestates/Entry Name.state.auto", "zip state");
        CHECK(r.service.raStateWritten(*r.game));
        r.service.saveAfterLaunch(*r.game, 1);
        CHECK(r.service.slotIsActive(*r.game, 1));
        CHECK(ableem::DirEntry::exists(
            r.tmp.at("RetroArch/bin/ab-states/picodrive_libretro/Entry Name/sstates/Entry Name.001.res")));
    }
}

TEST_CASE("RetroArch: removing a slot clears its state and picture") {
    RaResume r;
    r.raExitsHavingWritten("state");
    r.service.saveAfterLaunch(*r.game, 2);
    REQUIRE(r.service.slotIsActive(*r.game, 2));

    r.service.removeSlot(*r.game, 2);

    CHECK_FALSE(r.service.slotIsActive(*r.game, 2));
    CHECK_FALSE(ableem::DirEntry::exists(r.slots("screenshots/Sonic The Hedgehog.2.png.res")));
}

TEST_CASE("RetroArch: a core whose .info says savestate = false takes no part") {
    RaResume r;
    CHECK(r.service.raSupportsStates(*r.game)); // no .info at all: supported, as RetroArch has it

    r.tmp.writeFile(
        "RetroArch/bin/info/picodrive_libretro.info",
        "display_name = \"Sega - MS/GG/MD/MCD (PicoDrive)\"\nsavestate = \"true\"\nsavestate_features = \"basic\"\n");
    CHECK(r.service.raSupportsStates(*r.game));

    r.tmp.writeFile("RetroArch/bin/info/picodrive_libretro.info",
                    "display_name = \"X\"\nsavestate_features = \"basic\"\nsavestate = \"false\"\n");
    CHECK_FALSE(r.service.raSupportsStates(*r.game));

    // savestate_features alone is not the switch
    r.tmp.writeFile("RetroArch/bin/info/picodrive_libretro.info", "savestate_features = \"false\"\n");
    CHECK(r.service.raSupportsStates(*r.game));

    PsGame app;
    app.foreign = true;
    app.app = true;
    CHECK_FALSE(r.service.raSupportsStates(app));
    PsGame ps1;
    CHECK_FALSE(r.service.raSupportsStates(ps1));
}

TEST_CASE("RetroArch: the newest slot is the one kept last, -1 when there is none") {
    RaResume r;
    CHECK(r.service.newestSlot(*r.game) == -1);

    r.raExitsHavingWritten("one");
    r.service.saveAfterLaunch(*r.game, 2);
    CHECK(r.service.newestSlot(*r.game) == 2);

    // a state file's time is whole seconds: make slot 0's clearly newer by touching it forward
    r.raExitsHavingWritten("two");
    r.service.saveAfterLaunch(*r.game, 0);
    const string newer = r.slots("sstates/Sonic The Hedgehog.000.res");
    struct utimbuf times = {time(nullptr) + 100, time(nullptr) + 100};
    REQUIRE(utime(newer.c_str(), &times) == 0);
    CHECK(r.service.newestSlot(*r.game) == 0);

    PsGame ps1;
    CHECK(r.service.newestSlot(ps1) == -1);
}

// --- a core that claims savestates but whose auto save fails ---

TEST_CASE("RetroArch: its own 'Auto save state ... failed' line for this game is the failure, nothing else is") {
    const string failed = "[INFO] [Core]: Content ran\n"
                          "[ERROR] [State] Auto save state to \"/media/RetroArch/bin/savestates/Sonic The "
                          "Hedgehog.state.auto\" failed.\n";
    CHECK(ResumePointService::logSaysAutoSaveFailed(failed, "Sonic The Hedgehog.state.auto"));
    // another game's, a success, a crash with no line, an empty log
    CHECK_FALSE(ResumePointService::logSaysAutoSaveFailed(failed, "Other.state.auto"));
    CHECK_FALSE(ResumePointService::logSaysAutoSaveFailed(
        "[INFO] [State] Auto save state to \"/x/Sonic The Hedgehog.state.auto\" succeeded.\n",
        "Sonic The Hedgehog.state.auto"));
    CHECK_FALSE(ResumePointService::logSaysAutoSaveFailed("[ERROR] segfault\n", "Sonic The Hedgehog.state.auto"));
    CHECK_FALSE(ResumePointService::logSaysAutoSaveFailed("", "Sonic The Hedgehog.state.auto"));
}

TEST_CASE("RetroArch: the launch's retroarch.log is read from the logs dir; a missing log is no failure") {
    RaResume r;
    ableem::Environment::setRuntimeDir(r.tmp.at("run"));
    CHECK_FALSE(r.service.raAutoSaveFailed(*r.game)); // no log

    r.tmp.writeFile("run/logs/retroarch.log",
                    "[ERROR] [State] Auto save state to \"/s/Sonic The Hedgehog.state.auto\" failed.\n");
    CHECK(r.service.raAutoSaveFailed(*r.game));

    PsGame other = *r.game;
    other.image_path = "/media/RetroArch/roms/Other.md";
    CHECK_FALSE(r.service.raAutoSaveFailed(other));
    PsGame ps1;
    CHECK_FALSE(r.service.raAutoSaveFailed(ps1));
    ableem::Environment::setRuntimeDir("");
}

// --- the Resume row of our PS1 games (EMU-26): what Play starts from ---

TEST_CASE("slotForPlay: ask and never start from the beginning, last from the newest slot") {
    Resume r;
    r.pcsxExitsHavingWritten("one");
    r.service.saveAfterLaunch(*r.game, 1);
    r.service.storePictureForSlot(*r.game, 1);
    r.pcsxExitsHavingWritten("two");
    r.service.saveAfterLaunch(*r.game, 3);
    r.service.storePictureForSlot(*r.game, 3);
    // a state file's time is whole seconds: make slot 1's clearly newer by touching it forward
    const string newer = r.ss("sstates/one.001.res");
    struct utimbuf times = {time(nullptr) + 100, time(nullptr) + 100};
    REQUIRE(utime(newer.c_str(), &times) == 0);

    CHECK(r.service.slotForPlay(*r.game, ResumePointService::Ask) == -1);
    CHECK(r.service.slotForPlay(*r.game, ResumePointService::Never) == -1);
    CHECK(r.service.slotForPlay(*r.game, ResumePointService::Last) == 1);

    r.service.removeSlot(*r.game, 1);
    CHECK(r.service.slotForPlay(*r.game, ResumePointService::Last) == 3);
}

TEST_CASE("slotForPlay: a game with no slot (or an App) starts from the beginning whatever the mode") {
    Resume r;
    CHECK(r.service.slotForPlay(*r.game, ResumePointService::Last) == -1);
    PsGame app;
    app.foreign = true;
    app.app = true;
    CHECK(r.service.slotForPlay(app, ResumePointService::Last) == -1);
}

TEST_CASE("discardRun drops what the run wrote and keeps every slot") {
    Resume r;
    r.pcsxExitsHavingWritten("kept");
    r.service.saveAfterLaunch(*r.game, 2);
    r.service.storePictureForSlot(*r.game, 2);
    r.pcsxExitsHavingWritten("fresh");

    r.service.discardRun(*r.game);

    CHECK_FALSE(r.exists("filename.txt"));
    CHECK_FALSE(r.exists("sstates/fresh.000"));
    CHECK_FALSE(r.exists("screenshots/fresh.png"));
    CHECK_FALSE(r.exists("lastcdimg.txt"));
    CHECK_FALSE(r.service.exitedCleanly(*r.game));
    CHECK(r.service.slotIsActive(*r.game, 2));
    CHECK(r.exists("sstates/kept.002.res"));
    CHECK(r.exists("lastcdimg.2.txt"));
}
