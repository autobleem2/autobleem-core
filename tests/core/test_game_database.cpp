//
// GameDatabase: the incremental-scan support added for ScanService - id/path lookup, updating an existing
// game in place (keeping its id/history/last-played), replacing its disc list, and rewriting the sub-dir
// rows without touching GAME/DISC.
//
#include "doctest/doctest.h"

#include "../support/game_library_fixture.h"
#include "../support/temp_dir.h"

#include <ableem/engine/game_database.h>

#include <string>
#include <vector>

using ableem::GamePaths;
using std::string;
using std::vector;

TEST_CASE("loadGamePaths returns every game's id and folder") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Crash Bandicoot");
    lib.addUsbGame(2, "Spyro");

    GamePaths paths = lib.library.usbGames().loadGamePaths();
    REQUIRE(paths.size() == 2);

    bool sawCrash = false, sawSpyro = false;
    for (const auto &p : paths) {
        if (p.gameId == 1) {
            CHECK(p.path == lib.tmp.at("Games/Crash Bandicoot"));
            sawCrash = true;
        }
        if (p.gameId == 2) {
            CHECK(p.path == lib.tmp.at("Games/Spyro"));
            sawSpyro = true;
        }
    }
    CHECK(sawCrash);
    CHECK(sawSpyro);
}

TEST_CASE("findGameIdByPath finds an existing game and misses one that was never added") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Crash Bandicoot");

    int id = -1;
    CHECK(lib.library.usbGames().findGameIdByPath(lib.tmp.at("Games/Crash Bandicoot"), &id));
    CHECK(id == 1);

    CHECK_FALSE(lib.library.usbGames().findGameIdByPath(lib.tmp.at("Games/Not There"), &id));
}

TEST_CASE("maxGameId is 0 for an empty table and the highest id otherwise") {
    GameLibraryFixture lib;
    CHECK(lib.library.usbGames().maxGameId() == 0);

    lib.addUsbGame(1, "Crash Bandicoot");
    lib.addUsbGame(5, "Spyro");
    CHECK(lib.library.usbGames().maxGameId() == 5);
}

TEST_CASE("updateGame changes the metadata but keeps the id, path and play history") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Crash Bandicoot");
    lib.library.usbGames().updateHistory(1, 3);
    lib.library.usbGames().updateDatePlayed(1, 12345);

    bool ok = lib.library.usbGames().updateGame(1, "Crash Bandicoot: Renamed", "New Publisher", 2, 1998,
                                                lib.tmp.at("Games/Crash Bandicoot/sstates2"), "OTHER");
    CHECK(ok);

    auto games = lib.library.usbGames().loadUsbGames();
    REQUIRE(games.size() == 1);
    CHECK(games[0].gameId == 1);
    CHECK(games[0].title == "Crash Bandicoot: Renamed");
    CHECK(games[0].publisher == "New Publisher");
    CHECK(games[0].players == 2);
    CHECK(games[0].year == 1998);
    CHECK(games[0].memcard == "OTHER");
    CHECK(games[0].folder == lib.tmp.at("Games/Crash Bandicoot")); // updateGame does not touch PATH
    CHECK(games[0].history == 3);                                  // nor HISTORY
    CHECK(games[0].last_played == 12345);                          // nor LAST_PLAYED
}

TEST_CASE("replaceDiscs swaps a game's disc list for a new one") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Twisted Metal"); // one disc, "Twisted Metal", from the fixture

    CHECK(lib.library.usbGames().replaceDiscs(1, {"Twisted Metal (Disc 1)", "Twisted Metal (Disc 2)"}));

    auto games = lib.library.usbGames().loadUsbGames();
    REQUIRE(games.size() == 1);
    CHECK(games[0].cds == 2);
    CHECK(games[0].base == "Twisted Metal (Disc 1)"); // the lowest DISC_NUMBER row
}

TEST_CASE("clearSubDirTables empties SUBDIR_ROWS without touching GAME") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Crash Bandicoot");
    lib.addSubDirRow(0, "Games", 0, 1);
    lib.putGameInSubDirRow(0, 1);

    CHECK(lib.library.usbGames().clearSubDirTables());

    ableem::SubDirRowInfos rows;
    lib.library.usbGames().loadSubDirRows(&rows);
    CHECK(rows.empty());

    CHECK(lib.library.usbGames().countGames() == 1); // GAME/DISC survive
}

TEST_CASE("updateGamePath moves a game's row to another folder, keeping its id and play history") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Crash Bandicoot");
    lib.library.usbGames().updateHistory(1, 2);
    lib.library.usbGames().updateDatePlayed(1, 777);

    string newPath = lib.tmp.at("Games/Platformers/Crash Bandicoot") + "/";
    CHECK(lib.library.usbGames().updateGamePath(1, newPath));

    int id = 0;
    CHECK(lib.library.usbGames().findGameIdByPath(newPath, &id));
    CHECK(id == 1);
    CHECK_FALSE(lib.library.usbGames().findGameIdByPath(lib.tmp.at("Games/Crash Bandicoot") + "/", &id));

    auto games = lib.library.usbGames().loadUsbGames();
    REQUIRE(games.size() == 1);
    CHECK(games[0].history == 2);
    CHECK(games[0].last_played == 777);
}

TEST_CASE("loadDiscNames returns a game's disc file names in disc order, and nothing for an unknown id") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Twisted Metal");
    lib.library.usbGames().replaceDiscs(1, {"Twisted Metal (Disc 1)", "Twisted Metal (Disc 2)"});

    CHECK(lib.library.usbGames().loadDiscNames(1) ==
          vector<string>{"Twisted Metal (Disc 1)", "Twisted Metal (Disc 2)"});
    CHECK(lib.library.usbGames().loadDiscNames(99).empty());
}

TEST_CASE("the column migrations skip a database with no GAME table and create nothing") {
    TempDir tmp("dbmigrate");
    ableem::GameDatabase db;
    REQUIRE(db.open(tmp.at("empty.db")));

    db.addFavoriteColumnIfMissing();
    db.addHistoryColumnIfMissing();
    db.addLastPlayedColumnIfMissing();
    db.addPlayUsingRAColumnIfMissing();
    db.addLightgunColumnIfMissing();

    // the migrations must not have made a GAME table; CREATE then works and has no extra columns
    CHECK(db.execute("CREATE TABLE GAME (GAME_ID integer)", "create GAME"));
    CHECK_FALSE(db.execute("SELECT FAVORITE FROM GAME", "no FAVORITE column"));
}

TEST_CASE("the column migrations add the missing columns to an old GAME table, and again harmlessly") {
    TempDir tmp("dbmigrate");
    ableem::GameDatabase db;
    REQUIRE(db.open(tmp.at("old.db")));
    REQUIRE(db.execute("CREATE TABLE GAME (GAME_ID integer NOT NULL UNIQUE, GAME_TITLE_STRING text, HISTORY integer)",
                       "create the old GAME"));

    for (int round = 0; round < 2; round++) { // the second round finds every column there
        db.addFavoriteColumnIfMissing();
        db.addHistoryColumnIfMissing();
        db.addLastPlayedColumnIfMissing();
        db.addPlayUsingRAColumnIfMissing();
        db.addLightgunColumnIfMissing();
        CHECK(db.execute("SELECT FAVORITE, HISTORY, LAST_PLAYED, PLAY_USING_RA, LIGHTGUN FROM GAME", "all columns"));
    }
}

TEST_CASE("createSchema on a regional.db made before HISTORY and LAST_PLAYED adds them") {
    TempDir tmp("dbmigrate");
    ableem::GameDatabase db;
    REQUIRE(db.open(tmp.at("regional.db")));
    REQUIRE(db.execute("CREATE TABLE GAME (GAME_ID integer NOT NULL UNIQUE, GAME_TITLE_STRING text, "
                       "PUBLISHER_NAME text, RELEASE_YEAR integer, PLAYERS integer, RATING_IMAGE text, "
                       "GAME_MANUAL_QR_IMAGE text, LINK_GAME_ID integer, PATH text, SSPATH text, MEMCARD text, "
                       "PRIMARY KEY (GAME_ID))",
                       "create the old GAME"));

    CHECK(db.createSchema());
    CHECK(db.execute("SELECT HISTORY, LAST_PLAYED FROM GAME", "history columns"));
}
