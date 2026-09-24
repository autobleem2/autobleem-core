//
// ScanService: the background scan and its incremental regional.db reconciliation. Every test here drives
// the worker's methods (runScan()/checkForChanges()) directly, synchronously - see their header comment -
// so these tests need no real thread and no timing.
//
#include "doctest/doctest.h"

#include "../support/fake_game.h"
#include "../support/game_library_fixture.h"
#include "../support/rdb_builder.h"
#include "../support/string_maker.h"

#include "core/services/scan_service.h"

#include <ableem/engine/ini_file.h>
#include <ableem/engine/retroarch_playlist.h>
#include <ableem/engine/serial_scanner.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif

using std::string;
using std::vector;

namespace {

struct ScanServiceFixture : GameLibraryFixture {
    ScanServiceFixture() : svc(library) { env.setWorkingPath(tmp.path()); }

    // runs one scan cycle and applies every event it produced - what a real frame loop does over several
    // poll() calls, collapsed into one for a test that only cares about the end state
    ScanUpdate runAndPoll() {
        svc.runScan();
        return svc.poll();
    }

    string gamesDir() const { return tmp.at("Games"); }

    ScanService svc;
};

vector<string> titlesOf(const PsGames &games) {
    vector<string> titles;
    for (const auto &g : games)
        titles.push_back(g->title);
    return titles;
}

} // namespace

TEST_CASE("a first scan adds every verified game and saves the fingerprint") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");
    test_support::makeFakeGame(fx.gamesDir(), "Spyro", "SLUS_012.35");

    ScanUpdate update = fx.runAndPoll();

    CHECK(update.finished);
    CHECK(update.finishedGameCount == 2);
    CHECK(update.finishedFailedCount == 0);
    CHECK(update.addedGames.size() == 2);
    CHECK(update.updatedGames.empty());
    CHECK(update.removedGameIds.empty());

    vector<string> titles = titlesOf(update.addedGames);
    CHECK(std::find(titles.begin(), titles.end(), "Crash Bandicoot") != titles.end());
    CHECK(std::find(titles.begin(), titles.end(), "Spyro") != titles.end());

    CHECK(fx.library.usbGames().countGames() == 2);
    CHECK(ableem::DirEntry::exists(fx.tmp.at("games.fingerprint")));
    CHECK(ableem::DirEntry::exists(fx.tmp.at("autobleem.list")));
}

TEST_CASE("rescanning an unchanged game updates its row in place - same id, no duplicate") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");

    ScanUpdate first = fx.runAndPoll();
    REQUIRE(first.addedGames.size() == 1);
    int id = first.addedGames[0]->gameId;

    fx.library.usbGames().updateHistory(id, 1);
    fx.library.usbGames().updateDatePlayed(id, 999);

    ScanUpdate second = fx.runAndPoll();
    CHECK(second.addedGames.empty());
    REQUIRE(second.updatedGames.size() == 1);
    CHECK(second.updatedGames[0]->gameId == id);

    CHECK(fx.library.usbGames().countGames() == 1); // no duplicate row

    auto games = fx.library.usbGames().loadUsbGames();
    REQUIRE(games.size() == 1);
    CHECK(games[0].history == 1);       // history survives a rescan
    CHECK(games[0].last_played == 999); // so does last_played
}

TEST_CASE("the Game.ini flags the editor writes survive a rescan") {
    // Favorite and Play_using_ra are read back from Game.ini on every scan and written out again;
    // a misspelt key in that read-back (play_us_ra) used to reset Play using RA to false each time
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");
    REQUIRE(fx.runAndPoll().addedGames.size() == 1);

    string iniPath = fx.tmp.at("Games/Crash Bandicoot/Game.ini");
    ableem::IniFile ini;
    ini.load(iniPath);
    REQUIRE_FALSE(ini.values.empty());
    ini.values["favorite"] = "1";
    ini.values["play_using_ra"] = "true";
    ini.save(iniPath);

    ScanUpdate second = fx.runAndPoll();
    REQUIRE(second.updatedGames.size() == 1);
    CHECK(second.updatedGames[0]->favorite);
    CHECK(second.updatedGames[0]->play_using_ra);

    ini.values.clear();
    ini.load(iniPath);
    CHECK(ini.values["favorite"] == "1");
    CHECK(ini.values["play_using_ra"] == "true");
}

TEST_CASE("a locked game keeps the serial its Game.ini holds; an unlocked one is read from the image") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");
    test_support::makeFakeGame(fx.gamesDir(), "Spyro", "SLUS_012.35");
    REQUIRE(fx.runAndPoll().addedGames.size() == 2);

    auto setSerial = [&](const string &game, const string &automation, const string &serial) {
        string iniPath = fx.tmp.at("Games/" + game + "/Game.ini");
        ableem::IniFile ini;
        ini.load(iniPath);
        REQUIRE(ini.values["serial"] != serial);
        ini.values["automation"] = automation;
        ini.values["serial"] = serial;
        ini.values["region"] = "";
        ini.save(iniPath);
    };
    setSerial("Crash Bandicoot", "0", "SCUS-94900"); // locked: the user's serial is the truth
    setSerial("Spyro", "1", "SCUS-94901");           // unlocked: the image is

    fx.runAndPoll();

    // regional.db holds no serial for a USB game - Game.ini is where it lives (the meta panel reads it
    // from there), so that is what the scan must have left right
    auto iniValue = [&](const string &game, const string &key) {
        ableem::IniFile ini;
        ini.load(fx.tmp.at("Games/" + game + "/Game.ini"));
        return ini.values[key];
    };
    CHECK(iniValue("Crash Bandicoot", "serial") == "SCUS-94900");
    CHECK(iniValue("Crash Bandicoot", "region") ==
          ableem::SerialScanner::serialToRegion("SCUS-94900")); // derived, not left blank
    CHECK(iniValue("Spyro", "serial") == "SLUS-01235");
}

TEST_CASE("with a RetroArch tree the scan takes the title from the rdb and caches the thumbnail paths") {
    ScanServiceFixture fx;
    fx.env.setRetroarchDir(fx.tmp.makeSubDir("retroarch"));
    fx.tmp.makeSubDir("retroarch/database/rdb");
    test_support::Bytes records;
    test_support::appendGameRecord(records, "Crash Bandicoot (USA)", "SLUS-01234", "USA", "SCEA", 1996, 1);
    records.push_back(0xc0);
    test_support::writeRdb(fx.tmp, "retroarch/database/rdb/Sony - PlayStation.rdb", test_support::makeRdb(0, records));
    fx.tmp.makeSubDir("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts");
    fx.tmp.writeFile("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts/Crash Bandicoot (USA).png", "png");
    fx.tmp.makeSubDir("retroarch/thumbnails/Sony - PlayStation/Named_Snaps");
    fx.tmp.writeFile("retroarch/thumbnails/Sony - PlayStation/Named_Snaps/Crash Bandicoot (USA).png", "png");

    test_support::makeFakeGame(fx.gamesDir(), "crash", "SLUS_012.34"); // folder name is not the title
    ScanUpdate update = fx.runAndPoll();
    REQUIRE(update.addedGames.size() == 1);
    const PsGame &game = *update.addedGames[0];
    CHECK(game.title == "Crash Bandicoot"); // the rdb's, without its "(USA)"
    CHECK(game.publisher == "SCEA");
    CHECK(game.year == 1996);
    CHECK(game.recordName == "Crash Bandicoot (USA)");
    CHECK(game.coverPath ==
          fx.tmp.at("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts/Crash Bandicoot (USA).png"));
    CHECK(game.snapPath == fx.tmp.at("retroarch/thumbnails/Sony - PlayStation/Named_Snaps/Crash Bandicoot (USA).png"));

    ableem::IniFile ini;
    ini.load(fx.tmp.at("Games/crash/Game.ini"));
    CHECK(ini.values["thumbnail_record_name"] == "Crash Bandicoot (USA)");
    CHECK(ini.values["cached_cover_path"] == game.coverPath);
    CHECK(ini.values["cached_snap_path"] == game.snapPath);
}

TEST_CASE(
    "a default.png placeholder next to a game is removed once the thumbnails tree has a cover; a real cover stays") {
    ScanServiceFixture fx;
    fx.env.setRetroarchDir(fx.tmp.makeSubDir("retroarch"));
    fx.tmp.writeFile("default.png", "placeholder png bytes"); // the working path's default cover
    fx.tmp.makeSubDir("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts");
    fx.tmp.writeFile("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts/Crash Bandicoot.png", "png");
    fx.tmp.writeFile("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts/Spyro.png", "png");

    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");
    fx.tmp.writeFile("Games/Crash Bandicoot/Crash Bandicoot.png", "placeholder png bytes"); // an old scan's copy
    test_support::makeFakeGame(fx.gamesDir(), "Spyro", "SLUS_012.35");
    fx.tmp.writeFile("Games/Spyro/Spyro.png", "the user's own cover");

    ScanUpdate update = fx.runAndPoll();
    REQUIRE(update.addedGames.size() == 2);
    CHECK_FALSE(ableem::DirEntry::exists(fx.tmp.at("Games/Crash Bandicoot/Crash Bandicoot.png")));
    CHECK(ableem::DirEntry::exists(fx.tmp.at("Games/Spyro/Spyro.png")));
    for (const auto &g : update.addedGames)
        CHECK(g->coverPath == fx.tmp.at("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts/" + g->title + ".png"));
}

TEST_CASE("sibling (Disc n) folders are merged into one game before the scan, with an .m3u") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Final Fantasy VII (Disc 2)", "SLUS_009.00");
    test_support::makeFakeGame(fx.gamesDir(), "Final Fantasy VII (Disc 1)", "SLUS_008.99");
    test_support::makeFakeGame(fx.gamesDir(), "Final Fantasy VII (Disc 3)", "SLUS_009.01");
    test_support::makeFakeGame(fx.gamesDir(), "Single Disc Game", "SLUS_012.34");
    // an earlier scan's Game.ini on disc 1, with the disc list of one disc
    fx.tmp.writeFile(
        "Games/Final Fantasy VII (Disc 1)/Game.ini",
        "[Game]\nAutomation=1\nTitle=Final Fantasy VII (Disc 1)\nDiscs=Final Fantasy VII (Disc 1)\nFavorite=1\n");

    ScanUpdate update = fx.runAndPoll();
    REQUIRE(update.addedGames.size() == 2); // one merged game, one single-disc one

    string merged = fx.tmp.at("Games/Final Fantasy VII");
    CHECK(ableem::DirEntry::exists(merged));
    CHECK_FALSE(ableem::DirEntry::exists(fx.tmp.at("Games/Final Fantasy VII (Disc 1)")));
    CHECK_FALSE(ableem::DirEntry::exists(fx.tmp.at("Games/Final Fantasy VII (Disc 2)")));
    CHECK_FALSE(ableem::DirEntry::exists(fx.tmp.at("Games/Final Fantasy VII (Disc 3)")));
    CHECK(ableem::DirEntry::exists(merged + "/Final Fantasy VII (Disc 1).cue"));
    CHECK(ableem::DirEntry::exists(merged + "/Final Fantasy VII (Disc 2).bin"));
    CHECK(ableem::DirEntry::exists(merged + "/Final Fantasy VII (Disc 3).cue"));
    CHECK(ableem::DirEntry::exists(
        merged + "/Final Fantasy VII (Disc 1).m3u")); // named after the first disc, like every multi-disc folder's

    const PsGame *ff = nullptr;
    for (const auto &g : update.addedGames)
        if (g->folder.find("Final Fantasy VII") != string::npos)
            ff = g.get();
    REQUIRE(ff != nullptr);
    CHECK(ff->cds == 3);
    CHECK(ff->title == "Final Fantasy VII"); // the folder-derived "(Disc 1)" title became the base name
    CHECK(ff->favorite);                     // disc 1's Game.ini survived the merge

    // a second scan finds nothing to merge and changes nothing
    ScanUpdate second = fx.runAndPoll();
    CHECK(second.addedGames.empty());
    CHECK(second.updatedGames.size() == 2);
}

TEST_CASE(
    "a merge is skipped when the merged folder already exists as something else, or a file would be overwritten") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Tekken (Disc 1)", "SLUS_008.99");
    test_support::makeFakeGame(fx.gamesDir(), "Tekken (Disc 2)", "SLUS_009.00");
    test_support::makeFakeGame(fx.gamesDir(), "Tekken", "SLUS_009.01"); // a third, unrelated game in the way

    test_support::makeFakeGame(fx.gamesDir(), "Wipeout (Disc 1)", "SLUS_010.00");
    test_support::makeFakeGame(fx.gamesDir(), "Wipeout (Disc 2)", "SLUS_010.01");
    // disc 1's folder already holds a copy of disc 2's files: moving them in would overwrite these
    ableem::DirEntry::copy(fx.tmp.at("Games/Wipeout (Disc 2)/Wipeout (Disc 2).cue"),
                           fx.tmp.at("Games/Wipeout (Disc 1)/Wipeout (Disc 2).cue"));
    ableem::DirEntry::copy(fx.tmp.at("Games/Wipeout (Disc 2)/Wipeout (Disc 2).bin"),
                           fx.tmp.at("Games/Wipeout (Disc 1)/Wipeout (Disc 2).bin"));

    ScanUpdate update = fx.runAndPoll();
    CHECK(ableem::DirEntry::exists(fx.tmp.at("Games/Tekken (Disc 1)")));
    CHECK(ableem::DirEntry::exists(fx.tmp.at("Games/Tekken (Disc 2)")));
    CHECK(ableem::DirEntry::exists(fx.tmp.at("Games/Tekken")));
    CHECK(ableem::DirEntry::exists(fx.tmp.at("Games/Wipeout")));          // disc 1 was renamed...
    CHECK(ableem::DirEntry::exists(fx.tmp.at("Games/Wipeout (Disc 2)"))); // ...but disc 2 stayed where it was
    CHECK(ableem::DirEntry::exists(fx.tmp.at("Games/Wipeout (Disc 2)/Wipeout (Disc 2).cue"))); // not moved
    CHECK(update.addedGames.size() == 5);
}

TEST_CASE("a game folder that disappears is removed from regional.db on the next scan") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");
    test_support::makeFakeGame(fx.gamesDir(), "Spyro", "SLUS_012.35");
    ScanUpdate first = fx.runAndPoll();
    REQUIRE(first.addedGames.size() == 2);

    ableem::DirEntry::removeDirAndContents(fx.gamesDir() + ableem::sep + "Spyro");

    ScanUpdate second = fx.runAndPoll();
    REQUIRE(second.removedGameIds.size() == 1);
    CHECK(fx.library.usbGames().countGames() == 1);

    auto games = fx.library.usbGames().loadUsbGames();
    REQUIRE(games.size() == 1);
    CHECK(games[0].title == "Crash Bandicoot");
}

TEST_CASE("a game folder moved into a sub-folder keeps its row - id, history and last_played") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");
    test_support::makeFakeGame(fx.gamesDir(), "Spyro", "SLUS_012.35");
    ScanUpdate first = fx.runAndPoll();
    REQUIRE(first.addedGames.size() == 2);
    int crashId = 0;
    for (const auto &g : first.addedGames)
        if (g->title == "Crash Bandicoot")
            crashId = g->gameId;
    REQUIRE(crashId != 0);
    fx.library.usbGames().updateHistory(crashId, 1);
    fx.library.usbGames().updateDatePlayed(crashId, 999);

    // what a user does on the PC: drag the folder into a new sub-folder of Games/
    string sub = fx.tmp.makeSubDir("Games/Platformers");
    REQUIRE(rename((fx.gamesDir() + ableem::sep + "Crash Bandicoot").c_str(),
                   (sub + ableem::sep + "Crash Bandicoot").c_str()) == 0);

    ScanUpdate second = fx.runAndPoll();
    CHECK(second.removedGameIds.empty());
    CHECK(second.addedGames.empty());
    REQUIRE(second.updatedGames.size() == 2); // both rescanned in place, the moved one at its new path
    CHECK(fx.library.usbGames().countGames() == 2);

    int id = 0;
    CHECK(fx.library.usbGames().findGameIdByPath(sub + ableem::sep + "Crash Bandicoot" + ableem::sep, &id));
    CHECK(id == crashId);
    for (const auto &g : fx.library.usbGames().loadUsbGames()) {
        if (g.gameId != crashId)
            continue;
        CHECK(g.history == 1);
        CHECK(g.last_played == 999);
        CHECK(ableem::DirEntry::removeSeparatorFromEndOfPath(g.folder) == sub + ableem::sep + "Crash Bandicoot");
    }
}

TEST_CASE("a different game appearing under a vanished folder's name is a new game, and the old row goes") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");
    ScanUpdate first = fx.runAndPoll();
    REQUIRE(first.addedGames.size() == 1);
    int oldId = first.addedGames[0]->gameId;

    // the folder is replaced by another game whose image files are named differently
    ableem::DirEntry::removeDirAndContents(fx.gamesDir() + ableem::sep + "Crash Bandicoot");
    string sub = fx.tmp.makeSubDir("Games/Other");
    test_support::makeFakeGame(sub, "Crash Bandicoot", "SLUS_099.99");
    // same folder name, different disc file name
    string folder = sub + ableem::sep + "Crash Bandicoot";
    REQUIRE(rename((folder + ableem::sep + "Crash Bandicoot.bin").c_str(),
                   (folder + ableem::sep + "Other.bin").c_str()) == 0);
    REQUIRE(rename((folder + ableem::sep + "Crash Bandicoot.cue").c_str(),
                   (folder + ableem::sep + "Other.cue").c_str()) == 0);
    {
        std::ofstream cue(folder + ableem::sep + "Other.cue", std::ios::binary);
        cue << "FILE \"Other.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
    }

    ScanUpdate second = fx.runAndPoll();
    REQUIRE(second.removedGameIds.size() == 1);
    CHECK(second.removedGameIds[0] == oldId);
    REQUIRE(second.addedGames.size() == 1);
    CHECK(second.addedGames[0]->gameId != oldId);
    CHECK(fx.library.usbGames().countGames() == 1);
}

TEST_CASE("a game that fails verify() is dropped from regional.db") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");
    ScanUpdate first = fx.runAndPoll();
    REQUIRE(first.addedGames.size() == 1);

    // Break the game without removing its folder or its .bin, so it still looks like a game folder at the
    // hierarchy stage (GameSubDir::scanAll requires a .bin/.pbp/.img/.chd file - a folder missing one is
    // excluded from the scan entirely rather than reaching verify(), which is not what this test is after).
    // A second FILE line naming a disc that does not exist is what actually fails verify(): the scanner's
    // own cue repair (repairBrokenCueFiles) cannot heal this - it has only one real .bin to work with, so
    // its regenerated second track falls back to its own literal placeholder, which does not exist either.
    fx.tmp.writeFile("Games/Crash Bandicoot/Crash Bandicoot.cue", "FILE \"Crash Bandicoot.bin\" BINARY\n"
                                                                  "  TRACK 01 MODE2/2352\n"
                                                                  "    INDEX 01 00:00:00\n"
                                                                  "FILE \"Crash Bandicoot (Track 2).bin\" BINARY\n"
                                                                  "  TRACK 02 AUDIO\n"
                                                                  "    INDEX 00 00:02:00\n"
                                                                  "    INDEX 01 00:04:00\n");

    ScanUpdate second = fx.runAndPoll();
    CHECK(second.finishedFailedCount == 1);
    CHECK(second.lastFailedGamePath == fx.gamesDir() + ableem::sep + "Crash Bandicoot");
    REQUIRE(second.removedGameIds.size() == 1);
    CHECK(fx.library.usbGames().countGames() == 0);
}

TEST_CASE("checkForChanges: false when unchanged, true once a change is seen twice, false again after a scan") {
    ScanServiceFixture fx;

    CHECK_FALSE(fx.svc.checkForChanges()); // nothing scanned yet, empty dir: nothing changed

    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");
    CHECK_FALSE(fx.svc.checkForChanges()); // changed, but not yet seen twice in a row (debounce)
    CHECK(fx.svc.checkForChanges());       // same state as the previous check: trigger

    fx.runAndPoll();
    CHECK_FALSE(fx.svc.checkForChanges()); // matches what was just scanned again
}

TEST_CASE("start/requestScan/poll over the real worker thread finds a game end to end") {
    ScanServiceFixture fx;
    test_support::makeFakeGame(fx.gamesDir(), "Crash Bandicoot", "SLUS_012.34");

    fx.svc.start();
    CHECK(fx.svc.requestScan());

    ScanUpdate total;
    bool finished = false;
    for (int i = 0; i < 200 && !finished; i++) { // up to ~10s of polling; a tiny fake scan finishes in well under 1s
        ScanUpdate update = fx.svc.poll();
        total.addedGames.insert(total.addedGames.end(), update.addedGames.begin(), update.addedGames.end());
        if (update.finished)
            finished = true;
        if (!finished)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    fx.svc.stop();

    REQUIRE(finished);
    REQUIRE(total.addedGames.size() == 1);
    CHECK(total.addedGames[0]->title == "Crash Bandicoot");
    CHECK(fx.library.usbGames().countGames() == 1);
}

//*******************************
// the RetroArch ROM pass
//*******************************
namespace {

// a fake RetroArch install next to the games: the binary (what retroArchInstalled() looks for), one
// installed core with its .info, and the ROM folders. Pointed at through Env like the platform ini would.
struct RetroArchOnStick {
    explicit RetroArchOnStick(ScanServiceFixture &fx) : fx(fx) {
        fx.tmp.writeFile("retroarch/retroarch", "binary");
        fx.tmp.writeFile("retroarch/info/nestopia_libretro.info",
                         "display_name = \"Nintendo - NES / Famicom (Nestopia UE)\"\n"
                         "supported_extensions = \"nes|fds\"\n"
                         "database = \"Nintendo - Nintendo Entertainment System\"\n");
        fx.tmp.writeFile("retroarch/cores/nestopia_libretro.so", "core");
        fx.tmp.makeSubDir("retroarch/playlists");
        fx.tmp.makeSubDir("roms/Nintendo - Nintendo Entertainment System");
        fx.env.setRetroarchDir(fx.tmp.at("retroarch"));
        fx.env.setRetroarchRomsDir(fx.tmp.at("roms"));
        fx.env.setRetroArchBinaries({fx.tmp.at("retroarch/retroarch")});
    }
    void addRom(const string &name) {
        fx.tmp.writeFile("roms/Nintendo - Nintendo Entertainment System/" + name, "rom");
    }
    string playlist() const { return fx.tmp.at("retroarch/playlists/Nintendo - Nintendo Entertainment System.lpl"); }
    ScanServiceFixture &fx;
};

} // namespace

TEST_CASE("without RetroArch the ROM folders are not looked at, whatever is in them") {
    ScanServiceFixture fx;
    RetroArchOnStick ra(fx);
    ra.addRom("A.nes");
    fx.env.setRetroArchBinaries({}); // RetroArch is optional: no binary, no ROM pass
    CHECK_FALSE(ScanService::romScanEnabled());

    ScanUpdate update = fx.runAndPoll();
    CHECK(update.finished);
    CHECK(update.finishedRomCount == 0);
    CHECK(update.playlistsWritten.empty());
    CHECK_FALSE(ableem::DirEntry::exists(ra.playlist()));
    CHECK(ScanService::fingerprintsMatchDisk()); // the games fingerprint alone decides
}

TEST_CASE("with RetroArch the scan writes a playlist per ROM folder, reports it, and watches the folders") {
    ScanServiceFixture fx;
    RetroArchOnStick ra(fx);
    ra.addRom("A.nes");
    CHECK(ScanService::romScanEnabled());
    CHECK_FALSE(ScanService::fingerprintsMatchDisk());

    ScanUpdate update = fx.runAndPoll();
    CHECK(update.finished);
    CHECK(update.finishedRomCount == 1);
    CHECK(update.playlistsWritten == vector<string>{"Nintendo - Nintendo Entertainment System.lpl"});
    REQUIRE(ableem::DirEntry::exists(ra.playlist()));
    CHECK(ableem::DirEntry::exists(fx.tmp.at("roms.fingerprint")));
    CHECK(ScanService::fingerprintsMatchDisk());

    ableem::RetroArchPlaylistEntries entries;
    REQUIRE(ableem::RetroArchPlaylist::load(ra.playlist(), entries));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].label == "A");
    CHECK(entries[0].core_path == fx.tmp.at("retroarch/cores/nestopia_libretro.so"));

    // nothing changed: the next cycle rewrites nothing
    CHECK_FALSE(fx.svc.checkForChanges());
    CHECK(fx.runAndPoll().playlistsWritten.empty());

    // a ROM copied in is seen by the watcher (twice in a row, as for the games) and by the startup check
    ra.addRom("B.nes");
    CHECK_FALSE(ScanService::fingerprintsMatchDisk());
    CHECK_FALSE(fx.svc.checkForChanges());
    CHECK(fx.svc.checkForChanges());
    ScanUpdate second = fx.runAndPoll();
    CHECK(second.finishedRomCount == 2);
    CHECK(second.playlistsWritten.size() == 1);
    CHECK_FALSE(fx.svc.checkForChanges());
}

TEST_CASE("the online pass fetches the box art of a ROM without one, once, and the launcher is told") {
    ScanServiceFixture fx;
    RetroArchOnStick ra(fx);
    ra.addRom("A.nes");
    fx.tmp.makeSubDir("retroarch/thumbnails");
    fx.tmp.writeFile("retroarch/database/rdb/Atari - 2600.rdb", "RARCHDB"); // databases are there: no bundle fetch

    // a fake network: the probe page and the one cover, served into the file the command names
    vector<string> commands;
    auto runner = [&commands](const string &commandLine) {
        commands.push_back(commandLine);
        size_t sp = commandLine.find(' ');
        size_t sp2 = commandLine.find(' ', sp + 1);
        string url = commandLine.substr(sp + 1, sp2 - sp - 1);
        string out = commandLine.substr(sp2 + 1);
        string expected = OnlineAssets::boxArtUrl("http://thumbs", "Nintendo - Nintendo Entertainment System", "A");
        if (url != "http://thumbs/" && url != expected)
            return 22;
        std::ofstream o(out, std::ios::binary);
        o << "png";
        return 0;
    };
    OnlineAssets::Config online;
    online.downloadCommand = "fetch %u %o";
    online.thumbnailsBaseUrl = "http://thumbs";

    // off: nothing is fetched, nothing is run
    fx.svc.setOnline(false, online, runner);
    ScanUpdate first = fx.runAndPoll();
    CHECK(first.boxArtFetched == 0);
    CHECK(commands.empty());

    fx.svc.setOnline(true, online, runner);
    ScanUpdate second = fx.runAndPoll();
    CHECK(second.boxArtFetched == 1);
    CHECK(ableem::DirEntry::exists(
        fx.tmp.at("retroarch/thumbnails/Nintendo - Nintendo Entertainment System/Named_Boxarts/A.png")));

    size_t before = commands.size();
    ScanUpdate third = fx.runAndPoll();
    CHECK(third.boxArtFetched == 0);
    CHECK(commands.size() == before); // the cover is there: no probe, no fetch
}

//*******************************
// the scanner processors (docs/scanner-processors-plan.md in the launcher), with proc_helper as every one
//*******************************
namespace {

// System/Processors/<name>/ with proc_helper as its program, an ini and the helper's scripts. Every
// processor logs to one file (PROC_LOG, through the ini's Env=), in the order they ran.
struct ProcessorsOnStick {
    explicit ProcessorsOnStick(ScanServiceFixture &f) : fx(f) {}

    void add(const string &name, const string &ini, const vector<std::pair<string, string>> &scripts = {}) {
        string key = Env::appPlatformKeys().front();
        string dir = "System/Processors/" + name;
        fx.tmp.makeSubDir(dir + "/bin/" + key);
        string program = fx.tmp.at(dir + "/bin/" + key + "/" + name);
#ifdef _WIN32
        program += ".exe";
#endif
        REQUIRE(ableem::DirEntry::copy(AB_PROC_HELPER, program));
#ifndef _WIN32
        chmod(program.c_str(), 0755);
#endif
        fx.tmp.writeFile(dir + "/processor.ini",
                         "[Processor]\nExec=bin/{key}/" + name + "\nVersion=1.0\nEnv=PROC_LOG=" + log() + "\n" + ini);
        for (const auto &s : scripts)
            fx.tmp.writeFile(dir + "/" + s.first, s.second);
    }

    string log() const { return fx.tmp.at("proc.log"); }
    // what ran, one "<name> <args>" per line, the paths shortened to what is under the fixture's root
    vector<string> ran() const {
        vector<string> lines;
        std::ifstream in(log());
        string line;
        while (std::getline(in, line)) {
            size_t p;
            while ((p = line.find(fx.tmp.path())) != string::npos)
                line.replace(p, fx.tmp.path().size(), "~");
            lines.push_back(line);
        }
        return lines;
    }
    void clearLog() const { std::remove(log().c_str()); }

    ScanServiceFixture &fx;
};

} // namespace

TEST_CASE("processors: the preprocessor first, then each game's chain in the user's order, then the scan") {
    ScanServiceFixture fx;
    ProcessorsOnStick procs(fx);
    // a real game waiting in a stash, for the fake "converter" to put in place
    string stash = fx.tmp.makeSubDir("stash");
    test_support::makeFakeGame(stash, "Crash", "SLUS_012.34");
    fx.tmp.writeFile("Games/Crash.zip", "zip");

    procs.add("unzip", "Name=Fake unzip\nKinds=games-folder\nMatch=*.zip\nOrder=10\n",
              {{"games.txt", "#Starting - Fake unzip V1\n#Unpacking Crash.zip\n!mkdir {target}/Crash\n"
                             "!move {target}/Crash.zip|{target}/Crash/Crash.rvz\n1/1\n50\n#DONE\n"}});
    procs.add("rvz", "Name=Fake rvz\nKinds=ps1\nMatch=*.rvz\nOrder=20\n",
              {{"ismine.txt", "!has .rvz\n"},
               {"ps1.txt", "#Starting - Fake rvz\n!move " + stash +
                               "/Crash/Crash.bin|{target}/Crash.bin.part\n"
                               "!move {target}/Crash.bin.part|{target}/Crash.bin\n!move " +
                               stash +
                               "/Crash/Crash.cue|{target}/Crash.cue\n!remove {target}/Crash.rvz\n#WARN - kept nothing\n"
                               "#DONE\n"}});
    procs.add("checker", "Kinds=ps1\nModifies=false\nOrder=30\n"); // no Match: every game; the helper's default

    ScanUpdate update = fx.runAndPoll();
    CHECK(procs.ran() == vector<string>{"unzip --start --games ~/Games", "rvz --ismine --ps1 ~/Games/Crash",
                                        "rvz --start --ps1 ~/Games/Crash", "checker --ismine --ps1 ~/Games/Crash",
                                        "checker --start --ps1 ~/Games/Crash"});
    // what the chain made is what the scan read
    CHECK(update.finishedGameCount == 1);
    CHECK(fx.library.usbGames().countGames() == 1);
    CHECK_FALSE(ableem::DirEntry::exists(fx.tmp.at("Games/Crash.zip")));
    // the bubble's last report, and the warning
    CHECK(update.processorProgressed);
    CHECK(update.processor.title == "Fake unzip V1");
    CHECK(update.processor.stage == "Unpacking Crash.zip");
    CHECK(update.processor.percent == 50);
    CHECK(update.processor.total == 1);
    REQUIRE(update.processorNotices.size() == 1);
    CHECK(update.processorNotices[0].title == "Fake rvz");
    CHECK(update.processorNotices[0].item == "Crash");
    CHECK(update.processorNotices[0].message == "kept nothing");
    CHECK_FALSE(update.processorNotices[0].failed);
    // the sequence file was written with the new ones, by Order
    CHECK(fx.tmp.readFile("System/Processors/sequence.ini").find("[ps1]\nunzip\nrvz\nchecker\n") != string::npos);
    CHECK(fx.tmp.readFile("System/Logs/processors.log").find("=== exit 0, ok") != string::npos);

    // nothing changed: the next scan starts nothing at all
    procs.clearLog();
    fx.runAndPoll();
    CHECK(procs.ran().empty());
}

TEST_CASE("processors: the user's order decides, a switched-off one is skipped, a failure stops that game's chain") {
    ScanServiceFixture fx;
    ProcessorsOnStick procs(fx);
    test_support::makeFakeGame(fx.gamesDir(), "Crash", "SLUS_012.34");
    test_support::makeFakeGame(fx.gamesDir(), "Spyro", "SLUS_012.35");
    procs.add("first", "Kinds=ps1\nOrder=1\n");
    procs.add("breaks", "Kinds=ps1\nOrder=2\n",
              {{"ismine.txt", "!has Crash.bin\n"}, {"ps1.txt", "#Starting - breaks\n#ERROR - bad dump\n!exit 1\n"}});
    procs.add("last", "Kinds=ps1\nOrder=3\n");
    procs.add("off", "Kinds=ps1\nOrder=4\n");
    // the user put "last" first and switched "off" off
    fx.tmp.writeFile("System/Processors/sequence.ini", "[ps1]\nlast\nfirst\nbreaks\n-off\n");

    ScanUpdate update = fx.runAndPoll();
    vector<string> ran = procs.ran();
    vector<string> starts;
    for (const string &line : ran) {
        if (line.find("--start") != string::npos)
            starts.push_back(line);
    }
    CHECK(starts == vector<string>{"last --start --ps1 ~/Games/Crash", "first --start --ps1 ~/Games/Crash",
                                   "breaks --start --ps1 ~/Games/Crash", "last --start --ps1 ~/Games/Spyro",
                                   "first --start --ps1 ~/Games/Spyro"});
    REQUIRE(update.processorNotices.size() == 1);
    CHECK(update.processorNotices[0].failed);
    CHECK(update.processorNotices[0].message == "bad dump");
    CHECK(update.processorNotices[0].item == "Crash");
    CHECK(update.finishedGameCount == 2); // the games themselves are still scanned

    // a failure is not retried while nothing changed
    procs.clearLog();
    fx.runAndPoll();
    CHECK(procs.ran().empty());
}

TEST_CASE("processors: suspended for a launch, none that modifies starts; resumed, the next scan catches up") {
    ScanServiceFixture fx;
    ProcessorsOnStick procs(fx);
    test_support::makeFakeGame(fx.gamesDir(), "Crash", "SLUS_012.34");
    procs.add("writer", "Kinds=ps1\n");
    procs.add("reader", "Kinds=ps1\nModifies=false\n");

    fx.svc.setProcessorsSuspended(true);
    ScanUpdate update = fx.runAndPoll();
    CHECK(update.finishedGameCount == 1); // the scan went on without the writer
    CHECK(procs.ran() == vector<string>{"reader --ismine --ps1 ~/Games/Crash", "reader --start --ps1 ~/Games/Crash"});

    procs.clearLog();
    fx.svc.setProcessorsSuspended(false);
    fx.runAndPoll();
    CHECK(procs.ran() == vector<string>{"writer --ismine --ps1 ~/Games/Crash", "writer --start --ps1 ~/Games/Crash"});
}

TEST_CASE("processors: a file only a processor wants is a change the watcher and the startup check see") {
    ScanServiceFixture fx;
    ProcessorsOnStick procs(fx);
    test_support::makeFakeGame(fx.gamesDir(), "Crash", "SLUS_012.34");
    procs.add("unzip", "Kinds=games-folder\nMatch=*.zip\n");
    fx.runAndPoll();
    CHECK(ScanService::fingerprintsMatchDisk());
    CHECK_FALSE(fx.svc.checkForChanges());

    fx.tmp.writeFile("Games/Spyro.zip", "zip");
    CHECK_FALSE(ScanService::fingerprintsMatchDisk());
    CHECK_FALSE(fx.svc.checkForChanges());
    CHECK(fx.svc.checkForChanges()); // seen twice: a scan is due

    // and a half-written *.part is not
    fx.runAndPoll();
    fx.tmp.writeFile("Games/Crash/Crash.bin.part", "half");
    CHECK(ScanService::fingerprintsMatchDisk());
}

TEST_CASE("processors: a ROM chain gets --rom and --system, and what a step made is the next round's") {
    ScanServiceFixture fx;
    RetroArchOnStick ra(fx);
    ProcessorsOnStick procs(fx);
    ra.addRom("Sonic.zip");
    procs.add("unzip", "Kinds=rom\nMatch=*.zip\nOrder=1\n",
              {{"rom.txt", "#Starting - unzip\n!write {target}.nes.part|NES\n"
                           "!move {target}.nes.part|{target}.nes\n!remove {target}\n#DONE\n"}});
    procs.add("patch", "Kinds=rom\nMatch=*.nes\nSystems=Nintendo - Nintendo Entertainment System\nOrder=2\n");

    fx.runAndPoll();
    vector<string> ran = procs.ran();
    REQUIRE(ran.size() == 4);
    CHECK(ran[0].find("unzip --ismine --rom ") == 0);
    CHECK(ran[0].find("Sonic.zip --system Nintendo - Nintendo Entertainment System") != string::npos);
    CHECK(ran[1].find("unzip --start --rom ") == 0);
    CHECK(ran[2].find("patch --ismine --rom ") == 0);
    CHECK(ran[3].find("Sonic.zip.nes --system Nintendo - Nintendo Entertainment System") != string::npos);
}
