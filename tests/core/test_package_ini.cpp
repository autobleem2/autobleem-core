//
// PackageService::readDescriptor: package.ini, the descriptor of one of our packages (docs/packages.md 2.2).
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/services/package_service.h"

#include <string>
#include <vector>

using std::string;
using std::vector;

namespace {

// a package folder with the given package.ini and the named files (each one byte)
struct Pkg {
    explicit Pkg(const string &ini, const vector<string> &files = {}) : tmp("package_ini") {
        tmp.writeFile("p/package.ini", ini);
        for (const string &f : files)
            tmp.writeFile("p/" + f, "x");
    }
    bool read(PackageInfo &out, string *why = nullptr) {
        string problem;
        const bool ok = PackageService::readDescriptor(tmp.at("p"), out, problem);
        if (why != nullptr)
            *why = problem;
        return ok;
    }
    TempDir tmp;
};

} // namespace

TEST_CASE("package.ini: every key of the descriptor") {
    Pkg p("[package]\n"
          "Title=Freedoom\n"
          "Kind=doom-iwad\n"
          "Id=freedoom\n"
          "Version=0.13.0-1\n"
          "Licence=BSD-3-Clause\n"
          "Author=The Freedoom project\n"
          "Description=Free game data for Doom engines.\n"
          "Image=cover.png\n"
          "Readme=README.txt\n"
          "Source=store\n"
          "StoreId=pkg/freedoom\n"
          "PeSource=freedoomdata.mod\n"
          "Replaces=pe-freedoomdata; pe-other\n"
          "Game1.Id=freedoom1\n"
          "Game1.Title=Freedoom: Phase 1\n"
          "Game1.File=freedoom1.wad\n"
          "Game1.Variant=Phase 1\n"
          "Game2.Id=freedoom2\n"
          "Game2.Title=Freedoom: Phase 2\n"
          "Game2.File=freedoom2.wad\n",
          {"freedoom1.wad", "freedoom2.wad", "cover.png", "README.txt"});
    PackageInfo info;
    REQUIRE(p.read(info));
    CHECK(info.title == "Freedoom");
    CHECK(info.id == "freedoom");
    CHECK(info.version == "0.13.0-1");
    CHECK(info.licence == "BSD-3-Clause");
    CHECK(info.author == "The Freedoom project");
    CHECK(info.description == "Free game data for Doom engines.");
    CHECK(info.image == p.tmp.at("p/cover.png"));
    CHECK(info.readme == p.tmp.at("p/README.txt"));
    CHECK(info.source == "store");
    CHECK(info.storeId == "pkg/freedoom");
    CHECK(info.peSource == "freedoomdata.mod");
    CHECK(info.replaces == vector<string>{"pe-freedoomdata", "pe-other"});
    CHECK(info.descriptor);
    REQUIRE(info.games.size() == 2);
    CHECK(info.games[0].id == "freedoom1");
    CHECK(info.games[0].title == "Freedoom: Phase 1");
    CHECK(info.games[0].file == "freedoom1.wad");
    CHECK(info.games[0].variant == "Phase 1");
    CHECK(info.games[0].kind == "doom-iwad");
    CHECK(info.games[1].id == "freedoom2");
    CHECK(info.kinds() == vector<string>{"doom-iwad"});
}

TEST_CASE("package.ini: defaults - the id from the folder, game ids game<N>, Source user") {
    Pkg p("Title=My Game\nKind=dos-game\nGame1.Title=One\nGame1.File=a.exe\nGame2.Title=Two\nGame2.File=b.exe\n",
          {"a.exe", "b.exe"});
    PackageInfo info;
    REQUIRE(p.read(info));
    CHECK(info.id == "p"); // the folder is called "p"
    CHECK(info.source == "user");
    CHECK(info.games[0].id == "game1");
    CHECK(info.games[1].id == "game2");
    CHECK(info.image.empty());
}

TEST_CASE("package.ini: the default id is the folder name, lower case, other characters a dash") {
    TempDir tmp("package_ini_id");
    tmp.writeFile("My Game_2!/package.ini", "Title=T\nKind=k\nGame1.Title=G\nGame1.File=g.dat\n");
    tmp.writeFile("My Game_2!/g.dat", "x");
    PackageInfo info;
    string problem;
    REQUIRE(PackageService::readDescriptor(tmp.at("My Game_2!"), info, problem));
    CHECK(info.id == "my-game-2");
}

TEST_CASE("package.ini: CRLF, a BOM, comments anywhere on a line, the [package] header ignored, keys in any case") {
    Pkg p("\xEF\xBB\xBF[package]\r\n"
          "# a comment line\r\n"
          "TITLE = Prince of Persia   # the title cannot hold a hash\r\n"
          "kind=DOS-Game\r\n"
          "game1.title=Prince\r\n"
          "GAME1.FILE=PRINCE.EXE\r\n",
          {"PRINCE.EXE"});
    PackageInfo info;
    REQUIRE(p.read(info));
    CHECK(info.title == "Prince of Persia");
    CHECK(info.games[0].kind == "dos-game"); // lower-cased
}

TEST_CASE("package.ini: a gap in the numbering ends the list; a missing file drops that game only") {
    Pkg p("Title=T\nKind=k\n"
          "Game1.Title=One\nGame1.File=one.dat\n"
          "Game2.Title=Two\nGame2.File=missing.dat\n"
          "Game3.Title=Three\nGame3.File=three.dat\n"
          "Game5.Title=Five\nGame5.File=five.dat\n", // Game4 has no Title: the list stops there
          {"one.dat", "three.dat", "five.dat"});
    PackageInfo info;
    REQUIRE(p.read(info));
    REQUIRE(info.games.size() == 2);
    CHECK(info.games[0].title == "One");
    CHECK(info.games[1].title == "Three");
    CHECK(info.games[1].id == "game3"); // the number it was written with
}

TEST_CASE("package.ini: a path that escapes the package drops the game") {
    Pkg p("Title=T\nKind=k\n"
          "Game1.Title=Up\nGame1.File=../outside.dat\n"
          "Game2.Title=Abs\nGame2.File=/etc/passwd\n"
          "Game3.Title=Drive\nGame3.File=C:/x.dat\n"
          "Game4.Title=Back\nGame4.File=sub\\..\\..\\x.dat\n"
          "Game5.Title=Fine\nGame5.File=sub/fine.dat\n",
          {"sub/fine.dat"});
    p.tmp.writeFile("outside.dat", "x");
    PackageInfo info;
    REQUIRE(p.read(info));
    REQUIRE(info.games.size() == 1);
    CHECK(info.games[0].title == "Fine");
    CHECK(info.games[0].file == "sub/fine.dat");
}

TEST_CASE("package.ini: a bad kind or id is rejected") {
    Pkg p("Title=T\nKind=k\n"
          "Game1.Title=BadKind\nGame1.File=a.dat\nGame1.Kind=Not A Kind\n"
          "Game2.Title=BadId\nGame2.File=a.dat\nGame2.Id=Bad Id\n"
          "Game3.Title=Same\nGame3.File=a.dat\nGame3.Id=ok-id\n"
          "Game4.Title=Again\nGame4.File=a.dat\nGame4.Id=ok-id\n" // the id is taken
          "Game5.Title=Last\nGame5.File=a.dat\n",
          {"a.dat"});
    PackageInfo info;
    REQUIRE(p.read(info));
    REQUIRE(info.games.size() == 2);
    CHECK(info.games[0].title == "Same");
    CHECK(info.games[1].title == "Last");

    Pkg q("Title=T\nKind=k\nId=Not Valid\nGame1.Title=G\nGame1.File=a.dat\n", {"a.dat"});
    string why;
    CHECK_FALSE(q.read(info, &why));
    CHECK(why.find("id") != string::npos);

    Pkg none("Title=T\nGame1.Title=G\nGame1.File=a.dat\n", {"a.dat"}); // no Kind anywhere: no game left
    CHECK_FALSE(none.read(info, &why));
}

TEST_CASE("package.ini: a Title is required, and a package with no game is not a package") {
    string why;
    PackageInfo info;
    Pkg noTitle("Kind=k\nGame1.Title=G\nGame1.File=a.dat\n", {"a.dat"});
    CHECK_FALSE(noTitle.read(info, &why));
    Pkg noGames("Title=T\nKind=k\n");
    CHECK_FALSE(noGames.read(info, &why));
    Pkg allGone("Title=T\nKind=k\nGame1.Title=G\nGame1.File=gone.dat\n");
    CHECK_FALSE(allGone.read(info, &why));
    TempDir empty("package_ini_empty");
    empty.makeSubDir("p");
    CHECK_FALSE(PackageService::readDescriptor(empty.at("p"), info, why));
}

TEST_CASE("package.ini: a file over 64 KB is refused") {
    string big = "Title=T\nKind=k\nGame1.Title=G\nGame1.File=a.dat\n";
    big += "# " + string(70 * 1024, 'x') + "\n";
    Pkg p(big, {"a.dat"});
    PackageInfo info;
    string why;
    CHECK_FALSE(p.read(info, &why));
    CHECK(why.find("64 KB") != string::npos);
}

TEST_CASE("package.ini: the kind list and a game's own kind") {
    Pkg p("Title=My id Software games\nKind=doom-iwad; Quake-ID1\n"
          "Game1.Title=Doom II\nGame1.File=Doom/DOOM2.WAD\n"
          "Game2.Title=Quake\nGame2.Kind=quake-id1\nGame2.File=Quake/id1/pak0.pak\n"
          "Game3.Title=Mod\nGame3.Kind=hexen-iwad\nGame3.File=h.wad\n",
          {"Doom/DOOM2.WAD", "Quake/id1/pak0.pak", "h.wad"});
    PackageInfo info;
    REQUIRE(p.read(info));
    REQUIRE(info.games.size() == 3);
    CHECK(info.games[0].kind == "doom-iwad"); // no Kind on the game: the first of the package's
    CHECK(info.games[1].kind == "quake-id1");
    CHECK(info.games[2].kind == "hexen-iwad"); // a kind the package does not list: the game's own is kept
    CHECK(info.kinds() == vector<string>{"doom-iwad", "quake-id1", "hexen-iwad"});
}

TEST_CASE("package.ini: Start programs, Dosbox settings and a mapper (a dos-game)") {
    Pkg p("Title=Prince of Persia\nKind=dos-game\n"
          "Game1.Title=Prince of Persia\nGame1.File=PRINCE.EXE\n"
          "Game1.Start1.File=PRINCE.EXE\nGame1.Start1.Title=Play\n"
          "Game1.Start2.File=setup.exe\nGame1.Start2.Title=Setup\n"
          "Game1.Start3.File=nothing.exe\nGame1.Start3.Title=Gone\n"
          "Game1.Dosbox.Cycles=3000\nGame1.Dosbox.Memsize=16\n"
          "Game1.Mapper=keys/prince.map\n",
          {"PRINCE.EXE", "SETUP.EXE", "keys/prince.map"});
    PackageInfo info;
    REQUIRE(p.read(info));
    const PackageGame &g = info.games[0];
    REQUIRE(g.starts.size() == 2); // the third program is not there
    CHECK(g.starts[0].file == "PRINCE.EXE");
    CHECK(g.starts[0].title == "Play");
    CHECK(g.starts[1].file == "SETUP.EXE"); // the real spelling on disk
    REQUIRE(g.settings.size() == 2);
    CHECK(g.settings[0].first == "cycles");
    CHECK(g.settings[0].second == "3000");
    CHECK(g.settings[1].first == "memsize");
    CHECK(g.mapper == "keys/prince.map");
}

TEST_CASE("package.ini: names are resolved case-insensitively and keep the disk's spelling") {
    Pkg p("Title=T\nKind=k\nGame1.Title=G\nGame1.File=data/game.DAT\n", {"Data/Game.dat"});
    PackageInfo info;
    REQUIRE(p.read(info));
    CHECK(info.games[0].file == "Data/Game.dat");
}

TEST_CASE("package.ini: Replaces names plain folders only, an unknown Source is user, a long title is cut") {
    Pkg p("Title=" + string(120, 'T') +
              "\nKind=k\nSource=weird\nReplaces=../etc; a/b; .hidden; ok-app\n"
              "Description=" +
              string(300, 'd') + "\nGame1.Title=G\nGame1.File=a.dat\n",
          {"a.dat"});
    PackageInfo info;
    REQUIRE(p.read(info));
    CHECK(info.title.size() == 80);
    CHECK(info.description.size() == 200);
    CHECK(info.source == "user");
    CHECK(info.replaces == vector<string>{"ok-app"});
}
