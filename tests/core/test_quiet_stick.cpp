//
// The quiet stick (docs/quiet-stick-plan.md in the launcher repo): whatever runs on every start, every scan
// or every launch writes nothing to the data root unless the user's state changed. Each test does the
// thing twice and asserts the second pass left every file under the root exactly as it was - same size,
// same modification time - so a rewrite with identical bytes counts as a write, which is the point.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/fake_game.h"
#include "../support/game_library_fixture.h"
#include "../support/string_maker.h"
#include "../support/temp_dir.h"
#include "../support/tree_snapshot.h"

#include "core/services/config.h"
#include "core/services/scan_service.h"

#include <ableem/engine/config_file_editor.h>
#include <ableem/engine/ini_file.h>

#include <string>
#include <vector>

using std::string;
using std::vector;
using test_support::TreeSnapshot;

// A test marked should_fail() documents a write the plan has not removed yet. doctest reports such a test
// as a failure the moment it passes, so the step that fixes it has to take the decorator off.

TEST_CASE("a second start leaves config.ini alone") {
    TempDir tmp("quiet_config");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    Config{}; // the first start fills in the defaults and writes them - a real change
    TreeSnapshot before(tmp.path());
    Config{};
    CHECK(before.changesTo(TreeSnapshot(tmp.path())) == vector<string>{});
}

TEST_CASE("saving an unchanged ini file leaves it alone") {
    TempDir tmp("quiet_ini");
    tmp.writeFile("Game.ini", "[Game]\nTitle=Crash Bandicoot\nFavorite=0\n");

    ableem::IniFile ini;
    ini.load(tmp.at("Game.ini"));
    ini.save(tmp.at("Game.ini")); // normalises the layout once
    TreeSnapshot before(tmp.path());
    ini.save(tmp.at("Game.ini"));
    CHECK(before.changesTo(TreeSnapshot(tmp.path())) == vector<string>{});
}

TEST_CASE("setting cfg lines to the values they already have leaves the file alone") {
    TempDir tmp("quiet_cfg");
    tmp.writeFile("retroarch.cfg", "video_smooth = \"false\"\naspect_ratio_index = \"22\"\n");

    TreeSnapshot before(tmp.path());
    ableem::ConfigFileEditor editor;
    editor.replaceInFile(tmp.at("retroarch.cfg"), "video_smooth", "video_smooth = \"false\"");
    editor.replaceProperties(tmp.at("retroarch.cfg"), {{"aspect_ratio_index", "aspect_ratio_index = \"22\""},
                                                       {"video_smooth", "video_smooth = \"false\""}});
    CHECK(before.changesTo(TreeSnapshot(tmp.path())) == vector<string>{});
}

TEST_CASE("a rescan with nothing changed writes nothing") {
    GameLibraryFixture fx;
    fx.env.setWorkingPath(fx.tmp.path()); // the state dir: config, fingerprints, the scan's own files
    fx.env.setRetroarchDir(fx.tmp.makeSubDir("RetroArch/bin"));
    fx.tmp.makeSubDir("RetroArch/bin/playlists"); // AutoBleem.lpl is exported here
    fx.tmp.makeSubDir("RetroArch/bin/retroboot"); // and EmulationStation's gamelist.xml written for RetroBoot
    ScanService svc(fx.library);
    test_support::makeFakeGame(fx.tmp.at("Games"), "Crash Bandicoot", "SLUS_012.34");
    test_support::makeFakeGame(fx.tmp.at("Games"), "Spyro", "SLUS_012.35");
    // a second disc in Spyro's folder: the scan writes its .m3u
    test_support::makeFakeGame(fx.tmp.at("src"), "Spyro 2", "SLUS_012.35");
    ableem::DirEntry::renameFile(fx.tmp.at("src/Spyro 2/Spyro 2.cue"), fx.tmp.at("Games/Spyro/Spyro 2.cue"));
    ableem::DirEntry::renameFile(fx.tmp.at("src/Spyro 2/Spyro 2.bin"), fx.tmp.at("Games/Spyro/Spyro 2.bin"));

    svc.runScan();
    REQUIRE(svc.poll().addedGames.size() == 2);
    REQUIRE(ableem::DirEntry::exists(fx.tmp.at("Games/Spyro/Spyro 2.m3u"))); // named after the first disc
    REQUIRE(ableem::DirEntry::exists(fx.tmp.at("RetroArch/bin/playlists/AutoBleem.lpl")));
    REQUIRE(ableem::DirEntry::exists(
        fx.tmp.at("RetroArch/bin/retroboot/emulationstation/.emulationstation/gamelists/psx/gamelist.xml")));

    TreeSnapshot before(fx.tmp.path());
    svc.runScan();
    svc.poll();
    CHECK(before.changesTo(TreeSnapshot(fx.tmp.path())) == vector<string>{});
}

TEST_CASE("replaceProperties: a batch is one write, a missing key is appended, CRLF becomes LF") {
    TempDir tmp("quiet_cfg_batch");
    tmp.writeFile("pcsx.cfg", "Bios = SET_BY_PCSX\r\nFrameskip3 = 0\r\nScanlines = 0\r\n");

    ableem::ConfigFileEditor().replaceProperties(tmp.at("pcsx.cfg"),
                                                 {{"frameskip3", "Frameskip3 = 1"}, {"SlowBoot", "SlowBoot = 0"}});
    CHECK(tmp.readFile("pcsx.cfg") == "Bios = SET_BY_PCSX\nFrameskip3 = 1\nScanlines = 0\nSlowBoot = 0\n");

    // a file that is not there is not created
    ableem::ConfigFileEditor().replaceProperties(tmp.at("missing.cfg"), {{"a", "a = 1"}});
    CHECK_FALSE(ableem::DirEntry::exists(tmp.at("missing.cfg")));
}
