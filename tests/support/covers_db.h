//
// makeCoversDbWithCover: a covers<region>.db with the real schema and one game, for tests that need a cover blob
// of a chosen format (a PNG or a JPEG) to come out of the covers database.
//
#pragma once

#include "temp_dir.h"

#include "doctest/doctest.h"

#include <ableem/engine/game_database.h>

#include <string>

namespace test_support {

// the first bytes of a PNG / of a JPEG, as the hex of an SQL blob literal
const char CoverPngHex[] = "89504E470D0A1A0A";
const char CoverJpegHex[] = "FFD8FFE000104A464946";

// <tmp>/<dir>/covers<region>.db with one game whose serial is `serial` and whose COVER is the blob `coverHex`
inline void makeCoversDbWithCover(const TempDir &tmp, const std::string &dir, const std::string &region,
                                  const std::string &serial, const std::string &coverHex) {
    tmp.makeSubDir(dir);
    ableem::GameDatabase db;
    REQUIRE(db.open(tmp.at(dir + "/covers" + region + ".db")));
    REQUIRE(db.execute("CREATE TABLE GAME (ID INTEGER NOT NULL UNIQUE, TITLE TEXT NOT NULL, PUBLISHER TEXT NOT NULL, "
                       "RELEASE INTEGER NOT NULL, PLAYERS INTEGER NOT NULL, COVER BLOB, PRIMARY KEY(ID))",
                       "create GAME"));
    REQUIRE(db.execute("CREATE TABLE SERIALS (SERIAL TEXT NOT NULL, GAME INTEGER NOT NULL, PRIMARY KEY(SERIAL))",
                       "create SERIALS"));
    const std::string insert = "INSERT INTO GAME VALUES (1, 'Db Title', 'Db Publisher', 1999, 2, X'" + coverHex + "')";
    REQUIRE(db.execute(insert.c_str(), "insert GAME"));
    const std::string serials = "INSERT INTO SERIALS VALUES ('" + serial + "', 1)";
    REQUIRE(db.execute(serials.c_str(), "insert SERIALS"));
}

} // namespace test_support
