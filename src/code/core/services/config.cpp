//
// Created by screemer on 23.12.18.
//

#include "config.h"
#include "system.h"
#include "../main.h"
#include "../model/timing.h"
#include "core/version.h"
#include "environment.h"

//*******************************
// Config::Config()
//*******************************
Config::Config() {
    std::string path = Env::getPathToStateDir() + sep + "config.ini";
    inifile.load(path);

    // these are no longer used
    inifile.values.erase("stheme");
    inifile.values.erase("autoregion");
    inifile.values.erase("quick");
    inifile.values.erase("quickmenu");
    inifile.values.erase("delay");
    inifile.values.erase("adv");
    inifile.values.erase("ui");      // the classic UI is gone: the app always shows the EvolutionUI launcher now
    inifile.values.erase("version"); // the build says what version it is (core/version.h) since 2026-09-18
    inifile.values.erase("cfg");     // the selection script is <rc>/autobleem_cfg.sh (LaunchService), not a key
    inifile.values.erase("mip"); // the filter is per game since 2026-09-24 (pcsx.cfg plat_target.hwfilter, the editor)

    if (inifile.values["language"] == "") {
        inifile.values["language"] = "English";
    }
    // the shipped config.ini says ab2 too; this is for a config.ini that is missing or came back empty (an
    // unclean unmount on the first Pi boot did that) - the launcher should still come up in its own theme
    if (inifile.values["theme"] == "") {
        inifile.values["theme"] = "ab2";
    }
    if (inifile.values["aspect"] == "") {
        inifile.values["aspect"] = "false";
    }
    // which PS1 emulator a game starts in (Options -> "PS1 Emulator"): pcsx-abnxt, the next one
    // (Autobleem/bin/emunxt) - the default on every build since 2026-09-21 - or pcsx-ab, the one AutoBleem
    // has always shipped (Autobleem/bin/emu); the launch scripts get the name as their last argument. Both
    // read the same pcsx.cfg and memory cards; a resume point written by one does not load in the other
    // (different save-state versions), the game then starts fresh.
    if (inifile.values["emulator"] != "pcsx-ab" && inifile.values["emulator"] != "pcsx-abnxt") {
        inifile.values["emulator"] = "pcsx-abnxt";
    }
    if (inifile.values["jewel"] == "") {
        inifile.values["jewel"] = "default";
    }
    if (inifile.values["music"] == "") {
        inifile.values["music"] = "--";
    }
    if (inifile.values["showingtimeout"] == "") {
        inifile.values["showingtimeout"] = DefaultShowingTimeoutText;
    }

    if (inifile.values["raconfig"] == "") {
        inifile.values["raconfig"] = "true";
    }
    // Options -> "Persist RetroArch config": RetroArch's config_save_on_exit, handed to it on every start
    // (LaunchService::prepareRaAppend) - on, RetroArch keeps what the player changes in it, as it always
    // did; off, it writes nothing at exit and "Save Current Configuration" is the way to keep a change
    if (inifile.values["rapersist"] != "false") {
        inifile.values["rapersist"] = "true";
    }
    // the scan may fetch missing box art (and the databases) from libretro's servers, where the platform
    // has a download_command and the server answers; Options -> "Fetch box art online"
    if (inifile.values["online"] == "") {
        inifile.values["online"] = "true";
    }
    // the launcher's online update check (UpdateService, Options -> "Updates"): off | release | testing |
    // nightly - the download site's three channels. The default follows the build: a development build (git
    // describe past its tag) the nightlies, a pre-release the pre-releases, a release the releases. The two
    // older names are migrated (stable = release, latest = testing).
    std::string &updates = inifile.values["updates"];
    if (updates == "stable" || updates == "latest") {
        updates = updates == "stable" ? "release" : "testing";
    }
    if (updates == "") {
        updates = Version::isBetweenTags() ? "nightly" : Version::isPreRelease() ? "testing" : "release";
    }
    // the classic screens' font: the theme's, unless "themefont" is off and "font" names a .ttf/.otf from
    // retroarch/fonts, resources/fonts or the theme's own folder (Options -> Font; "--" is the theme's)
    if (inifile.values["themefont"] == "") {
        inifile.values["themefont"] = "true";
    }
    if (inifile.values["font"] == "") {
        inifile.values["font"] = "--";
    }

    if (inifile.values["surprisehighscore"] == "") {
        inifile.values["surprisehighscore"] = "0";
    }

    // "Keep logs on the stick" (Options): off unless asked for - the logs live in RAM (docs/quiet-stick-plan.md).
    // A tester's System/Logs/keep marker is the same switch, so it shows as on
    if (DirEntry::exists(Env::keepLogsMarkerFile()))
        inifile.values["keeplogs"] = "true";
    else if (inifile.values["keeplogs"] != "true")
        inifile.values["keeplogs"] = "false";

    inifile.values["pcsx"] = "bleemsync";

    // once, and only when a key was dropped or a default filled in: save() leaves an unchanged file alone
    save();
}

//*******************************
// Config::save
//*******************************
void Config::save() {
    inifile.values["pcsx"] = "bleemsync";
    std::string path = Env::getPathToStateDir() + sep + "config.ini";
    inifile.save(path);
}
