//
// Config: the defaults it fills in, the obsolete keys it drops, and the save/reload round trip.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/temp_dir.h"

#include "core/services/config.h"

#include <ableem/engine/ini_file.h>

using std::string;

namespace {

// reads the config.ini Config just wrote, so a test can assert on the file rather than on the live object
ableem::IniFile reloadFromDisk(const TempDir &tmp) {
    ableem::IniFile onDisk;
    onDisk.load(tmp.at("config.ini"));
    return onDisk;
}

} // namespace

TEST_CASE("Config fills in a default for every key the UI reads") {
    TempDir tmp("config_defaults");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    Config config;

    // the UI reads these without checking whether they are there, so all of them must have a value
    CHECK(config.inifile.values["language"] == "English");
    CHECK(config.inifile.values["aspect"] == "false");
    CHECK(config.inifile.values["jewel"] == "default");
    CHECK(config.inifile.values["music"] == "--");
    CHECK(config.inifile.values["showingtimeout"] == "2");
    CHECK(config.inifile.values["raconfig"] == "true");
    CHECK(config.inifile.values["emulator"] == "pcsx-abnxt");
    CHECK(config.inifile.values["padswap"] == "false");
}

TEST_CASE("Config: padswap defaults off and keeps an explicit true (C11)") {
    TempDir tmp("config_padswap");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    CHECK(Config().inifile.values["padswap"] == "false");

    tmp.writeFile("config.ini", "[General]\nPadswap=true\n");
    CHECK(Config().inifile.values["padswap"] == "true");

    // anything else - a stray value from hand-editing - is not kept as "true"
    tmp.writeFile("config.ini", "[General]\nPadswap=yes\n");
    CHECK(Config().inifile.values["padswap"] == "false");
}

TEST_CASE("Config: covershine defaults on and keeps an explicit false") {
    TempDir tmp("config_covershine");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    // nothing set: on
    CHECK(Config().inifile.values["covershine"] == "true");

    tmp.writeFile("config.ini", "[General]\nCovershine=false\n");
    CHECK(Config().inifile.values["covershine"] == "false");

    // anything else - a stray value from hand-editing - is not kept as "false"
    tmp.writeFile("config.ini", "[General]\nCovershine=no\n");
    CHECK(Config().inifile.values["covershine"] == "true");

    tmp.writeFile("config.ini", "[General]\nCovershine=true\n");
    CHECK(Config().inifile.values["covershine"] == "true");
}

TEST_CASE("Config keeps a known emulator and falls back to pcsx-abnxt for anything else") {
    TempDir tmp("config_emulator");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    tmp.writeFile("config.ini", "[General]\nEmulator=pcsx-ab\n");
    CHECK(Config().inifile.values["emulator"] == "pcsx-ab");

    // a name the launch scripts have no folder for (a typo, an emulator that was removed) is not passed on
    tmp.writeFile("config.ini", "[General]\nEmulator=pcsx-xyz\n");
    CHECK(Config().inifile.values["emulator"] == "pcsx-abnxt");
}

TEST_CASE("Config: the update channel - the three the site has, the old names migrated") {
    TempDir tmp("config_updates");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    // the channels before the site had three: stable is the release list, latest the pre-release
    tmp.writeFile("config.ini", "[General]\nUpdates=stable\n");
    CHECK(Config().inifile.values["updates"] == "release");
    CHECK(reloadFromDisk(tmp).values["updates"] == "release");
    tmp.writeFile("config.ini", "[General]\nUpdates=latest\n");
    CHECK(Config().inifile.values["updates"] == "testing");

    for (const char *channel : {"release", "testing", "nightly", "off"}) {
        tmp.writeFile("config.ini", string("[General]\nUpdates=") + channel + "\n");
        CHECK(Config().inifile.values["updates"] == channel);
    }

    // none set: one of the three, following the build
    tmp.writeFile("config.ini", "[General]\nLanguage=English\n");
    const string def = Config().inifile.values["updates"];
    CHECK((def == "release" || def == "testing" || def == "nightly"));
}

TEST_CASE("Config keeps what the file already says") {
    TempDir tmp("config_existing");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    tmp.writeFile("config.ini", "[General]\n"
                                "Theme=aergb\n"
                                "Language=Polish\n");

    Config config;

    // keys are lower-cased on load, values are not
    CHECK(config.inifile.values["theme"] == "aergb");
    CHECK(config.inifile.values["language"] == "Polish");

    // and the defaults still fill in around them
    CHECK(config.inifile.values["aspect"] == "false");
}

TEST_CASE("Config over an empty config.ini gives the shipped defaults, ab2.0.0 included") {
    // what an unclean unmount left on the first Pi boot: the file exists and holds nothing
    TempDir tmp("config_empty");
    EnvFixture env;
    env.setWorkingPath(tmp.path());
    tmp.writeFile("config.ini", "");

    Config config;

    CHECK(config.inifile.values["theme"] == "ab2.0.0");
    CHECK(config.inifile.values["language"] == "English");
    // and the file written back is a real one again (atomically: no .tmp left behind)
    CHECK(tmp.readFile("config.ini").find("Theme=ab2.0.0") != std::string::npos);
    CHECK_FALSE(DirEntry::exists(tmp.path() + sep + "config.ini.tmp"));
}

TEST_CASE("Config drops the keys older AutoBleem versions wrote") {
    TempDir tmp("config_obsolete");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    tmp.writeFile("config.ini",
                  "Theme=aergb\n"
                  "Stheme=old\n"
                  "Autoregion=1\n"
                  "Quick=1\n"
                  "Quickmenu=1\n"
                  "Delay=3\n"
                  "Adv=1\n"
                  "Mip=true\n"
                  "UI=classic\n"); // the classic UI is gone - see Session::MenuOption / GuiLauncher

    Config config;

    for (const char *gone : {"stheme", "autoregion", "quick", "quickmenu", "delay", "adv", "ui", "mip"}) {
        CHECK(config.inifile.values.count(gone) == 0);
    }
    CHECK(config.inifile.values["theme"] == "aergb");

    // they are gone from the file on disk too, not just from the live map
    ableem::IniFile onDisk = reloadFromDisk(tmp);
    for (const char *gone : {"stheme", "autoregion", "quick", "quickmenu", "delay", "adv", "ui", "mip"}) {
        CHECK(onDisk.values.count(gone) == 0);
    }
}

TEST_CASE("Config forces pcsx to bleemsync whatever the file says") {
    TempDir tmp("config_pcsx");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    tmp.writeFile("config.ini", "Pcsx=something-else\n");

    Config config;
    CHECK(config.inifile.values["pcsx"] == "bleemsync");

    config.save();
    CHECK(reloadFromDisk(tmp).values["pcsx"] == "bleemsync");
}

TEST_CASE("Config::save round trips through the file") {
    TempDir tmp("config_roundtrip");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    {
        Config config;
        config.inifile.values["theme"] = "evolution";
        config.inifile.values["showingtimeout"] = "7";
        config.save();
    }

    Config reloaded;
    CHECK(reloaded.inifile.values["theme"] == "evolution");
    CHECK(reloaded.inifile.values["showingtimeout"] == "7"); // not overwritten by the default
}

TEST_CASE("Config writes config.ini into the working path, not the current directory") {
    TempDir tmp("config_location");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    Config config;

    CHECK(ableem::DirEntry::exists(tmp.at("config.ini")));
}

TEST_CASE("Config: scaler migrates from the old aspect switch, only when scaler itself is absent") {
    // a file from before 2026-09-29: Aspect=true meant "fill the screen" (Options -> Widescreen)
    {
        TempDir tmp("config_scaler_migrate_true");
        EnvFixture env;
        env.setWorkingPath(tmp.path());
        tmp.writeFile("config.ini", "[General]\nAspect=true\n");
        CHECK(Config().inifile.values["scaler"] == "full");
    }
    // Aspect=false (or absent) meant the letterboxed 4:3 the emulator always drew
    {
        TempDir tmp("config_scaler_migrate_false");
        EnvFixture env;
        env.setWorkingPath(tmp.path());
        tmp.writeFile("config.ini", "[General]\nAspect=false\n");
        CHECK(Config().inifile.values["scaler"] == "4:3");
    }
    {
        TempDir tmp("config_scaler_migrate_none");
        EnvFixture env;
        env.setWorkingPath(tmp.path());
        tmp.writeFile("config.ini", "[General]\nLanguage=English\n"); // no Aspect key at all - an old install
        CHECK(Config().inifile.values["scaler"] == "4:3");
        CHECK(Config().inifile.values["aspect"] == "false"); // aspect still gets its own default too
    }
    // a file that already has a scaler value (this build, or a hand edit) keeps it - aspect is not consulted,
    // even when the two disagree
    {
        TempDir tmp("config_scaler_kept");
        EnvFixture env;
        env.setWorkingPath(tmp.path());
        tmp.writeFile("config.ini", "[General]\nAspect=true\nScaler=1x1\n");
        CHECK(Config().inifile.values["scaler"] == "1x1");
    }
    for (const char *value : {"1x1", "2x", "4:3", "4:3i", "full"}) {
        TempDir tmp(string("config_scaler_roundtrip_") + value);
        EnvFixture env;
        env.setWorkingPath(tmp.path());
        {
            Config config;
            config.inifile.values["scaler"] = value;
            config.save();
        }
        CHECK(Config().inifile.values["scaler"] == value); // survives a reload unmigrated
    }
}

TEST_CASE("Config: notification timeout defaults to 2s and keeps a 0 chosen after the migration") {
    TempDir tmp("config_showingtimeout_zero");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    // the default, nothing in the file
    CHECK(Config().inifile.values["showingtimeout"] == "2");

    // 0 ("don't show") is a real, saved value like any other 1..20 once the migration has run
    tmp.writeFile("config.ini", "[General]\nShowingtimeout=0\nShowingtimeoutmigrated=1\n");
    Config config;
    CHECK(config.inifile.values["showingtimeout"] == "0");
    config.save();
    CHECK(reloadFromDisk(tmp).values["showingtimeout"] == "0");

    // and the whole 1..20 range round-trips too
    for (int seconds = 1; seconds <= 20; ++seconds) {
        TempDir t2(string("config_showingtimeout_") + std::to_string(seconds));
        EnvFixture e2;
        e2.setWorkingPath(t2.path());
        t2.writeFile("config.ini", "[General]\nShowingtimeout=" + std::to_string(seconds) + "\n");
        CHECK(Config().inifile.values["showingtimeout"] == std::to_string(seconds));
    }
}

TEST_CASE("Config: an old showingtimeout=0 (which meant stay up) is converted to 2 once") {
    TempDir tmp("config_showingtimeout_migrate");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    // no marker: the 0 becomes the default and the marker is written
    tmp.writeFile("config.ini", "[General]\nShowingtimeout=0\n");
    {
        Config config;
        CHECK(config.inifile.values["showingtimeout"] == "2");
        CHECK(config.inifile.values["showingtimeoutmigrated"] == "1");
    }
    CHECK(reloadFromDisk(tmp).values["showingtimeout"] == "2");
    CHECK(reloadFromDisk(tmp).values["showingtimeoutmigrated"] == "1");

    // the user then chooses 0: it stays 0 through every later load
    {
        Config config;
        config.inifile.values["showingtimeout"] = "0";
        config.save();
    }
    CHECK(Config().inifile.values["showingtimeout"] == "0");
    CHECK(Config().inifile.values["showingtimeout"] == "0");

    // a fresh file gets the marker too, so a 0 chosen on it is kept
    tmp.writeFile("config.ini", "[General]\nLanguage=English\n");
    CHECK(Config().inifile.values["showingtimeoutmigrated"] == "1");

    // other values are untouched, marker or not
    for (const char *value : {"1", "2", "7", "20"}) {
        tmp.writeFile("config.ini", string("[General]\nShowingtimeout=") + value + "\n");
        CHECK(Config().inifile.values["showingtimeout"] == value);
    }
}

TEST_CASE("Config: the splash screen defaults on and keeps an explicit false") {
    TempDir tmp("config_splashscreen");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    CHECK(Config().inifile.values["splashscreen"] == "true");

    tmp.writeFile("config.ini", "[General]\nSplashscreen=false\n");
    CHECK(Config().inifile.values["splashscreen"] == "false");

    tmp.writeFile("config.ini", "[General]\nSplashscreen=no\n");
    CHECK(Config().inifile.values["splashscreen"] == "true");
}

TEST_CASE("Config: the animations (screen transitions) default on and keep an explicit false") {
    TempDir tmp("config_animations");
    EnvFixture env;
    env.setWorkingPath(tmp.path());

    CHECK(Config().inifile.values["animations"] == "true");

    tmp.writeFile("config.ini", "[General]\nAnimations=false\n");
    CHECK(Config().inifile.values["animations"] == "false");

    tmp.writeFile("config.ini", "[General]\nAnimations=maybe\n");
    CHECK(Config().inifile.values["animations"] == "true");
}

TEST_CASE("Config writes config.ini into the state dir when one is set apart from the working path") {
    TempDir tmp("config_state_dir");
    EnvFixture env;
    env.setWorkingPath(tmp.makeSubDir("program"));
    ableem::Environment::setStateDir(tmp.makeSubDir("data/System"));

    Config config;
    config.inifile.values["theme"] = "aergb";
    config.save();

    CHECK_FALSE(ableem::DirEntry::exists(tmp.at("program/config.ini")));
    CHECK(ableem::DirEntry::exists(tmp.at("data/System/config.ini")));
    Config again;
    CHECK(again.inifile.values["theme"] == "aergb");
}
