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

TEST_CASE("a second start leaves config.ini alone" * doctest::should_fail()) { // until plan step 2.2
    TempDir tmp("quiet_config");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    Config{}; // the first start fills in the defaults and writes them - a real change
    TreeSnapshot before(tmp.path());
    Config{};
    CHECK(before.changesTo(TreeSnapshot(tmp.path())) == vector<string>{});
}

TEST_CASE("saving an unchanged ini file leaves it alone" * doctest::should_fail()) { // until plan step 2.2
    TempDir tmp("quiet_ini");
    tmp.writeFile("Game.ini", "[Game]\nTitle=Crash Bandicoot\nFavorite=0\n");

    ableem::IniFile ini;
    ini.load(tmp.at("Game.ini"));
    ini.save(tmp.at("Game.ini")); // normalises the layout once
    TreeSnapshot before(tmp.path());
    ini.save(tmp.at("Game.ini"));
    CHECK(before.changesTo(TreeSnapshot(tmp.path())) == vector<string>{});
}

TEST_CASE("setting a cfg line to the value it already has leaves the file alone" *
          doctest::should_fail()) { // until plan step 2.3
    TempDir tmp("quiet_cfg");
    tmp.writeFile("retroarch.cfg", "video_smooth = \"false\"\naspect_ratio_index = \"22\"\n");

    TreeSnapshot before(tmp.path());
    ableem::ConfigFileEditor().replaceInFile(tmp.at("retroarch.cfg"), "video_smooth", "video_smooth = \"false\"");
    CHECK(before.changesTo(TreeSnapshot(tmp.path())) == vector<string>{});
}

TEST_CASE("a rescan with nothing changed writes nothing" * doctest::should_fail()) { // until plan steps 2.4-2.5
    GameLibraryFixture fx;
    fx.env.setWorkingPath(fx.tmp.path()); // the state dir: config, fingerprints, the scan's own files
    ScanService svc(fx.library);
    test_support::makeFakeGame(fx.tmp.at("Games"), "Crash Bandicoot", "SLUS_012.34");
    test_support::makeFakeGame(fx.tmp.at("Games"), "Spyro", "SLUS_012.35");

    svc.runScan();
    REQUIRE(svc.poll().addedGames.size() == 2);

    TreeSnapshot before(fx.tmp.path());
    svc.runScan();
    svc.poll();
    CHECK(before.changesTo(TreeSnapshot(fx.tmp.path())) == vector<string>{});
}
