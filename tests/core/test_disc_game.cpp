//
// DiscGameService: a disc in a drive as a game whose saves belong to its serial.
//
#include "doctest/doctest.h"

#include "../support/game_library_fixture.h"
#include "../support/rdb_builder.h"

#include "core/services/disc_game.h"
#include "core/services/game_settings.h"

#include <ableem/engine/config_file_editor.h>
#include <ableem/engine/environment.h>
#include <ableem/engine/game_database.h>
#include <ableem/engine/ini_file.h>

#include <string>

using namespace test_support;
using ableem::DirEntry;
using std::string;

namespace {

// Tekken 3 on two discs, Crash on one; Metal Gear's disc 1 is missing from the rdb
string makeRdbFile(const TempDir &tmp) {
    Bytes records;
    appendGameRecord(records, "Tekken 3 (Europe) (Disc 1)", "SCES-01237", "Europe", "Namco", 1998, 2);
    appendGameRecord(records, "Tekken 3 (Europe) (Disc 2)", "SCES-11237", "Europe", "Namco", 1998, 2);
    appendGameRecord(records, "Crash Bandicoot (USA)", "SCUS-94900", "USA", "Sony", 1996, 1);
    appendGameRecord(records, "Metal Gear Solid (USA) (Disc 2)", "SLUS-00776", "USA", "Konami", 1998, 1);
    records.push_back(0xc0);
    return writeRdb(tmp, "Sony - PlayStation.rdb", makeRdb(0, records));
}

// covers U db with the real schema and one game, its cover four bytes
void makeCoversDb(const TempDir &tmp, const string &serial) {
    ableem::GameDatabase db;
    REQUIRE(db.open(tmp.at("db/coversU.db")));
    REQUIRE(db.execute("CREATE TABLE GAME (ID INTEGER NOT NULL UNIQUE, TITLE TEXT NOT NULL, PUBLISHER TEXT NOT NULL, "
                       "RELEASE INTEGER NOT NULL, PLAYERS INTEGER NOT NULL, COVER BLOB, PRIMARY KEY(ID))",
                       "create GAME"));
    REQUIRE(db.execute("CREATE TABLE SERIALS (SERIAL TEXT NOT NULL, GAME INTEGER NOT NULL, PRIMARY KEY(SERIAL))",
                       "create SERIALS"));
    REQUIRE(db.execute("INSERT INTO GAME VALUES (1, 'Crash Bandicoot', 'Sony', 1996, 1, X'89504E47')", "insert GAME"));
    string insert = "INSERT INTO SERIALS VALUES ('" + serial + "', 1)";
    REQUIRE(db.execute(insert.c_str(), "insert SERIALS"));
}

struct Rig {
    GameLibraryFixture lib;
    string thumbDir;
    ableem::MetadataLookup metadata;
    DiscGameService service;
    explicit Rig(bool coversDb = false)
        : thumbDir(setUpThumbnails(lib, coversDb)), metadata(lib.tmp.at("db"), makeRdbFile(lib.tmp)),
          service(lib.library, metadata) {}

    // RetroArch's thumbnails tree under the temp stick (and the covers db when asked), before the lookups read paths
    static string setUpThumbnails(GameLibraryFixture &lib, bool coversDb) {
        lib.env.setRetroarchDir(lib.tmp.at("retroarch"));
        if (coversDb)
            makeCoversDb(lib.tmp, "SCUS-94900");
        return lib.tmp.makeSubDir("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts");
    }

    void addLibraryGame(int id, const string &title, const string &serial) {
        lib.addUsbGame(id, title, "MyCard");
        lib.tmp.writeFile("Games/" + title + "/" + ableem::GAME_INI,
                          "[Game]\nAutomation=1\nSerial=" + serial + "\n");
    }
};

} // namespace

TEST_CASE("a disc whose serial is in the library plays with that game's folder, card and title row") {
    Rig rig;
    rig.addLibraryGame(1, "Crash Bandicoot", "SCUS-94900");

    DiscGame disc = rig.service.make("SCUS-94900", "U", "CRASH", 3);
    CHECK(disc.sharesLibraryGame);
    CHECK(disc.physicalDisc);
    CHECK(disc.sgIndex == 3);
    CHECK(disc.game->ssFolder == rig.lib.tmp.at("Games/Crash Bandicoot/sstates"));
    CHECK(disc.game->memcard == "MyCard");
    CHECK(disc.game->gameId == 1);
}

TEST_CASE("disc 2 of a set shares the folder of the library game holding disc 1") {
    Rig rig;
    rig.addLibraryGame(7, "Tekken 3", "SCES-01237");

    CHECK(rig.service.keySerial("SCES-11237") == "SCES-01237");
    CHECK(rig.service.keySerial("SCES-01237") == "SCES-01237");

    DiscGame disc2 = rig.service.make("SCES-11237", "P", "TEKKEN3_2", 0);
    CHECK(disc2.sharesLibraryGame);
    CHECK(disc2.keySerial == "SCES-01237");
    CHECK(disc2.game->ssFolder == rig.lib.tmp.at("Games/Tekken 3/sstates"));
    CHECK(disc2.game->serial == "SCES-11237"); // its own serial is still what the disc says
}

TEST_CASE("an unknown serial gets disc-<serial> in the save states dir, created only by ensureFolder") {
    Rig rig;
    rig.addLibraryGame(1, "Crash Bandicoot", "SCUS-94900");

    DiscGame disc = rig.service.make("SLES-55555", "P", "SOMEGAME", 1);
    CHECK_FALSE(disc.sharesLibraryGame);
    string expected = ableem::Environment::getPathToSaveStatesDir() + ableem::sep + "disc-SLES-55555/";
    CHECK(disc.game->ssFolder == expected);
    CHECK_FALSE(DirEntry::exists(expected));

    REQUIRE(disc.ensureFolder());
    CHECK(DirEntry::exists(expected));
}

TEST_CASE("a new set's disc 2 uses the folder named for disc 1's serial") {
    Rig rig;
    DiscGame disc2 = rig.service.make("SCES-11237", "P", "TEKKEN3_2", 0);
    CHECK_FALSE(disc2.sharesLibraryGame);
    CHECK(disc2.game->ssFolder == DiscGameService::folderForKey("SCES-01237"));
    CHECK_FALSE(DirEntry::exists(disc2.game->ssFolder));
}

TEST_CASE("a disc 2 whose disc 1 the rdb lacks is its own set") {
    Rig rig;
    CHECK(rig.service.keySerial("SLUS-00776") == "SLUS-00776");
}

TEST_CASE("metadata comes from the lookup; the volume label is the title when nothing knows the serial") {
    Rig rig;
    DiscGame known = rig.service.make("SCUS-94900", "", "CRASH", 0);
    CHECK(known.game->title == "Crash Bandicoot");
    CHECK(known.game->publisher == "Sony");
    CHECK(known.game->year == 1996);
    CHECK(known.game->players == 1);
    CHECK(known.game->region == "US"); // from the serial when the caller had none

    DiscGame unknown = rig.service.make("SLES-55555", "P", "SOME GAME", 0);
    CHECK(unknown.game->title == "SOME GAME");
    CHECK(unknown.game->region == "P");
    CHECK(unknown.cover.empty());
}

TEST_CASE("no covers db: the thumbnails tree's box art, by the rdb's record name, is the cover") {
    Rig rig;
    rig.lib.tmp.writeFile("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts/Crash Bandicoot (USA).png", "BOXART");

    DiscGame disc = rig.service.make("SCUS-94900", "U", "CRASH", 0);
    CHECK(string(disc.cover.begin(), disc.cover.end()) == "BOXART");
}

TEST_CASE("no covers db and no thumbnail: no cover, the caller falls back to default.png") {
    Rig rig;
    DiscGame disc = rig.service.make("SCUS-94900", "U", "CRASH", 0);
    CHECK(disc.inDatabase);
    CHECK(disc.cover.empty());

    DiscGame unknown = rig.service.make("SLES-55555", "P", "SOME GAME", 0);
    CHECK(unknown.cover.empty());
}

TEST_CASE("the covers db's picture wins over the thumbnails tree") {
    Rig rig(true);
    rig.lib.tmp.writeFile("retroarch/thumbnails/Sony - PlayStation/Named_Boxarts/Crash Bandicoot (USA).png", "BOXART");

    DiscGame disc = rig.service.make("SCUS-94900", "U", "CRASH", 0);
    CHECK(string(disc.cover.begin(), disc.cover.end()) == "\x89PNG");
}

TEST_CASE("a disc no library game shares gets its pcsx.cfg, Game.ini and cover when the folder is made, once") {
    Rig rig;
    DiscGame disc = rig.service.make("SCUS-94900", "U", "CRASH", 0);
    REQUIRE_FALSE(disc.sharesLibraryGame);
    CHECK(disc.game->physicalDisc);
    disc.cover = {'P', 'N', 'G'};

    REQUIRE(disc.ensureFolder());
    const string folder = disc.game->folder;
    string cfg;
    REQUIRE(DirEntry::readFile(folder + "pcsx.cfg", cfg));
    CHECK(cfg.find("SlowBoot = 1") != string::npos);
    ableem::IniFile ini;
    ini.load(folder + ableem::GAME_INI);
    CHECK(ini.values["title"] == "Crash Bandicoot");
    CHECK(ini.values["serial"] == "SCUS-94900");
    CHECK(ini.values["memcard"] == "SONY");
    string png;
    REQUIRE(DirEntry::readFile(folder + "cover.png", png));
    CHECK(png == "PNG");

    // what is there stays: an edit survives the next launch
    ableem::ConfigFileEditor().replaceInFile(folder + "pcsx.cfg", "SlowBoot", "SlowBoot = 0");
    REQUIRE(disc.ensureFolder());
    REQUIRE(DirEntry::readFile(folder + "pcsx.cfg", cfg));
    CHECK(cfg.find("SlowBoot = 0") != string::npos);
}

TEST_CASE("a disc that shares a library game gets no files of its own") {
    Rig rig;
    rig.addLibraryGame(1, "Crash Bandicoot", "SCUS-94900");
    DiscGame disc = rig.service.make("SCUS-94900", "U", "CRASH", 0);
    disc.cover = {'P', 'N', 'G'};
    REQUIRE(disc.ensureFolder());
    CHECK_FALSE(DirEntry::exists(disc.game->folder + "/cover.png"));
}

TEST_CASE("the game options of a seeded disc save a dithering change in its pcsx.cfg") {
    Rig rig;
    DiscGame disc = rig.service.make("SLES-55555", "P", "SOMEGAME", 0);
    REQUIRE(disc.ensureFolder());

    GameSettingsService settings(rig.lib.library);
    GameSettings s = settings.open(disc.game);
    settings.setDithering(s, 2);
    CHECK_FALSE(s.cfgMissing);
    CHECK(s.pcsx.dither == 2);
    string cfg;
    REQUIRE(DirEntry::readFile(disc.game->folder + "pcsx.cfg", cfg));
    CHECK(cfg.find("dithering2 = 2") != string::npos);
}

TEST_CASE("without a pcsx.cfg a setting is reported as not saved") {
    Rig rig;
    DiscGame disc = rig.service.make("SLES-55555", "P", "SOMEGAME", 0);
    REQUIRE(DirEntry::createDirs(disc.game->ssFolder)); // the folder, not ensureFolder's files

    GameSettingsService settings(rig.lib.library);
    GameSettings s = settings.open(disc.game);
    settings.setDithering(s, 2);
    CHECK(s.cfgMissing);
    CHECK_FALSE(DirEntry::exists(disc.game->folder + "pcsx.cfg"));
}
