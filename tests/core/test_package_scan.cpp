//
// PackageService: the scan of Packages/ into the RAM index (docs/packages.md 2.1, 4.4) - what is recognised from
// the table, what is unknown data, the limits, and that nothing is ever created or written.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "../support/tree_snapshot.h"
#include "core/main.h"
#include "core/services/package_service.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using std::string;
using std::vector;

namespace {

// the shipped table of these tests: the shapes of the real one, with sizes small enough to write
const char *const Table = "# autobleem-packages 1\n"
                          "[doom]\nkind=doom-iwad\ntitle=Doom\nmatch=DOOM.WAD\nmagic=IWAD\n"
                          "[doom2]\nkind=doom-iwad\ntitle=Doom II\nmatch=DOOM2.WAD\nmagic=IWAD\n"
                          "[quake]\nkind=quake-id1\ntitle=Quake\nmatch=id1/pak0.pak;id1/pak1.pak\n"
                          "[quake-shareware]\nkind=quake-id1\ntitle=Quake (Shareware)\nmatch=id1/pak0.pak\nsize=8\n"
                          "[quake-id1]\nkind=quake-id1\ntitle=Quake (id1 data)\nmatch=id1/pak0.pak\n"
                          "[prince]\nkind=dos-game\ntitle=Prince of Persia\nmatch=PRINCE.EXE\n"
                          "start=PRINCE.EXE|Play;SETUP.EXE|Setup\nset.cycles=3000\nmapper=keys.map\n";

struct Stick {
    Stick() : tmp("package_scan") {
        tmp.writeFile("rc/packages.ini", Table);
        tmp.makeSubDir("Packages");
    }
    string packages() const { return tmp.at("Packages"); }
    string table() const { return tmp.at("rc/packages.ini"); }
    void write(const string &rel, const string &contents = "x") const { tmp.writeFile("Packages/" + rel, contents); }
    PackageService &scan(const PackageService::Limits &limits = PackageService::Limits()) {
        service.rescan(packages(), table(), limits);
        return service;
    }
    std::shared_ptr<PackageInfo> find(const string &id) {
        for (const PackageInfo &p : service.packages())
            if (p.id == id)
                return std::make_shared<PackageInfo>(p);
        return nullptr;
    }
    TempDir tmp;
    PackageService service;
};

vector<string> ids(const vector<PackageInfo> &packages) {
    vector<string> out;
    for (const PackageInfo &p : packages)
        out.push_back(p.id);
    return out;
}

} // namespace

TEST_CASE("PackageService: the limits are the spec's") {
    CHECK(PackageService::Limits().maxDepth == 4);
    CHECK(PackageService::Limits().maxFolders == 2000);
}

TEST_CASE("PackageService: DOOM.WAD and DOOM2.WAD in one folder are two packages, one game each") {
    Stick s;
    s.write("Doom/DOOM.WAD", "IWAD-doom");
    s.write("Doom/DOOM2.WAD", "IWAD-doom2");
    s.scan();

    vector<PackageInfo> packages = s.service.packages();
    REQUIRE(packages.size() == 2);
    CHECK(s.service.packageCount() == 2);
    CHECK(packages[0].id == "u/doom/doom.wad");
    CHECK(packages[0].title == "Doom");
    CHECK(packages[0].source == "user");
    CHECK_FALSE(packages[0].descriptor);
    CHECK(packages[0].root == s.tmp.at("Packages/Doom")); // both live in the folder the files are in
    REQUIRE(packages[0].games.size() == 1);
    CHECK(packages[0].games[0].id == "doom");
    CHECK(packages[0].games[0].file == "DOOM.WAD");
    CHECK(packages[0].games[0].kind == "doom-iwad");
    CHECK(packages[1].id == "u/doom/doom2.wad");
    CHECK(packages[1].title == "Doom II");
    REQUIRE(packages[1].games.size() == 1);
    CHECK(packages[1].games[0].id == "doom2");

    // the picker still lists both games, each under its own package
    AppManifest app;
    app.folder = s.tmp.at("Apps/crispy");
    app.uses = {"doom-iwad"};
    vector<PackageEntry> entries = s.service.entriesFor(app);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].id() == "u/doom/doom.wad/doom");
    CHECK(entries[1].id() == "u/doom/doom2.wad/doom2");
}

TEST_CASE("PackageService: a data dir stays one package, and loose files beside it are packages of their own") {
    Stick s;
    s.write("Mixed/DOOM.WAD", "IWAD");
    s.write("Mixed/DOOM2.WAD", "IWAD");
    s.write("Mixed/id1/pak0.pak", "x");
    s.write("Mixed/id1/pak1.pak", "x");
    s.write("OneFile/DOOM2.WAD", "IWAD");
    s.scan();

    std::shared_ptr<PackageInfo> quake = s.find("u/mixed");
    REQUIRE(quake != nullptr); // the data dir's package keeps the folder's id and name
    CHECK(quake->title == "Mixed");
    REQUIRE(quake->games.size() == 1);
    CHECK(quake->games[0].id == "quake");
    CHECK(s.find("u/mixed/doom.wad") != nullptr);
    CHECK(s.find("u/mixed/doom2.wad") != nullptr);
    // a folder with a single file is as it always was
    REQUIRE(s.find("u/onefile") != nullptr);
    CHECK(s.find("u/onefile")->title == "OneFile");
    CHECK(s.service.packageCount() == 4);
}

TEST_CASE("PackageService: names in any letter case match, and the real spelling is what the engine gets") {
    Stick s;
    s.write("Q/Id1/Pak0.Pak", "x");
    s.write("Q/Id1/PAK1.pak", "x");
    s.write("D/doom.wad", "IWAD");
    s.write("E/Doom2.Wad", "IWAD");
    s.scan();

    std::shared_ptr<PackageInfo> quake = s.find("u/q");
    REQUIRE(quake != nullptr);
    REQUIRE(quake->games.size() == 1);
    CHECK(quake->games[0].id == "quake");
    CHECK(quake->games[0].file == "Id1/Pak0.Pak"); // not "id1/pak0.pak"
    std::shared_ptr<PackageInfo> d = s.find("u/d");
    REQUIRE(d != nullptr);
    CHECK(d->games[0].file == "doom.wad");
    CHECK(s.find("u/e")->games[0].file == "Doom2.Wad");

    PackageEntry e;
    e.root = quake->root;
    e.game = quake->games[0];
    CHECK(e.file() == s.tmp.at("Packages/Q/Id1/Pak0.Pak"));
}

TEST_CASE("PackageService: Quake - the full game, then the shareware by size, then the generic row") {
    Stick s;
    s.write("Full/id1/pak0.pak", "12345678");
    s.write("Full/id1/pak1.pak", "x");
    s.write("Demo/id1/pak0.pak", "12345678"); // 8 bytes: the shareware row's size
    s.write("Other/id1/pak0.pak", "123");     // any other size: the generic row
    s.scan();

    CHECK(s.find("u/full")->games[0].id == "quake");
    CHECK(s.find("u/demo")->games[0].id == "quake-shareware");
    CHECK(s.find("u/other")->games[0].id == "quake-id1");
    // one game per root and main file: the full game's folder does not also list the generic row
    CHECK(s.find("u/full")->games.size() == 1);
}

TEST_CASE("PackageService: magic is checked after the names matched - a mismatch does not match that row") {
    Stick s;
    s.write("Fake/DOOM.WAD", "NOPE-not-an-iwad");
    s.write("Real/DOOM.WAD", "IWAD");
    s.scan();

    std::shared_ptr<PackageInfo> fake = s.find("u/fake");
    REQUIRE(fake != nullptr);
    CHECK(fake->unknown); // nothing matched: data we do not know
    std::shared_ptr<PackageInfo> real = s.find("u/real");
    REQUIRE(real != nullptr);
    CHECK_FALSE(real->unknown);
}

TEST_CASE("PackageService: a folder up to four levels down is a root, the fifth is not") {
    Stick s;
    s.write("DOS/Prince/PRINCE.EXE", "x");   // depth 2
    s.write("a/b/c/d/DOOM.WAD", "IWAD");     // the root a/b/c/d is 4 levels down: found
    s.write("a1/b/c/d/e/DOOM2.WAD", "IWAD"); // 5 levels: not found
    s.scan();

    vector<string> found = ids(s.service.packages());
    CHECK(std::find(found.begin(), found.end(), "u/dos/prince") != found.end());
    CHECK(std::find(found.begin(), found.end(), "u/a/b/c/d") != found.end());
    CHECK(std::find(found.begin(), found.end(), "u/a1/b/c/d/e") == found.end());
    // the grouping folder DOS/ is not unknown data - it holds a recognised game
    CHECK(s.find("u/dos") == nullptr);
    // a1/ holds nothing recognised: it is unknown data
    REQUIRE(s.find("u/a1") != nullptr);
    CHECK(s.find("u/a1")->unknown);
}

TEST_CASE("PackageService: a table row for a dos-game carries its starts, settings and mapper") {
    Stick s;
    s.write("DOS/Prince/PRINCE.EXE", "x");
    s.write("DOS/Prince/Setup.exe", "x");
    s.write("DOS/Prince/KEYS.MAP", "x");
    s.scan();

    std::shared_ptr<PackageInfo> p = s.find("u/dos/prince");
    REQUIRE(p != nullptr);
    REQUIRE(p->games.size() == 1);
    const PackageGame &g = p->games[0];
    CHECK(g.kind == "dos-game");
    REQUIRE(g.starts.size() == 2);
    CHECK(g.starts[0].file == "PRINCE.EXE");
    CHECK(g.starts[1].file == "Setup.exe"); // the real spelling
    CHECK(g.starts[1].title == "Setup");
    REQUIRE(g.settings.size() == 1);
    CHECK(g.settings[0].second == "3000");
    CHECK(g.mapper == "KEYS.MAP");
}

TEST_CASE("PackageService: the folder cap stops the scan and says so") {
    Stick s;
    for (int i = 0; i < 30; i++)
        s.tmp.makeSubDir("Packages/empty" + std::to_string(100 + i));
    PackageService::Limits limits;
    limits.maxFolders = 10;
    s.scan(limits);

    bool said = false;
    for (const string &line : s.service.problems())
        said = said || line.find("stopped after 10 folders") != string::npos;
    CHECK(said);
    // the folders the walk never reached are not made out to be unknown data
    CHECK(s.service.packageCount() <= 9);
}

TEST_CASE("PackageService: a descriptor package is not descended into and wins over a table row inside it") {
    Stick s;
    s.write("freedoom/package.ini", "Title=Freedoom\nKind=doom-iwad\nGame1.Title=Phase 2\nGame1.File=freedoom2.wad\n");
    s.write("freedoom/freedoom2.wad", "IWAD");
    s.write("freedoom/DOOM2.WAD", "IWAD");      // a table row would match it - the descriptor owns the folder
    s.write("freedoom/inner/DOOM.WAD", "IWAD"); // and nothing below it is looked at
    s.scan();

    vector<PackageInfo> packages = s.service.packages();
    REQUIRE(packages.size() == 1);
    CHECK(packages[0].id == "freedoom");
    CHECK(packages[0].descriptor);
    REQUIRE(packages[0].games.size() == 1);
    CHECK(packages[0].games[0].file == "freedoom2.wad");
}

TEST_CASE("PackageService: loose files at Packages/ itself are one implicit package; the rest are ignored") {
    Stick s;
    s.write("DOOM2.WAD", "IWAD");
    s.write("notes.txt", "hello");
    s.write("README.txt", "readme");
    s.write("packages.ini", "");
    s.scan();

    vector<PackageInfo> packages = s.service.packages();
    REQUIRE(packages.size() == 1);
    CHECK(packages[0].id == "u/");
    CHECK(packages[0].title == "Packages");
    CHECK(packages[0].games[0].id == "doom2");
}

TEST_CASE("PackageService: hidden, $ and System Volume Information folders are skipped") {
    Stick s;
    s.write(".trash/DOOM.WAD", "IWAD");
    s.write("$RECYCLE.BIN/DOOM2.WAD", "IWAD");
    s.write("System Volume Information/DOOM.WAD", "IWAD");
    s.write("Doom/.hidden/DOOM2.WAD", "IWAD");
    s.scan();
    CHECK(s.service.packageCount() == 1);
    CHECK(s.find("u/doom")->unknown); // Doom/ itself holds nothing it can see
}

TEST_CASE(
    "PackageService: an empty folder and one with nothing recognised are unknown data; a grouping folder is not") {
    Stick s;
    s.tmp.makeSubDir("Packages/Empty");
    s.write("Stuff/readme.txt", "x");
    s.write("Group/Doom/DOOM.WAD", "IWAD");
    s.scan();

    REQUIRE(s.find("u/empty") != nullptr);
    CHECK(s.find("u/empty")->unknown);
    CHECK(s.find("u/empty")->title == "Empty");
    REQUIRE(s.find("u/stuff") != nullptr);
    CHECK(s.find("u/stuff")->unknown);
    CHECK(s.find("u/group") == nullptr);
    CHECK(s.find("u/group/doom") != nullptr);
}

TEST_CASE("PackageService: the same game in two places is two entries") {
    Stick s;
    s.write("One/DOOM2.WAD", "IWAD");
    s.write("Two/DOOM2.WAD", "IWAD");
    s.scan();

    AppManifest app;
    app.folder = s.tmp.at("Apps/crispy");
    app.uses = {"doom-iwad"};
    vector<PackageEntry> entries = s.service.entriesFor(app);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].game.title == "Doom II");
    CHECK(entries[1].game.title == "Doom II");
    CHECK(entries[0].id() != entries[1].id());
    CHECK(entries[0].id() == "u/one/doom2");
}

TEST_CASE("PackageService: ids are stable across scans") {
    Stick s;
    s.write("Doom/DOOM.WAD", "IWAD");
    s.write("Doom/DOOM2.WAD", "IWAD");
    s.write("Q/id1/pak0.pak", "x");
    s.scan();
    vector<PackageInfo> first = s.service.packages();
    s.scan();
    vector<PackageInfo> second = s.service.packages();

    REQUIRE(first.size() == second.size());
    for (size_t i = 0; i < first.size(); i++) {
        CHECK(first[i].id == second[i].id);
        REQUIRE(first[i].games.size() == second[i].games.size());
        for (size_t g = 0; g < first[i].games.size(); g++)
            CHECK(first[i].games[g].id == second[i].games[g].id);
    }
}

TEST_CASE("PackageService: a missing Packages/ is an empty index and no folder is made") {
    TempDir tmp("package_scan_missing");
    tmp.writeFile("rc/packages.ini", Table);
    test_support::TreeSnapshot before(tmp.path());
    PackageService service;
    service.rescan(tmp.at("Packages"), tmp.at("rc/packages.ini"));

    CHECK(service.packageCount() == 0);
    CHECK_FALSE(DirEntry::exists(tmp.at("Packages")));
    CHECK(before.changesTo(test_support::TreeSnapshot(tmp.path())).empty());
}

TEST_CASE("PackageService: two folders with one id - the first by folder name keeps it, the other is a duplicate") {
    Stick s;
    s.write("a-first/package.ini", "Title=One\nId=same\nKind=k\nGame1.Title=G\nGame1.File=g.dat\n");
    s.write("a-first/g.dat");
    s.write("b-second/package.ini", "Title=Two\nId=same\nKind=k\nGame1.Title=G\nGame1.File=g.dat\n");
    s.write("b-second/g.dat");
    s.scan();

    vector<PackageInfo> packages = s.service.packages();
    REQUIRE(packages.size() == 2);
    CHECK_FALSE(packages[0].duplicate);
    CHECK(packages[0].title == "One");
    CHECK(packages[1].duplicate);

    AppManifest app;
    app.folder = s.tmp.at("Apps/x");
    app.uses = {"k"};
    CHECK(s.service.entriesFor(app).size() == 1); // the duplicate's games are not offered
}

TEST_CASE("PackageService: the player's own Packages/packages.ini is read first, so his rows win") {
    Stick s;
    s.write("Doom/DOOM.WAD", "IWAD");
    s.write("Mine/GAME.DAT", "x");
    s.write("packages.ini", "[my-doom]\nkind=doom-iwad\ntitle=My Doom\nmatch=DOOM.WAD\n"
                            "[mine]\nkind=my-kind\ntitle=Mine\nmatch=GAME.DAT\n");
    s.scan();

    CHECK(s.find("u/doom")->games[0].title == "My Doom"); // his row before the shipped [doom]
    CHECK(s.find("u/doom")->games[0].id == "my-doom");
    REQUIRE(s.find("u/mine") != nullptr);
    CHECK(s.find("u/mine")->games[0].kind == "my-kind"); // a game we do not know, one row
    // the rows in force: his first
    CHECK(s.service.rows().front().id == "my-doom");
}

TEST_CASE("PackageService: scanning twice changes nothing on the stick") {
    Stick s;
    s.write("Doom/DOOM.WAD", "IWAD");
    s.write("freedoom/package.ini", "Title=F\nKind=doom-iwad\nGame1.Title=G\nGame1.File=f.wad\n");
    s.write("freedoom/f.wad", "IWAD");
    s.tmp.makeSubDir("Packages/Unknown");
    test_support::TreeSnapshot before(s.tmp.path());
    s.scan();
    s.scan();
    CHECK(before.changesTo(test_support::TreeSnapshot(s.tmp.path())).empty());
}

TEST_CASE("PackageService: the signature of the top-level listing changes when something is added") {
    Stick s;
    s.write("Doom/DOOM.WAD", "IWAD");
    const string one = PackageService::signatureOf(s.packages());
    CHECK(one == PackageService::signatureOf(s.packages()));
    s.write("Doom/DOOM2.WAD", "IWAD"); // dropped into an existing folder
    const string two = PackageService::signatureOf(s.packages());
    CHECK(two != one);
    s.tmp.makeSubDir("Packages/New");
    CHECK(PackageService::signatureOf(s.packages()) != two);
    CHECK(PackageService::signatureOf(s.tmp.at("Nothing")).empty());
}

TEST_CASE("PackageService: kind names, source labels and natural order") {
    CHECK(PackageService::kindName("doom-iwad") == "Doom data");
    CHECK(PackageService::kindName("dos-game") == "DOS game");
    CHECK(PackageService::kindName("future-kind") == "future-kind"); // an unknown kind is shown by its id
    CHECK(PackageService::knownKinds().size() == 11);
    CHECK(PackageService::sourceName("store", false) == "Store");
    CHECK(PackageService::sourceName("mod", false) == "Mod");
    CHECK(PackageService::sourceName("user", false) == "Your files");
    CHECK(PackageService::sourceName("user", true) == "In this App");

    CHECK(PackageService::naturalLess("Level 2", "Level 10"));
    CHECK_FALSE(PackageService::naturalLess("Level 10", "Level 2"));
    CHECK(PackageService::naturalLess("doom", "DOOM II"));
    CHECK(PackageService::naturalLess("apple", "Banana"));
    CHECK_FALSE(PackageService::naturalLess("same", "SAME"));
}

TEST_CASE("PackageService: the README the installers lay lists one example per kind of the table") {
    vector<PackageRow> rows;
    vector<string> problems;
    PackageTable::parse(Table, rows, problems);
    const string text = PackageService::readmeText(rows);
    CHECK(text.find("Packages/Doom/DOOM.WAD") != string::npos);
    CHECK(text.find("Packages/Quake/id1/pak0.pak") != string::npos);
    CHECK(text.find("only read, never changed") != string::npos);
    CHECK(text.find("never supply") != string::npos);
}
