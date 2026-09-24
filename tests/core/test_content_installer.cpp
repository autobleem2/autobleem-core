//
// AppInstaller and GameInstaller: downloaded content into Apps/ and Games/ through a staging folder
// (docs/store-plan.md, docs/app-format-plan.md in the launcher).
//
#include "doctest/doctest.h"

#include "../support/tar_builder.h"
#include "../support/temp_dir.h"
#include "core/services/content_installer.h"
#include "core/main.h"

#include <ableem/engine/zip_writer.h>

#include <map>

using namespace std;

namespace {
void zip(const string &path, const map<string, string> &entries) {
    ableem::ZipWriter w;
    REQUIRE(w.open(path));
    for (const auto &e : entries)
        REQUIRE(w.addBytes(e.first, e.second));
    REQUIRE(w.close());
}

struct Stick {
    Stick() : tmp("installer") {
        tmp.makeSubDir("Apps");
        tmp.makeSubDir("Games");
        tmp.makeSubDir("System/Store/staging");
        tmp.makeSubDir("dl");
    }
    string apps() const { return tmp.at("Apps"); }
    string games() const { return tmp.at("Games"); }
    string staging() const { return tmp.at("System/Store/staging"); }
    TempDir tmp;
};
} // namespace

TEST_CASE("AppInstaller: our package layout (Apps/<name>/...) installs, for this machine's key") {
    Stick s;
    zip(s.tmp.at("dl/opentyrian-psc-2.1.zip"), {{"Apps/opentyrian/app.ini", "[app]\nTitle=OpenTyrian\nVersion=2.1\nExec=bin/{key}/tyrian\n"},
                                               {"Apps/opentyrian/bin/psc/tyrian", "psc binary"},
                                               {"Apps/opentyrian/data/level1", "level"},
                                               {"Apps/opentyrian/pad.ini", "package pad"}});
    InstallResult r = AppInstaller::install(s.tmp.at("dl/opentyrian-psc-2.1.zip"), s.apps(), s.staging(), {"psc"});
    REQUIRE(r.ok);
    CHECK(r.name == "opentyrian");
    CHECK(r.path == s.apps() + "/opentyrian");
    CHECK(s.tmp.readFile("Apps/opentyrian/bin/psc/tyrian") == "psc binary");
    CHECK(s.tmp.readFile("Apps/opentyrian/data/level1") == "level");
    CHECK(s.tmp.readFile("Apps/opentyrian/pad.ini") == "package pad"); // none was there: the package's
    CHECK(DirEntry::diru(s.staging()).empty());                        // nothing left in staging
}

TEST_CASE("AppInstaller: another platform's package of the same version merges; a new version replaces") {
    Stick s;
    zip(s.tmp.at("dl/t-psc.zip"), {{"Apps/t/app.ini", "Version=1\nExec=bin/{key}/t\n"}, {"Apps/t/bin/psc/t", "psc 1"}});
    zip(s.tmp.at("dl/t-rpi64.zip"), {{"Apps/t/app.ini", "Version=1\nExec=bin/{key}/t\n"}, {"Apps/t/bin/rpi64/t", "rpi64 1"},
                                     {"Apps/t/pad.ini", "package pad"}});
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t-psc.zip"), s.apps(), s.staging(), {"psc"}).ok);
    s.tmp.writeFile("Apps/t/pad.ini", "the user's pad");
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t-rpi64.zip"), s.apps(), s.staging(), {"rpi64", "linux-arm64"}).ok);
    CHECK(s.tmp.readFile("Apps/t/bin/psc/t") == "psc 1"); // the other platform's binary kept
    CHECK(s.tmp.readFile("Apps/t/bin/rpi64/t") == "rpi64 1");
    CHECK(s.tmp.readFile("Apps/t/pad.ini") == "the user's pad"); // the user's kept

    zip(s.tmp.at("dl/t2-psc.zip"), {{"Apps/t/app.ini", "Version=2\nExec=bin/{key}/t\n"}, {"Apps/t/bin/psc/t", "psc 2"}});
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t2-psc.zip"), s.apps(), s.staging(), {"psc"}).ok);
    CHECK(s.tmp.readFile("Apps/t/bin/psc/t") == "psc 2");
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/t/bin/rpi64/t"))); // no two versions mix
    CHECK(s.tmp.readFile("Apps/t/pad.ini") == "the user's pad");
}

TEST_CASE("AppInstaller: an app.ini at the archive's root, or in its one folder; a tar.gz as well") {
    Stick s;
    zip(s.tmp.at("dl/flat-1.0.zip"), {{"app.ini", "Exec=bin/{key}/f\n"}, {"bin/psc/f", "f"}});
    InstallResult flat = AppInstaller::install(s.tmp.at("dl/flat-1.0.zip"), s.apps(), s.staging(), {"psc"});
    REQUIRE(flat.ok);
    CHECK(flat.name == "flat"); // named after the archive, up to its first "-"

    zip(s.tmp.at("dl/folder.zip"), {{"MyGame/app.ini", "Exec=bin/{key}/g\n"}, {"MyGame/bin/psc/g", "g"},
                                    {"__MACOSX/MyGame/._app.ini", "junk"}});
    InstallResult folder = AppInstaller::install(s.tmp.at("dl/folder.zip"), s.apps(), s.staging(), {"psc"});
    REQUIRE(folder.ok);
    CHECK(folder.name == "MyGame");

    test_support::TarBuilder tar;
    tar.file("Apps/tarred/app.ini", "Exec=bin/{key}/x\n").file("Apps/tarred/bin/psc/x", "x");
    REQUIRE(tar.writeTarGz(s.tmp.at("dl/tarred.tar.gz")));
    REQUIRE(AppInstaller::install(s.tmp.at("dl/tarred.tar.gz"), s.apps(), s.staging(), {"psc"}).ok);
    CHECK(s.tmp.readFile("Apps/tarred/bin/psc/x") == "x");
}

TEST_CASE("AppInstaller refuses: no App, not for this machine, not an archive - and touches nothing") {
    Stick s;
    zip(s.tmp.at("dl/none.zip"), {{"readme.txt", "hi"}});
    InstallResult none = AppInstaller::install(s.tmp.at("dl/none.zip"), s.apps(), s.staging(), {"psc"});
    CHECK_FALSE(none.ok);
    CHECK(none.error.find("no App") != string::npos);

    zip(s.tmp.at("dl/win-only.zip"), {{"Apps/w/app.ini", "Exec=bin/{key}/w\n"}, {"Apps/w/bin/win/w.exe", "w"}});
    InstallResult winOnly = AppInstaller::install(s.tmp.at("dl/win-only.zip"), s.apps(), s.staging(), {"psc"});
    CHECK_FALSE(winOnly.ok);
    CHECK(winOnly.error.find("cannot run on this system") != string::npos);

    s.tmp.writeFile("dl/game.rar", "Rar!");
    CHECK_FALSE(AppInstaller::install(s.tmp.at("dl/game.rar"), s.apps(), s.staging(), {"psc"}).ok);
    CHECK(DirEntry::diru(s.apps()).empty());
}

TEST_CASE("AppInstaller::remove") {
    Stick s;
    zip(s.tmp.at("dl/t.zip"), {{"Apps/t/app.ini", "Exec=bin/{key}/t\n"}, {"Apps/t/bin/psc/t", "t"}});
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t.zip"), s.apps(), s.staging(), {"psc"}).ok);
    string error;
    CHECK(AppInstaller::remove(s.apps() + "/t", error));
    CHECK_FALSE(DirEntry::exists(s.apps() + "/t"));
    CHECK_FALSE(AppInstaller::remove(s.apps() + "/t", error));
}

TEST_CASE("GameInstaller: a two-disc game from two archives, nested folders, into one folder") {
    Stick s;
    zip(s.tmp.at("dl/d1.zip"), {{"Some Game (Disc 1)/Some Game (Disc 1).cue", "FILE \"Some Game (Disc 1).bin\" BINARY"},
                                {"Some Game (Disc 1)/Some Game (Disc 1).bin", "disc one"},
                                {"readme.nfo", "not a disc"}});
    zip(s.tmp.at("dl/d2.zip"), {{"Some Game (Disc 2).chd", "disc two"}});
    InstallResult r = GameInstaller::install({s.tmp.at("dl/d1.zip"), s.tmp.at("dl/d2.zip")}, "Some Game: The Sequel",
                                             s.games(), s.staging());
    REQUIRE(r.ok);
    CHECK(r.name == "Some Game - The Sequel");
    CHECK(s.tmp.readFile("Games/Some Game - The Sequel/Some Game (Disc 1).bin") == "disc one");
    CHECK(s.tmp.readFile("Games/Some Game - The Sequel/Some Game (Disc 2).chd") == "disc two");
    CHECK(DirEntry::exists(s.tmp.at("Games/Some Game - The Sequel/Some Game (Disc 1).cue")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Games/Some Game - The Sequel/readme.nfo")));
    CHECK(DirEntry::diru(s.staging()).empty());
}

TEST_CASE("GameInstaller: a bare .bin gets a .cue; a taken folder name gets (2)") {
    Stick s;
    s.tmp.writeFile("dl/homebrew.bin", "data");
    s.tmp.makeSubDir("Games/Homebrew");
    InstallResult r = GameInstaller::install({s.tmp.at("dl/homebrew.bin")}, "Homebrew", s.games(), s.staging());
    REQUIRE(r.ok);
    CHECK(r.name == "Homebrew (2)");
    CHECK(s.tmp.readFile("Games/Homebrew (2)/homebrew.bin") == "data");
    CHECK(s.tmp.readFile("Games/Homebrew (2)/homebrew.cue") == GameInstaller::cueFor("homebrew.bin"));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("dl/homebrew.bin"))); // moved, not copied
}

TEST_CASE("GameInstaller refuses what holds no disc image, and leaves nothing") {
    Stick s;
    zip(s.tmp.at("dl/manual.zip"), {{"manual.pdf", "pdf"}, {"game.sbi", "subchannel only"}});
    InstallResult r = GameInstaller::install({s.tmp.at("dl/manual.zip")}, "Nope", s.games(), s.staging());
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("no PlayStation disc image") != string::npos);
    CHECK(DirEntry::diru(s.games()).empty());
    CHECK(DirEntry::diru(s.staging()).empty());
    CHECK_FALSE(GameInstaller::install({}, "Nothing", s.games(), s.staging()).ok);
}

TEST_CASE("GameInstaller::folderNameFor") {
    CHECK(GameInstaller::folderNameFor("Crash: Warped?") == "Crash - Warped");
    CHECK(GameInstaller::folderNameFor("a/b\\c<d>e|f*g\"h") == "abcdefgh");
    CHECK(GameInstaller::folderNameFor("Trailing dots...  ") == "Trailing dots");
    CHECK(GameInstaller::folderNameFor("???") == "Game");
}
