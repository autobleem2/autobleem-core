//
// cleanGameName: a RetroArch game's name without its trailing (...) and [...] tags.
//
#include "doctest/doctest.h"

#include <ableem/engine/clean_game_name.h>

using ableem::cleanGameName;

TEST_CASE("one trailing group goes") {
    CHECK(cleanGameName("Space Invaders (USA)") == "Space Invaders");
    CHECK(cleanGameName("Game [!]") == "Game");
}

TEST_CASE("several groups, of both kinds, go") {
    CHECK(cleanGameName("Sonic (Rev 1) [!]") == "Sonic");
    CHECK(cleanGameName("Zelda (Europe) (En,Fr,De) (Rev A)[b1]") == "Zelda");
}

TEST_CASE("nested groups go as one") {
    CHECK(cleanGameName("Game (USA (Beta))") == "Game");
    CHECK(cleanGameName("Game [a (b) c] (x)") == "Game");
}

TEST_CASE("a name that is only tags stays as it is") {
    CHECK(cleanGameName("(USA)") == "(USA)");
    CHECK(cleanGameName("[!] (Rev 1)") == "[!] (Rev 1)");
    CHECK(cleanGameName("") == "");
}

TEST_CASE("a group in the middle stays, only the trailing ones go") {
    CHECK(cleanGameName("Tom (& Jerry) Run") == "Tom (& Jerry) Run");
    CHECK(cleanGameName("Tom (& Jerry) Run (USA)") == "Tom (& Jerry) Run");
}

TEST_CASE("extra spaces are tidied") {
    CHECK(cleanGameName("  Space   Invaders   (USA)  ") == "Space Invaders");
    CHECK(cleanGameName("Tom  (& Jerry)   Run") == "Tom (& Jerry) Run");
}

TEST_CASE("an unbalanced bracket is left alone") {
    CHECK(cleanGameName("Game (USA") == "Game (USA");
    CHECK(cleanGameName("Game USA)") == "Game USA)");
}

TEST_CASE("a name without tags is unchanged") {
    CHECK(cleanGameName("Pac-Man") == "Pac-Man");
}
