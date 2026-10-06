//
// AppSettings: the player's per-App choices (Game settings > Pad mode) and the mode an App is started with.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "../support/tree_snapshot.h"
#include "core/services/app_settings.h"
#include "core/main.h"

using namespace std;

TEST_CASE("AppSettings: the effective pad mode is the player's choice, else the app.ini value, else empty") {
    CHECK(AppSettings::effectivePadMode("x360", "psc") == "x360");               // the choice wins
    CHECK(AppSettings::effectivePadMode("", "psc-kernel") == "psc-kernel");      // Automatic: the App's own
    CHECK(AppSettings::effectivePadMode("", "") == "");                          // nothing: the old behaviour
    CHECK(AppSettings::effectivePadMode("", "  X360-Kernel ") == "x360-kernel"); // spelled loosely in an ini
    CHECK(AppSettings::effectivePadMode("", "ps4") == "");            // an unknown app.ini value counts as none
    CHECK(AppSettings::effectivePadMode("nonsense", "psc") == "psc"); // an unknown choice is Automatic
}

TEST_CASE("AppSettings: the pad mode override round-trips through the App's folder") {
    TempDir tmp("appsettings");
    const string app = tmp.makeSubDir("Apps/t");

    CHECK(AppSettings::padModeOverride(app) == ""); // no file: Automatic
    for (const string &mode : AppSettings::padModes()) {
        REQUIRE(AppSettings::setPadModeOverride(app, mode));
        CHECK(AppSettings::padModeOverride(app) == mode);
    }
    CHECK(tmp.readFile("Apps/t/ab_settings.ini") == "PadMode=x360-kernel\n"); // one line, the last choice

    REQUIRE(AppSettings::setPadModeOverride(app, "")); // back to Automatic
    CHECK(AppSettings::padModeOverride(app) == "");
    CHECK_FALSE(DirEntry::exists(tmp.at("Apps/t/ab_settings.ini"))); // nothing chosen, no file

    REQUIRE(AppSettings::setPadModeOverride(app, "bogus")); // unknown = Automatic, no file either
    CHECK_FALSE(DirEntry::exists(tmp.at("Apps/t/ab_settings.ini")));
}

TEST_CASE("AppSettings: a file written by hand is read loosely and other lines survive a change") {
    TempDir tmp("appsettings");
    const string app = tmp.makeSubDir("Apps/t");
    tmp.writeFile("Apps/t/ab_settings.ini", "Other=1\r\n padmode = PSC \r\n");
    CHECK(AppSettings::padModeOverride(app) == "psc");

    REQUIRE(AppSettings::setPadModeOverride(app, "x360"));
    CHECK(tmp.readFile("Apps/t/ab_settings.ini") == "Other=1\nPadMode=x360\n");
    REQUIRE(AppSettings::setPadModeOverride(app, ""));
    CHECK(tmp.readFile("Apps/t/ab_settings.ini") == "Other=1\n"); // the file stays: it holds something else

    CHECK_FALSE(AppSettings::setPadModeOverride("", "psc")); // no folder, nothing written
}

TEST_CASE("AppSettings: the d-pad / stick flags - the player's choice, else the app.ini value, else empty") {
    CHECK(AppSettings::normalizeFlag(" On ") == "1");
    CHECK(AppSettings::normalizeFlag("FALSE") == "0");
    CHECK(AppSettings::normalizeFlag("maybe") == "");
    CHECK(AppSettings::effectiveFlag("0", "1") == "0");  // the choice wins
    CHECK(AppSettings::effectiveFlag("", "yes") == "1"); // Automatic: the App's own
    CHECK(AppSettings::effectiveFlag("", "") == "");     // nothing: the pad output's default

    TempDir tmp("appsettings");
    const string app = tmp.makeSubDir("Apps/t");
    CHECK(AppSettings::flagOverride(app, AppSettings::Dpad2AnalogKey) == "");
    REQUIRE(AppSettings::setPadModeOverride(app, "psc-kernel"));
    REQUIRE(AppSettings::setFlagOverride(app, AppSettings::Dpad2AnalogKey, "1"));
    REQUIRE(AppSettings::setFlagOverride(app, AppSettings::Analog2DpadKey, "off"));
    CHECK(tmp.readFile("Apps/t/ab_settings.ini") == "PadMode=psc-kernel\nDpad2Analog=1\nAnalog2Dpad=0\n");
    CHECK(AppSettings::flagOverride(app, AppSettings::Dpad2AnalogKey) == "1");
    CHECK(AppSettings::flagOverride(app, AppSettings::Analog2DpadKey) == "0");
    CHECK(AppSettings::padModeOverride(app) == "psc-kernel"); // one key's change leaves the others alone

    REQUIRE(AppSettings::setFlagOverride(app, AppSettings::Dpad2AnalogKey, ""));
    REQUIRE(AppSettings::setFlagOverride(app, AppSettings::Analog2DpadKey, ""));
    REQUIRE(AppSettings::setPadModeOverride(app, ""));
    CHECK_FALSE(DirEntry::exists(tmp.at("Apps/t/ab_settings.ini"))); // all Automatic: no file
}

TEST_CASE("AppSettings: LastPackage round-trips, an identical second write leaves the file alone, and the file goes") {
    TempDir tmp("appsettings");
    const string app = tmp.makeSubDir("Apps/crispy");
    CHECK(AppSettings::lastPackage(app) == "");

    REQUIRE(AppSettings::setLastPackage(app, "u/doom/doom2"));
    CHECK(AppSettings::lastPackage(app) == "u/doom/doom2");
    CHECK(tmp.readFile("Apps/crispy/ab_settings.ini") == "LastPackage=u/doom/doom2\n");

    // the same pick again: not a byte, not a modification time
    test_support::TreeSnapshot before(tmp.path());
    REQUIRE(AppSettings::setLastPackage(app, "u/doom/doom2"));
    CHECK(before.changesTo(test_support::TreeSnapshot(tmp.path())).empty());

    // another pick replaces it; the pad settings in the same file stay
    REQUIRE(AppSettings::setPadModeOverride(app, "psc"));
    REQUIRE(AppSettings::setLastPackage(app, "freedoom/freedoom1"));
    CHECK(tmp.readFile("Apps/crispy/ab_settings.ini") == "PadMode=psc\nLastPackage=freedoom/freedoom1\n");
    CHECK(AppSettings::padModeOverride(app) == "psc");

    // nothing left in the file: no file
    REQUIRE(AppSettings::setPadModeOverride(app, ""));
    REQUIRE(AppSettings::setLastPackage(app, ""));
    CHECK_FALSE(DirEntry::exists(tmp.at("Apps/crispy/ab_settings.ini")));
    CHECK_FALSE(AppSettings::setLastPackage("", "x")); // no folder, nothing written
}
