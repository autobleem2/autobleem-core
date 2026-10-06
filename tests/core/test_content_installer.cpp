//
// AppInstaller, PackageInstaller and GameInstaller: downloaded content into Apps/, Packages/ and Games/ through a
// staging folder (the launcher's docs/store-plan.md, autobleem-main docs/archive/app-format-plan.md).
//
#include "doctest/doctest.h"

#include "../support/tar_builder.h"
#include "../support/temp_dir.h"
#include "../support/tree_snapshot.h"
#include "core/services/app_settings.h"
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
    zip(s.tmp.at("dl/opentyrian-psc-2.1.zip"),
        {{"Apps/opentyrian/app.ini", "[app]\nTitle=OpenTyrian\nVersion=2.1\nExec=bin/{key}/tyrian\n"},
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
    zip(s.tmp.at("dl/t-rpi64.zip"), {{"Apps/t/app.ini", "Version=1\nExec=bin/{key}/t\n"},
                                     {"Apps/t/bin/rpi64/t", "rpi64 1"},
                                     {"Apps/t/pad.ini", "package pad"}});
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t-psc.zip"), s.apps(), s.staging(), {"psc"}).ok);
    s.tmp.writeFile("Apps/t/pad.ini", "the user's pad");
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t-rpi64.zip"), s.apps(), s.staging(), {"rpi64", "linux-arm64"}).ok);
    CHECK(s.tmp.readFile("Apps/t/bin/psc/t") == "psc 1"); // the other platform's binary kept
    CHECK(s.tmp.readFile("Apps/t/bin/rpi64/t") == "rpi64 1");
    CHECK(s.tmp.readFile("Apps/t/pad.ini") == "the user's pad"); // the user's kept

    zip(s.tmp.at("dl/t2-psc.zip"),
        {{"Apps/t/app.ini", "Version=2\nExec=bin/{key}/t\n"}, {"Apps/t/bin/psc/t", "psc 2"}});
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t2-psc.zip"), s.apps(), s.staging(), {"psc"}).ok);
    CHECK(s.tmp.readFile("Apps/t/bin/psc/t") == "psc 2");
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/t/bin/rpi64/t"))); // no two versions mix
    CHECK(s.tmp.readFile("Apps/t/pad.ini") == "the user's pad");
}

TEST_CASE("AppInstaller: the player's Game settings (ab_settings.ini) survive an update, same version or new") {
    Stick s;
    zip(s.tmp.at("dl/t-psc.zip"), {{"Apps/t/app.ini", "Version=1\nExec=bin/{key}/t\n"}, {"Apps/t/bin/psc/t", "psc 1"}});
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t-psc.zip"), s.apps(), s.staging(), {"psc"}).ok);
    REQUIRE(AppSettings::setPadModeOverride(s.apps() + sep + "t", "psc-kernel"));

    REQUIRE(AppInstaller::install(s.tmp.at("dl/t-psc.zip"), s.apps(), s.staging(), {"psc"}).ok); // same version
    CHECK(AppSettings::padModeOverride(s.apps() + sep + "t") == "psc-kernel");

    zip(s.tmp.at("dl/t2-psc.zip"), {{"Apps/t/app.ini", "Version=2\nExec=bin/{key}/t\n"},
                                    {"Apps/t/bin/psc/t", "psc 2"},
                                    {"Apps/t/ab_settings.ini", "PadMode=x360\n"}}); // even if a package ships one
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t2-psc.zip"), s.apps(), s.staging(), {"psc"}).ok); // a new version
    CHECK(s.tmp.readFile("Apps/t/bin/psc/t") == "psc 2");
    CHECK(AppSettings::padModeOverride(s.apps() + sep + "t") == "psc-kernel");
}

TEST_CASE("AppInstaller: an App of the old kind (Startup=, no Exec) is replaced whole - nothing of it kept") {
    Stick s;
    // RetroBoot's build of the same App: its own ini, script, binary, config and a save
    s.tmp.makeSubDir("Apps/opentyrian/data");
    s.tmp.writeFile("Apps/opentyrian/app.ini", "Title=Tyrian (OpenTyrian)\nStartup=run.sh\nKernel=false\n");
    s.tmp.writeFile("Apps/opentyrian/run.sh", "sh launchot.sh\n");
    s.tmp.writeFile("Apps/opentyrian/opentyrian", "old binary");
    s.tmp.writeFile("Apps/opentyrian/tyrian.sav", "old save");
    s.tmp.writeFile("Apps/opentyrian/pad.ini", "old pad");
    s.tmp.writeFile("Apps/opentyrian/data/old", "old data");
    zip(s.tmp.at("dl/opentyrian-psc-2.zip"), {{"Apps/opentyrian/app.ini", "Version=2\nExec=bin/{key}/opentyrian\n"},
                                              {"Apps/opentyrian/bin/psc/opentyrian", "new binary"},
                                              {"Apps/opentyrian/data/level1", "level"},
                                              {"Apps/opentyrian/pad.ini", "package pad"}});
    REQUIRE(AppInstaller::install(s.tmp.at("dl/opentyrian-psc-2.zip"), s.apps(), s.staging(), {"psc"}).ok);
    CHECK(s.tmp.readFile("Apps/opentyrian/bin/psc/opentyrian") == "new binary");
    CHECK(s.tmp.readFile("Apps/opentyrian/data/level1") == "level");
    CHECK(s.tmp.readFile("Apps/opentyrian/pad.ini") == "package pad"); // the old one went with the rest
    for (const char *old : {"run.sh", "opentyrian", "tyrian.sav", "data/old"})
        CHECK_FALSE(DirEntry::exists(s.tmp.at(string("Apps/opentyrian/") + old)));

    // an App of our kind over it again keeps what it always kept (the user's pad.ini, the same version)
    s.tmp.writeFile("Apps/opentyrian/pad.ini", "the user's pad");
    s.tmp.writeFile("Apps/opentyrian/opentyrian.cfg", "the user's settings");
    REQUIRE(AppInstaller::install(s.tmp.at("dl/opentyrian-psc-2.zip"), s.apps(), s.staging(), {"psc"}).ok);
    CHECK(s.tmp.readFile("Apps/opentyrian/pad.ini") == "the user's pad");
    CHECK(s.tmp.readFile("Apps/opentyrian/opentyrian.cfg") == "the user's settings");
}
TEST_CASE("AppInstaller: an app.ini at the archive's root, or in its one folder; a tar.gz as well") {
    Stick s;
    zip(s.tmp.at("dl/flat-1.0.zip"), {{"app.ini", "Exec=bin/{key}/f\n"}, {"bin/psc/f", "f"}});
    InstallResult flat = AppInstaller::install(s.tmp.at("dl/flat-1.0.zip"), s.apps(), s.staging(), {"psc"});
    REQUIRE(flat.ok);
    CHECK(flat.name == "flat"); // named after the archive, up to its first "-"

    zip(s.tmp.at("dl/folder.zip"),
        {{"MyGame/app.ini", "Exec=bin/{key}/g\n"}, {"MyGame/bin/psc/g", "g"}, {"__MACOSX/MyGame/._app.ini", "junk"}});
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

TEST_CASE("ModInstaller: a .mod lands whole in Mods/ (made when missing), a same-name file is replaced") {
    Stick s;
    s.tmp.writeFile("dl/openlara-0.9.0-1.mod", string("!<arch>\n") + "package one");
    InstallResult r = ModInstaller::install(s.tmp.at("dl/openlara-0.9.0-1.mod"), s.tmp.at("Mods"), s.apps());
    REQUIRE(r.ok);
    CHECK(r.path == s.tmp.at("Mods") + "/openlara-0.9.0-1.mod");
    CHECK(r.name == "openlara-0.9.0-1.mod");
    CHECK(s.tmp.readFile("Mods/openlara-0.9.0-1.mod") == "!<arch>\npackage one");
    CHECK_FALSE(DirEntry::exists(s.tmp.at("dl/openlara-0.9.0-1.mod"))); // moved, not copied
    CHECK(DirEntry::diru(s.tmp.at("Mods")).size() == 1);                // no .part left

    s.tmp.writeFile("dl/openlara-0.9.0-1.mod", string("!<arch>\n") + "package two");
    REQUIRE(ModInstaller::install(s.tmp.at("dl/openlara-0.9.0-1.mod"), s.tmp.at("Mods"), s.apps()).ok);
    CHECK(s.tmp.readFile("Mods/openlara-0.9.0-1.mod") == "!<arch>\npackage two");
}

TEST_CASE("ModInstaller: an update retires the old version's package and its marker, not its App") {
    Stick s;
    s.tmp.makeSubDir("Mods");
    s.tmp.writeFile("Mods/t-1.0.mod", "!<arch>\nold");
    s.tmp.writeFile("Apps/.pe_state/t-1.0.mod.ini", "Version=1.0\nApps=pe-t\n");
    s.tmp.writeFile("Apps/pe-t/app.ini", "Title=T\nPeSource=t-1.0.mod\n");
    s.tmp.writeFile("dl/t-1.1.mod", "!<arch>\nnew");
    REQUIRE(ModInstaller::install(s.tmp.at("dl/t-1.1.mod"), s.tmp.at("Mods"), s.apps(), s.tmp.at("Mods/t-1.0.mod")).ok);
    CHECK(DirEntry::exists(s.tmp.at("Mods/t-1.1.mod")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Mods/t-1.0.mod")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/.pe_state/t-1.0.mod.ini")));
    CHECK(DirEntry::exists(s.tmp.at("Apps/pe-t/app.ini"))); // the processor replaces it from the new package
}

TEST_CASE("ModInstaller: an update retires the old package from Mods/done/ too, and a done/ copy of the new name") {
    Stick s;
    s.tmp.makeSubDir("Mods/done");
    s.tmp.writeFile("Mods/done/t-1.0.mod", "!<arch>\nold");
    s.tmp.writeFile("Mods/done/t-1.1.mod", "!<arch>\nstale copy");
    s.tmp.writeFile("Apps/.pe_state/t-1.0.mod.ini", "Version=1.0\nApps=pe-t\n");
    s.tmp.writeFile("Apps/pe-t/app.ini", "Title=T\nPeSource=t-1.0.mod\n");
    s.tmp.writeFile("dl/t-1.1.mod", "!<arch>\nnew");
    // the Store passes the path it recorded (Mods/...), the file now being in done/
    REQUIRE(ModInstaller::install(s.tmp.at("dl/t-1.1.mod"), s.tmp.at("Mods"), s.apps(), s.tmp.at("Mods/t-1.0.mod")).ok);
    CHECK(s.tmp.readFile("Mods/t-1.1.mod") == "!<arch>\nnew");
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Mods/done/t-1.0.mod")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Mods/done/t-1.1.mod")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/.pe_state/t-1.0.mod.ini")));
    CHECK(DirEntry::exists(s.tmp.at("Apps/pe-t/app.ini")));
}

TEST_CASE("ModInstaller::present: the .mod in Mods/ or in Mods/done/, or the processor's marker") {
    Stick s;
    s.tmp.makeSubDir("Mods/done");
    CHECK_FALSE(ModInstaller::present(s.tmp.at("Mods/a-1.0.mod"), s.apps()));
    s.tmp.writeFile("Mods/a-1.0.mod", "!<arch>\na");
    CHECK(ModInstaller::present(s.tmp.at("Mods/a-1.0.mod"), s.apps()));
    CHECK(DirEntry::removeFile(s.tmp.at("Mods/a-1.0.mod")));
    CHECK_FALSE(ModInstaller::present(s.tmp.at("Mods/a-1.0.mod"), s.apps()));
    s.tmp.writeFile("Mods/done/a-1.0.mod", "!<arch>\na"); // the processor moved it
    CHECK(ModInstaller::present(s.tmp.at("Mods/a-1.0.mod"), s.apps()));
    CHECK(ModInstaller::present(s.tmp.at("Mods/done/a-1.0.mod"), s.apps())); // asked with the done/ path too
    CHECK(DirEntry::removeFile(s.tmp.at("Mods/done/a-1.0.mod")));
    s.tmp.writeFile("Apps/.pe_state/a-1.0.mod.ini", "Version=1.0\nApps=pe-a\n"); // only the marker is left
    CHECK(ModInstaller::present(s.tmp.at("Mods/a-1.0.mod"), s.apps()));
    CHECK(ModInstaller::modsDirOf(s.tmp.at("Mods/done/a-1.0.mod")) == s.tmp.at("Mods"));
    CHECK(ModInstaller::modsDirOf(s.tmp.at("Mods/a-1.0.mod")) == s.tmp.at("Mods"));
}

TEST_CASE("ModInstaller::remove: a package that the processor moved to Mods/done/ goes too, with its App and marker") {
    Stick s;
    s.tmp.makeSubDir("Mods/done");
    s.tmp.writeFile("Mods/done/a-1.0.mod", "!<arch>\na");
    s.tmp.writeFile("Mods/done/b-1.0.mod", "!<arch>\nb");
    s.tmp.writeFile("Apps/.pe_state/a-1.0.mod.ini", "Version=1.0\nApps=pe-a\n");
    s.tmp.writeFile("Apps/pe-a/app.ini", "Title=A\nPeSource=a-1.0.mod\n");
    s.tmp.writeFile("Apps/pe-b/app.ini", "Title=B\nPeSource=b-1.0.mod\n");
    string error;
    // asked with the Mods/ path the Store recorded, then with the done/ path itself
    REQUIRE(ModInstaller::remove(s.tmp.at("Mods/a-1.0.mod"), s.apps(), error));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Mods/done/a-1.0.mod")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/pe-a")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/.pe_state/a-1.0.mod.ini")));
    CHECK(DirEntry::exists(s.tmp.at("Mods/done/b-1.0.mod")));
    CHECK(DirEntry::exists(s.tmp.at("Apps/pe-b/app.ini")));
    REQUIRE(ModInstaller::remove(s.tmp.at("Mods/done/b-1.0.mod"), s.apps(), error));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Mods/done/b-1.0.mod")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/pe-b")));
    // a copy in both places (the user dropped it again before the scan): both go
    s.tmp.writeFile("Mods/done/c-1.0.mod", "!<arch>\nold");
    s.tmp.writeFile("Mods/c-1.0.mod", "!<arch>\nnew");
    REQUIRE(ModInstaller::remove(s.tmp.at("Mods/c-1.0.mod"), s.apps(), error));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Mods/done/c-1.0.mod")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Mods/c-1.0.mod")));
}

TEST_CASE("ModInstaller refuses what is no PE package, and places nothing") {
    Stick s;
    s.tmp.writeFile("dl/game.zip", "!<arch>\nx");
    s.tmp.writeFile("dl/garbage.mod", "this is not an archive");
    CHECK_FALSE(ModInstaller::install(s.tmp.at("dl/game.zip"), s.tmp.at("Mods"), s.apps()).ok);
    CHECK_FALSE(ModInstaller::install(s.tmp.at("dl/garbage.mod"), s.tmp.at("Mods"), s.apps()).ok);
    CHECK_FALSE(ModInstaller::install(s.tmp.at("dl/none.mod"), s.tmp.at("Mods"), s.apps()).ok);
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Mods")));
}

TEST_CASE("ModInstaller::remove: the package, the Apps made from it and the marker - not an App of anyone else") {
    Stick s;
    s.tmp.makeSubDir("Mods");
    s.tmp.writeFile("Mods/a-1.0.mod", "!<arch>\na");
    s.tmp.writeFile("Mods/b-1.0.mod", "!<arch>\nb");
    s.tmp.writeFile("Apps/.pe_state/a-1.0.mod.ini", "Version=1.0\nApps=pe-a\n");
    s.tmp.writeFile("Apps/pe-a/app.ini", "Title=A\nPeSource=a-1.0.mod\n");
    s.tmp.writeFile("Apps/pe-a/save.dat", "the player's save");
    s.tmp.writeFile("Apps/pe-b/app.ini", "Title=B\nPeSource=b-1.0.mod\n");
    s.tmp.writeFile("Apps/pe-mine/app.ini", "Title=Mine\n"); // no PeSource: not made by the processor
    s.tmp.writeFile("Apps/opentyrian/app.ini", "Title=OpenTyrian\n");
    string error;
    REQUIRE(ModInstaller::remove(s.tmp.at("Mods/a-1.0.mod"), s.apps(), error));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Mods/a-1.0.mod")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/pe-a")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/.pe_state/a-1.0.mod.ini")));
    CHECK(DirEntry::exists(s.tmp.at("Mods/b-1.0.mod")));
    CHECK(DirEntry::exists(s.tmp.at("Apps/pe-b/app.ini")));
    CHECK(DirEntry::exists(s.tmp.at("Apps/pe-mine/app.ini")));
    CHECK(DirEntry::exists(s.tmp.at("Apps/opentyrian/app.ini")));
    // already gone: still fine (the Store forgets it)
    CHECK(ModInstaller::remove(s.tmp.at("Mods/a-1.0.mod"), s.apps(), error));
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

// ---------------------------------------------------------------------------------------------------------
// PackageInstaller and the Replaces=/Migrate= of AppInstaller (autobleem-main docs/packages.md 2.4, 11)
// ---------------------------------------------------------------------------------------------------------
namespace {
string freedoomIni(const string &version, const string &extra = "") {
    return "[package]\nTitle=Freedoom\nKind=doom-iwad\nVersion=" + version + "\nId=freedoom\n" + extra +
           "Game1.Id=freedoom1\nGame1.Title=Freedoom: Phase 1\nGame1.File=freedoom1.wad\n";
}
string packages(const Stick &s) {
    return s.tmp.at("Packages");
}
} // namespace

TEST_CASE("PackageInstaller: a zip with package.ini at its root becomes Packages/<id>/, stamped, with the README") {
    Stick s;
    zip(s.tmp.at("dl/freedoom-0.13.zip"),
        {{"package.ini", freedoomIni("0.13")}, {"freedoom1.wad", "IWAD"}, {"licences/COPYING.txt", "BSD"}});
    REQUIRE_FALSE(DirEntry::exists(packages(s))); // the first install makes it

    InstallResult r =
        PackageInstaller::install(s.tmp.at("dl/freedoom-0.13.zip"), packages(s), s.staging(), "pkg/freedoom");
    REQUIRE(r.ok);
    CHECK_FALSE(r.unchanged);
    CHECK(r.name == "freedoom");
    CHECK(r.path == packages(s) + "/freedoom");
    CHECK(s.tmp.readFile("Packages/freedoom/freedoom1.wad") == "IWAD");
    CHECK(s.tmp.readFile("Packages/freedoom/licences/COPYING.txt") == "BSD");
    const string ini = s.tmp.readFile("Packages/freedoom/package.ini");
    CHECK(ini.find("Source=store") != string::npos);
    CHECK(ini.find("StoreId=pkg/freedoom") != string::npos);
    CHECK(ini.find("Title=Freedoom") != string::npos); // the rest as it came
    CHECK(DirEntry::exists(s.tmp.at("Packages/README.txt")));
    CHECK(DirEntry::diru(s.staging()).empty()); // nothing left in staging
}

TEST_CASE("PackageInstaller: package.ini in the archive's one folder; a hand-stamped Source is replaced by ours") {
    Stick s;
    zip(s.tmp.at("dl/x.zip"),
        {{"pq/package.ini", freedoomIni("1", "Source=user\nStoreId=old\n")}, {"pq/freedoom1.wad", "x"}});
    InstallResult r = PackageInstaller::install(s.tmp.at("dl/x.zip"), packages(s), s.staging(), "pkg/freedoom");
    REQUIRE(r.ok);
    const string ini = s.tmp.readFile("Packages/freedoom/package.ini");
    CHECK(ini.find("Source=user") == string::npos);
    CHECK(ini.find("StoreId=old") == string::npos);
    CHECK(ini.find("Source=store") != string::npos);
    CHECK(ini.find("StoreId=pkg/freedoom") != string::npos);
}

TEST_CASE("PackageInstaller: a newer version replaces the folder whole; the same or an older one is a no-op") {
    Stick s;
    zip(s.tmp.at("dl/f1.zip"), {{"package.ini", freedoomIni("0.12")}, {"freedoom1.wad", "one"}, {"old-only.txt", "x"}});
    zip(s.tmp.at("dl/f2.zip"), {{"package.ini", freedoomIni("0.13")}, {"freedoom1.wad", "two"}});
    REQUIRE(PackageInstaller::install(s.tmp.at("dl/f1.zip"), packages(s), s.staging(), "pkg/freedoom").ok);

    InstallResult same = PackageInstaller::install(s.tmp.at("dl/f1.zip"), packages(s), s.staging(), "pkg/freedoom");
    REQUIRE(same.ok);
    CHECK(same.unchanged);
    CHECK(s.tmp.readFile("Packages/freedoom/freedoom1.wad") == "one");

    InstallResult newer = PackageInstaller::install(s.tmp.at("dl/f2.zip"), packages(s), s.staging(), "pkg/freedoom");
    REQUIRE(newer.ok);
    CHECK_FALSE(newer.unchanged);
    CHECK(s.tmp.readFile("Packages/freedoom/freedoom1.wad") == "two");
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Packages/freedoom/old-only.txt"))); // the old folder is gone whole
    CHECK(DirEntry::diru(packages(s)).size() == 2);                            // freedoom/ and README.txt - no leftover

    InstallResult older = PackageInstaller::install(s.tmp.at("dl/f1.zip"), packages(s), s.staging(), "pkg/freedoom");
    REQUIRE(older.ok);
    CHECK(older.unchanged);
    CHECK(s.tmp.readFile("Packages/freedoom/freedoom1.wad") == "two"); // never a downgrade
}

TEST_CASE("PackageInstaller refuses what is no valid package and leaves Packages/ as it was") {
    Stick s;
    s.tmp.makeSubDir("Packages");
    s.tmp.writeFile("Packages/Mine/DOOM2.WAD", "player's own");
    zip(s.tmp.at("dl/nodesc.zip"), {{"freedoom1.wad", "x"}});
    zip(s.tmp.at("dl/nofile.zip"), {{"package.ini", freedoomIni("1")}}); // freedoom1.wad is not in it
    zip(s.tmp.at("dl/notitle.zip"), {{"package.ini", "Kind=k\nGame1.Title=G\nGame1.File=g\n"}, {"g", "x"}});
    s.tmp.writeFile("dl/notanarchive.txt", "hello");
    test_support::TreeSnapshot before(packages(s));

    for (const char *name : {"nodesc.zip", "nofile.zip", "notitle.zip", "notanarchive.txt", "missing.zip"}) {
        INFO(name);
        InstallResult r = PackageInstaller::install(s.tmp.at(string("dl/") + name), packages(s), s.staging(), "pkg/x");
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.error.empty());
    }
    CHECK(DirEntry::diru(s.staging()).empty());
    CHECK(before.changesTo(test_support::TreeSnapshot(packages(s))).empty()); // Packages/ is as it was
    CHECK(s.tmp.readFile("Packages/Mine/DOOM2.WAD") == "player's own");
}

TEST_CASE("PackageInstaller: a first install that is refused does not make Packages/") {
    Stick s;
    zip(s.tmp.at("dl/bad.zip"), {{"package.ini", freedoomIni("1")}});
    CHECK_FALSE(PackageInstaller::install(s.tmp.at("dl/bad.zip"), packages(s), s.staging(), "pkg/freedoom").ok);
    CHECK_FALSE(DirEntry::exists(packages(s)));
}

TEST_CASE("PackageInstaller: another package's folder is never taken - the name gets (2)") {
    Stick s;
    s.tmp.writeFile("Packages/freedoom/DOOM2.WAD", "the player's own folder, no descriptor");
    zip(s.tmp.at("dl/f.zip"), {{"package.ini", freedoomIni("1")}, {"freedoom1.wad", "x"}});
    InstallResult first = PackageInstaller::install(s.tmp.at("dl/f.zip"), packages(s), s.staging(), "pkg/freedoom");
    REQUIRE(first.ok);
    CHECK(first.name == "freedoom (2)");
    CHECK(s.tmp.readFile("Packages/freedoom/DOOM2.WAD") == "the player's own folder, no descriptor");

    // the same catalog id again finds its own folder, even when the name had to be made unique
    InstallResult again = PackageInstaller::install(s.tmp.at("dl/f.zip"), packages(s), s.staging(), "pkg/freedoom");
    REQUIRE(again.ok);
    CHECK(again.unchanged);
    CHECK(again.name == "freedoom (2)");

    // another catalog item with the same package id: one more folder
    InstallResult other =
        PackageInstaller::install(s.tmp.at("dl/f.zip"), packages(s), s.staging(), "pkg/freedoom-fork");
    REQUIRE(other.ok);
    CHECK(other.name == "freedoom (3)");
}

TEST_CASE("PackageInstaller::remove: only a folder whose descriptor says Source=store or mod") {
    Stick s;
    zip(s.tmp.at("dl/f.zip"), {{"package.ini", freedoomIni("1")}, {"freedoom1.wad", "x"}});
    REQUIRE(PackageInstaller::install(s.tmp.at("dl/f.zip"), packages(s), s.staging(), "pkg/freedoom").ok);
    s.tmp.writeFile("Packages/Mine/DOOM2.WAD", "x");                                                   // no descriptor
    s.tmp.writeFile("Packages/Hand/package.ini", "Title=Hand\nKind=k\nGame1.Title=G\nGame1.File=g\n"); // no Source
    s.tmp.writeFile("Packages/Hand/g", "x");
    s.tmp.writeFile("Packages/Man/package.ini", "Title=Man\nSource=user\n");
    s.tmp.writeFile("Packages/Modded/package.ini", "Title=Mod\nSource=mod\nPeSource=a.mod\n");

    string error;
    CHECK_FALSE(PackageInstaller::remove(packages(s) + "/Mine", error));
    CHECK_FALSE(PackageInstaller::remove(packages(s) + "/Hand", error));
    CHECK_FALSE(PackageInstaller::remove(packages(s) + "/Man", error));
    CHECK(DirEntry::exists(s.tmp.at("Packages/Mine/DOOM2.WAD")));
    CHECK(DirEntry::exists(s.tmp.at("Packages/Hand/g")));
    CHECK(PackageInstaller::remove(packages(s) + "/freedoom", error));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Packages/freedoom")));
    CHECK(PackageInstaller::remove(packages(s) + "/Modded", error));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Packages/Modded")));
}

TEST_CASE("PackageInstaller: Replaces= parks our old App only after the package is in place") {
    Stick s;
    s.tmp.writeFile("Apps/pe-freedoomdata/app.ini", "Title=Freedoom data\nPeSource=freedoomdata.mod\n");
    s.tmp.writeFile("Apps/pe-freedoomdata/data/f.wad", "old");
    s.tmp.writeFile("Apps/pe-strange/app.ini", "Title=Not ours\n"); // no PeSource=: left alone
    zip(s.tmp.at("dl/bad.zip"), {{"package.ini", freedoomIni("1", "Replaces=pe-freedoomdata; pe-strange\n")}});
    zip(s.tmp.at("dl/f.zip"),
        {{"package.ini", freedoomIni("1", "Replaces=pe-freedoomdata; pe-strange\n")}, {"freedoom1.wad", "x"}});

    // a failed install leaves the App where it was
    CHECK_FALSE(
        PackageInstaller::install(s.tmp.at("dl/bad.zip"), packages(s), s.staging(), "pkg/freedoom", s.apps()).ok);
    CHECK(DirEntry::exists(s.tmp.at("Apps/pe-freedoomdata/app.ini")));

    REQUIRE(PackageInstaller::install(s.tmp.at("dl/f.zip"), packages(s), s.staging(), "pkg/freedoom", s.apps()).ok);
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/pe-freedoomdata"))); // moved, not deleted
    CHECK(s.tmp.readFile("Apps/.replaced/pe-freedoomdata/data/f.wad") == "old");
    CHECK(DirEntry::exists(s.tmp.at("Apps/pe-strange/app.ini"))); // not ours: left alone

    // with no Apps folder named, nothing is parked
    Stick t;
    t.tmp.writeFile("Apps/pe-freedoomdata/app.ini", "Title=x\nPeSource=a.mod\n");
    zip(t.tmp.at("dl/f.zip"),
        {{"package.ini", freedoomIni("1", "Replaces=pe-freedoomdata\n")}, {"freedoom1.wad", "x"}});
    REQUIRE(PackageInstaller::install(t.tmp.at("dl/f.zip"), packages(t), t.staging(), "pkg/freedoom").ok);
    CHECK(DirEntry::exists(t.tmp.at("Apps/pe-freedoomdata/app.ini")));
}

TEST_CASE("PackageInstaller::compareVersions") {
    CHECK(PackageInstaller::compareVersions("0.13.0-1", "0.12.1-9") > 0);
    CHECK(PackageInstaller::compareVersions("1.9", "1.10") < 0); // by value, not as text
    CHECK(PackageInstaller::compareVersions("1.0", "1.0.0") == 0);
    CHECK(PackageInstaller::compareVersions("2", "1.99.99") > 0);
    CHECK(PackageInstaller::compareVersions("", "") == 0);
    CHECK(PackageInstaller::compareVersions("", "0.1") < 0);
}

TEST_CASE("ModInstaller: a package made from a mod (Packages/ beside Mods/) counts as present and goes with remove") {
    Stick s;
    s.tmp.makeSubDir("Mods/done");
    s.tmp.writeFile("Packages/pe-data/package.ini", "Title=D\nSource=mod\nPeSource=data-1.0.mod\n");
    s.tmp.writeFile("Packages/pe-data/g", "x");
    s.tmp.writeFile("Packages/pe-other/package.ini", "Title=O\nSource=mod\nPeSource=other-1.0.mod\n");
    s.tmp.writeFile("Packages/Mine/DOOM2.WAD", "x");

    CHECK(ModInstaller::present(s.tmp.at("Mods/data-1.0.mod"), s.apps())); // only the package is left of it
    CHECK(ModInstaller::present(s.tmp.at("Mods/done/data-1.0.mod"), s.apps()));
    CHECK_FALSE(ModInstaller::present(s.tmp.at("Mods/none-1.0.mod"), s.apps()));

    string error;
    REQUIRE(ModInstaller::remove(s.tmp.at("Mods/data-1.0.mod"), s.apps(), error));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Packages/pe-data")));
    CHECK(DirEntry::exists(s.tmp.at("Packages/pe-other/package.ini"))); // not its package
    CHECK(DirEntry::exists(s.tmp.at("Packages/Mine/DOOM2.WAD")));       // never a player's folder
    CHECK_FALSE(ModInstaller::present(s.tmp.at("Mods/data-1.0.mod"), s.apps()));
}

namespace {
// the App that merges three: Crispy Doom, with the saves of the old ones
void crispyZip(const Stick &s, const string &name, const map<string, string> &more = {}) {
    map<string, string> files{
        {"Apps/crispydoom/app.ini",
         "[app]\nTitle=Crispy Doom\nVersion=5.12\nExec=bin/{key}/crispy-doom\nUses=doom-iwad\n"
         "Replaces=doom; freedoom1; freedoom2; missing-app\n"
         "Migrate=doom:savegames>savegames/doom1-shareware; freedoom1:savegames>savegames/freedoom1; "
         "freedoom2:savegames>savegames/freedoom2; doom:default.cfg>default.cfg; "
         "gone:savegames>savegames/gone\n"},
        {"Apps/crispydoom/bin/psc/crispy-doom", "bin"}};
    for (const auto &kv : more)
        files[kv.first] = kv.second;
    zip(s.tmp.at("dl/" + name), files);
}
void oldDoomApps(const Stick &s) {
    for (const char *app : {"doom", "freedoom1", "freedoom2"}) {
        s.tmp.writeFile(string("Apps/") + app + "/app.ini", string("Title=") + app + "\nExec=bin/{key}/x\n");
        s.tmp.writeFile(string("Apps/") + app + "/bin/psc/x", "x");
        s.tmp.writeFile(string("Apps/") + app + "/savegames/doomsav0.dsg", string("save of ") + app);
        s.tmp.writeFile(string("Apps/") + app + "/savegames/sub/deep.dsg", string("deep of ") + app);
    }
    s.tmp.writeFile("Apps/doom/default.cfg", "mouse=1\n");
}
} // namespace

TEST_CASE("AppInstaller: Replaces= and Migrate= copy the saves per game, then park the old Apps - nothing deleted") {
    Stick s;
    oldDoomApps(s);
    crispyZip(s, "crispydoom-psc-5.12.zip");
    InstallResult r = AppInstaller::install(s.tmp.at("dl/crispydoom-psc-5.12.zip"), s.apps(), s.staging(), {"psc"});
    REQUIRE(r.ok);
    CHECK(r.warning.empty());

    // copied: a file or a folder, to the new path
    CHECK(s.tmp.readFile("Apps/crispydoom/savegames/doom1-shareware/doomsav0.dsg") == "save of doom");
    CHECK(s.tmp.readFile("Apps/crispydoom/savegames/doom1-shareware/sub/deep.dsg") == "deep of doom");
    CHECK(s.tmp.readFile("Apps/crispydoom/savegames/freedoom1/doomsav0.dsg") == "save of freedoom1");
    CHECK(s.tmp.readFile("Apps/crispydoom/savegames/freedoom2/doomsav0.dsg") == "save of freedoom2");
    CHECK(s.tmp.readFile("Apps/crispydoom/default.cfg") == "mouse=1\n");
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/crispydoom/savegames/gone"))); // a missing source is skipped

    // the old Apps are parked whole, not deleted, and are no Apps any more
    for (const char *app : {"doom", "freedoom1", "freedoom2"}) {
        CHECK_FALSE(DirEntry::exists(s.tmp.at(string("Apps/") + app)));
        CHECK(DirEntry::exists(s.tmp.at(string("Apps/.replaced/") + app + "/app.ini")));
        CHECK(DirEntry::exists(s.tmp.at(string("Apps/.replaced/") + app + "/savegames/doomsav0.dsg")));
    }
}

TEST_CASE("AppInstaller: Migrate= never overwrites, and a second install copies and moves nothing new") {
    Stick s;
    oldDoomApps(s);
    // the new App already has a save of its own at one destination
    s.tmp.writeFile("Apps/crispydoom/app.ini", "Title=Crispy Doom\nVersion=5.11\nExec=bin/{key}/crispy-doom\n");
    s.tmp.writeFile("Apps/crispydoom/bin/psc/crispy-doom", "old bin");
    s.tmp.writeFile("Apps/crispydoom/savegames/freedoom1/doomsav0.dsg", "the player's newer save");
    crispyZip(s, "crispydoom-psc-5.12.zip");
    REQUIRE(AppInstaller::install(s.tmp.at("dl/crispydoom-psc-5.12.zip"), s.apps(), s.staging(), {"psc"}).ok);
    CHECK(s.tmp.readFile("Apps/crispydoom/savegames/freedoom1/doomsav0.dsg") == "the player's newer save");
    CHECK(s.tmp.readFile("Apps/crispydoom/savegames/freedoom1/sub/deep.dsg") == "deep of freedoom1"); // the rest came

    // a second install (the old Apps are parked now; the player has a new save)
    s.tmp.writeFile("Apps/crispydoom/savegames/freedoom2/doomsav0.dsg", "changed since");
    REQUIRE(AppInstaller::install(s.tmp.at("dl/crispydoom-psc-5.12.zip"), s.apps(), s.staging(), {"psc"}).ok);
    CHECK(s.tmp.readFile("Apps/crispydoom/savegames/freedoom2/doomsav0.dsg") == "changed since");
    CHECK(DirEntry::diru_DirsOnly(s.tmp.at("Apps/.replaced")).size() == 3); // not parked again, not duplicated
}

TEST_CASE("AppInstaller: when a copy fails nothing is moved - the old Apps stay beside the new one") {
    Stick s;
    oldDoomApps(s);
    // "savegames" is a FILE in the new App: the folder for the copies cannot be made
    crispyZip(s, "crispydoom-psc-5.12.zip", {{"Apps/crispydoom/savegames", "i am a file"}});
    InstallResult r = AppInstaller::install(s.tmp.at("dl/crispydoom-psc-5.12.zip"), s.apps(), s.staging(), {"psc"});
    REQUIRE(r.ok); // the App itself is in
    CHECK_FALSE(r.warning.empty());
    for (const char *app : {"doom", "freedoom1", "freedoom2"}) {
        CHECK(DirEntry::exists(s.tmp.at(string("Apps/") + app + "/app.ini")));
        CHECK(DirEntry::exists(s.tmp.at(string("Apps/") + app + "/savegames/doomsav0.dsg")));
    }
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/.replaced")));
}

TEST_CASE("AppInstaller: Replaces= alone parks the named Apps; a Migrate path that leaves the App is refused") {
    Stick s;
    s.tmp.writeFile("Apps/old/app.ini", "Title=old\n");
    s.tmp.writeFile("Apps/old/save.dat", "x");
    s.tmp.writeFile("Secret/s.dat", "secret");
    zip(s.tmp.at("dl/n-psc-1.zip"),
        {{"Apps/n/app.ini", "Version=1\nExec=bin/{key}/n\nReplaces=old; ../Secret; .hidden\n"
                            "Migrate=old:../../Secret>stolen; old:save.dat>../escape\n"},
         {"Apps/n/bin/psc/n", "x"}});
    InstallResult r = AppInstaller::install(s.tmp.at("dl/n-psc-1.zip"), s.apps(), s.staging(), {"psc"});
    REQUIRE(r.ok);
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/old")));
    CHECK(DirEntry::exists(s.tmp.at("Apps/.replaced/old/save.dat")));
    CHECK(DirEntry::exists(s.tmp.at("Secret/s.dat")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/n/stolen")));
    CHECK_FALSE(DirEntry::exists(s.tmp.at("Apps/escape")));
}

TEST_CASE("AppInstaller: LastPackage in ab_settings.ini survives an update like the pad settings") {
    Stick s;
    zip(s.tmp.at("dl/t-psc.zip"), {{"Apps/t/app.ini", "Version=1\nExec=bin/{key}/t\n"}, {"Apps/t/bin/psc/t", "psc 1"}});
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t-psc.zip"), s.apps(), s.staging(), {"psc"}).ok);
    REQUIRE(AppSettings::setLastPackage(s.apps() + sep + "t", "u/doom/doom2"));
    zip(s.tmp.at("dl/t2-psc.zip"),
        {{"Apps/t/app.ini", "Version=2\nExec=bin/{key}/t\n"}, {"Apps/t/bin/psc/t", "psc 2"}});
    REQUIRE(AppInstaller::install(s.tmp.at("dl/t2-psc.zip"), s.apps(), s.staging(), {"psc"}).ok);
    CHECK(AppSettings::lastPackage(s.apps() + sep + "t") == "u/doom/doom2");
}
