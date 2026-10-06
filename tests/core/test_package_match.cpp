//
// PackageService::entriesFor: which games an engine (an App with Uses=) is offered, in what order, and from where
// (docs/packages.md 5.1, 5.4, 6.1).
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/main.h"
#include "core/services/package_service.h"

#include <string>
#include <vector>

using std::string;
using std::vector;

namespace {

const char *const Table = "[doom]\nkind=doom-iwad\ntitle=Doom\nmatch=DOOM.WAD\nmagic=IWAD\n"
                          "[doom2]\nkind=doom-iwad\ntitle=Doom II\nmatch=DOOM2.WAD\nmagic=IWAD\n"
                          "[heretic]\nkind=heretic-iwad\ntitle=Heretic\nmatch=HERETIC.WAD\nmagic=IWAD\n"
                          "[quake]\nkind=quake-id1\ntitle=Quake\nmatch=id1/pak0.pak\n";

struct Rig {
    Rig() : tmp("package_match") {
        tmp.writeFile("rc/packages.ini", Table);
        tmp.makeSubDir("Packages");
    }
    void write(const string &rel, const string &contents = "IWAD") const { tmp.writeFile(rel, contents); }
    void scan() { service.rescan(tmp.at("Packages"), tmp.at("rc/packages.ini")); }
    AppManifest app(const string &uses, const string &packageDir = "") const {
        AppManifest m;
        m.folder = tmp.at("Apps/engine");
        m.uses = AppManifest::parseList(uses, true);
        m.packageDirs = AppManifest::parseList(packageDir, false);
        return m;
    }
    static vector<string> titles(const vector<PackageEntry> &entries) {
        vector<string> out;
        for (const PackageEntry &e : entries)
            out.push_back(e.game.title);
        return out;
    }
    TempDir tmp;
    PackageService service;
};

} // namespace

TEST_CASE("Uses=: kinds are matched in any case and blanks, and an empty Uses= takes no package") {
    Rig r;
    r.write("Packages/Doom/DOOM2.WAD");
    r.write("Packages/Q/id1/pak0.pak", "x");
    r.scan();

    CHECK(Rig::titles(r.service.entriesFor(r.app("doom-iwad"))) == vector<string>{"Doom II"});
    CHECK(Rig::titles(r.service.entriesFor(r.app("  DOOM-IWAD ; "))) == vector<string>{"Doom II"});
    CHECK(Rig::titles(r.service.entriesFor(r.app("quake-id1"))) == vector<string>{"Quake"});
    CHECK(r.service.entriesFor(r.app("")).empty()); // no Uses=: the engine starts as it always did
    CHECK(r.service.entriesFor(r.app("hexen-iwad")).empty());
}

TEST_CASE("Uses=: an engine that runs several kinds is offered the games of all of them") {
    Rig r;
    r.write("Packages/Doom/DOOM2.WAD");
    r.write("Packages/Heretic/HERETIC.WAD");
    r.write("Packages/Q/id1/pak0.pak", "x");
    r.scan();
    CHECK(r.service.entriesFor(r.app("doom-iwad; heretic-iwad")).size() == 2);
    CHECK(r.service.entriesFor(r.app("doom-iwad; heretic-iwad; quake-id1")).size() == 3);
}

TEST_CASE("the order: the kind's place in Uses=, then the title by number, then the package's title") {
    Rig r;
    r.write("Packages/A/DOOM2.WAD");
    r.write("Packages/B/DOOM.WAD");
    r.write("Packages/C/HERETIC.WAD");
    r.write("Packages/D/package.ini", "Title=Level Packs\nKind=doom-iwad\n"
                                      "Game1.Title=Level 10\nGame1.File=l10.wad\n"
                                      "Game2.Title=Level 2\nGame2.File=l2.wad\n"
                                      "Game3.Title=doom ii\nGame3.File=l3.wad\n");
    r.write("Packages/D/l10.wad");
    r.write("Packages/D/l2.wad");
    r.write("Packages/D/l3.wad");
    r.scan();

    // heretic first (it is first in Uses=), then the Doom games by title: "Doom" < "doom ii" = "Doom II" (then the
    // package's title: "A" before "Level Packs") < "Level 2" < "Level 10"
    vector<PackageEntry> entries = r.service.entriesFor(r.app("heretic-iwad; doom-iwad"));
    REQUIRE(entries.size() == 6);
    CHECK(Rig::titles(entries) == vector<string>{"Heretic", "Doom", "Doom II", "doom ii", "Level 2", "Level 10"});
    CHECK(entries[2].packageTitle == "A");
    CHECK(entries[3].packageTitle == "Level Packs");

    // stable between runs
    CHECK(Rig::titles(r.service.entriesFor(r.app("heretic-iwad; doom-iwad"))) == Rig::titles(entries));
    // the other way round
    CHECK(r.service.entriesFor(r.app("doom-iwad; heretic-iwad")).back().game.title == "Heretic");
}

TEST_CASE("an engine's own PackageDir is a source: marked in this App, not in Packages/") {
    Rig r;
    r.write("Packages/Doom/DOOM2.WAD");
    r.write("Apps/engine/WAD/HERETIC.WAD");
    r.write("Apps/engine/WAD/sub/DOOM.WAD");
    r.write("Apps/engine/other/DOOM2.WAD"); // not a PackageDir
    r.scan();

    vector<PackageEntry> entries = r.service.entriesFor(r.app("doom-iwad; heretic-iwad", "WAD"));
    REQUIRE(entries.size() == 3);
    int inApp = 0;
    for (const PackageEntry &e : entries) {
        if (!e.inApp)
            continue;
        inApp++;
        CHECK(e.packageId.compare(0, 5, "e/wad") == 0);
        CHECK(PackageService::sourceName(e.source, e.inApp) == "In this App");
    }
    CHECK(inApp == 2);
    // the Packages row knows nothing of them
    CHECK(r.service.packageCount() == 1);
    // an engine with no PackageDir sees only Packages/
    CHECK(r.service.entriesFor(r.app("doom-iwad; heretic-iwad")).size() == 1);
    // a PackageDir that is not a folder, or tries to leave the App, adds nothing
    CHECK(r.service.entriesFor(r.app("doom-iwad", "nothing; ../x")).size() == 1);
}

TEST_CASE("an unknown kind is kept and shown by its id; unknown data is never offered") {
    Rig r;
    r.write("Packages/Future/package.ini", "Title=Future game\nKind=future-kind\nGame1.Title=F\nGame1.File=f.dat\n");
    r.write("Packages/Future/f.dat");
    r.write("Packages/Stuff/readme.txt", "x");
    r.scan();

    CHECK(PackageService::kindName("future-kind") == "future-kind");
    vector<PackageEntry> entries = r.service.entriesFor(r.app("future-kind"));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].game.kind == "future-kind");
    CHECK(entries[0].id() == "future/game1");
    // an engine that lists no kind of the folders sees nothing - and "Stuff" (unknown data) is never an entry
    CHECK(r.service.entriesFor(r.app("doom-iwad")).empty());
}

TEST_CASE("an entry's files are checked right before the start") {
    Rig r;
    r.write("Packages/Doom/DOOM2.WAD");
    r.scan();
    vector<PackageEntry> entries = r.service.entriesFor(r.app("doom-iwad"));
    REQUIRE(entries.size() == 1);
    CHECK(PackageService::stillThere(entries[0]));
    CHECK(entries[0].file() == r.tmp.at("Packages/Doom/DOOM2.WAD"));
    CHECK(entries[0].root == r.tmp.at("Packages/Doom"));
    DirEntry::removeFile(r.tmp.at("Packages/Doom/DOOM2.WAD"));
    CHECK_FALSE(PackageService::stillThere(entries[0])); // the index is RAM: the file is what says
}

TEST_CASE("the picker's second line and key: the package, the kind and <package>/<game>") {
    Rig r;
    r.write("Packages/Doom/DOOM2.WAD");
    r.scan();
    vector<PackageEntry> entries = r.service.entriesFor(r.app("doom-iwad"));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].id() == "u/doom/doom2");
    CHECK(entries[0].packageTitle == "Doom");
    CHECK(PackageService::kindName(entries[0].game.kind) == "Doom data");
}
