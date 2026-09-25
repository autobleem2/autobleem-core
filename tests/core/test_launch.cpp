//
// LaunchService: what a game launch hands to rc/launch.sh / rc/launch_rb.sh, and what happens around it.
//
#include "doctest/doctest.h"

#include "../support/fake_process_runner.h"
#include "../support/game_library_fixture.h"
#include "../support/tree_snapshot.h"
#include "../support/string_maker.h"

#include "core/services/launch.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using std::string;
using std::vector;

namespace {

bool contains(const string &text, const string &piece) {
    return text.find(piece) != string::npos;
}

// A library with one USB game and one internal game, the services a launch goes through, and a runner that
// records the argv instead of forking. config.ini is written per test through configure().
struct Launching : GameLibraryFixture {
    Launching() {
        addUsbGame(1, "Tekken 3");
        addSubDirRow(0, "Games", 0, 1);
        putGameInSubDirRow(0, 1);
        addInternalGame(10, "Jumping Flash");

        // MemcardManager::create copies these blanks into a new set
        tmp.makeSubDir("memcard");
        tmp.writeFile("memcard/card1.mcd", "blank one");
        tmp.writeFile("memcard/card2.mcd", "blank two");
        tmp.makeSubDir("Games/!MemCards");
        ableem::Environment::setWorkingPath(tmp.path());

        configure("");
    }

    // Config reads config.ini out of the working path and fills in its defaults; the selection script goes
    // to <usb root>/Autobleem/rc, which the fixture's root has
    void configure(const string &contents) {
        tmp.writeFile("config.ini", "Theme=aergb\n" + contents);
        tmp.makeSubDir("Autobleem/rc");
        config = std::make_unique<Config>();
        memcards = std::make_unique<MemcardService>(library);
        resumePoints = std::make_unique<ResumePointService>();
        service = std::make_unique<LaunchService>(*config, session, library, *memcards, *resumePoints, runner);
    }

    PsGamePtr usbGame() {
        PsGames games = PsGame::fromRecords(library.usbGames().loadUsbGames());
        REQUIRE(games.size() == 1);
        return games[0];
    }

    PsGamePtr internalGame() {
        PsGames games = PsGame::fromRecords(library.internalGames().loadInternalGames());
        REQUIRE(games.size() == 1);
        return games[0];
    }

    // a RetroArch playlist entry or an App: not one of ours, so no memory cards and no pcsx.cfg
    PsGamePtr foreignGame(bool app) {
        PsGamePtr game = std::make_shared<PsGame>();
        game->foreign = true;
        game->app = app;
        game->title = app ? "Some App" : "Some ROM";
        game->base = app ? "/media/Apps/SomeApp" : "rom";
        game->startup = "run.sh";
        game->image_path = "/media/RetroArch/roms/snes/rom.sfc";
        game->core_path = "/media/RetroArch/bin/cores/snes9x_libretro.so";
        return game;
    }

    string ssFile(const PsGamePtr &game, const string &relative) const {
        return game->ssFolder + ableem::sep + relative;
    }
    string read(const string &fullPath) const { return tmp.readFile(fullPath.substr(tmp.path().size() + 1)); }
    string rcScript(const string &name) const { return tmp.at("Autobleem/rc/" + name); }

    Session session;
    FakeProcessRunner runner;
    std::unique_ptr<Config> config;
    std::unique_ptr<MemcardService> memcards;
    std::unique_ptr<ResumePointService> resumePoints;
    std::unique_ptr<LaunchService> service;
};

} // namespace

// --- the selection script ---

TEST_CASE("writeSelectionScript records the menu choice and the settings the rc scripts read") {
    Launching lib;
    lib.session.menuOption = MENU_OPTION_START;

    lib.service->writeSelectionScript();

    // in the runtime dir (RAM on the console): a hand-over, not something to keep on the stick
    string script = lib.tmp.readFile("System/Runtime/autobleem_cfg.sh");
    CHECK(contains(script, "#!/bin/sh"));
    CHECK(contains(script, "AB_SELECTION=5"));
    CHECK(contains(script, "AB_THEME=aergb"));
    CHECK(contains(script, "AB_PCSX=bleemsync"));
}

// --- PCSX ---

TEST_CASE("PCSX is started through rc/launch.sh with the nine arguments the script reads") {
    Launching lib;
    PsGamePtr game = lib.usbGame();
    game->ssFolder += "/"; // as the database sometimes has it; the script must not get the slash

    lib.service->launch(game, EmuMode::Pcsx, -1);

    const FakeProcessRunner::Call &call = lib.runner.only();
    CHECK(call.exe == lib.rcScript("launch.sh"));
    CHECK(call.args == vector<string>{lib.tmp.at("Games/Tekken 3/sstates"),      // ssFolder
                                      lib.tmp.at("Games/Tekken 3/Tekken 3.cue"), // cdfile
                                      "2",                                       // lang (no lang file loaded)
                                      "2",                                       // region
                                      lib.tmp.at("Games/Tekken 3"),              // gameFolder
                                      "0",                                       // resume
                                      "0",                                       // aspect
                                      "0",                                       // filter
                                      "NA",                                      // pad
                                      "pcsx-abnxt",                              // emulator (the default)
                                      "English"});                               // language (the default)
    CHECK(game->ssFolder == lib.tmp.at("Games/Tekken 3/sstates"));               // normalised in place, as always

    CHECK(lib.usbGame()->last_played > 0); // the launch is the "last played" time
    // no selection around a game: one left over from it would hide a later crash from the rc scripts
    CHECK_FALSE(ableem::DirEntry::exists(lib.tmp.at("System/Runtime/autobleem_cfg.sh")));
}

TEST_CASE("the aspect comes from config.ini, the filter from the game's pcsx.cfg as Off/Linear/Sharp 0/1/2") {
    Launching lib;
    lib.configure("Aspect=true\n");
    PsGamePtr game = lib.usbGame();

    // launch.sh gets pcsx-abnxt's numbering as is and converts it for the classic pcsx-ab itself
    for (const char *mode : {"0", "1", "2"}) {
        lib.tmp.writeFile("Games/Tekken 3/pcsx.cfg", string("plat_target.hwfilter = ") + mode + "\n");
        lib.runner.calls.clear();
        lib.service->launch(game, EmuMode::Pcsx, -1);
        const vector<string> &args = lib.runner.only().args;
        CHECK(args[6] == "1");
        CHECK(args[7] == mode);
    }
    // no line, or a value out of range: Off
    lib.tmp.writeFile("Games/Tekken 3/pcsx.cfg", "plat_target.hwfilter = 7\n");
    lib.runner.calls.clear();
    lib.service->launch(game, EmuMode::Pcsx, -1);
    CHECK(lib.runner.only().args[7] == "0");
}

TEST_CASE("the classic pcsx-ab's -filter is the other way round and has no Sharp") {
    CHECK(LaunchService::pcsxAbFilter(0) == "1"); // Off -> nearest
    CHECK(LaunchService::pcsxAbFilter(1) == "0"); // Linear -> bilinear
    CHECK(LaunchService::pcsxAbFilter(2) == "1"); // Sharp -> nearest
}

TEST_CASE("the emulator argument is config.ini's choice between pcsx-ab and pcsx-abnxt") {
    Launching lib;
    lib.configure("Emulator=pcsx-ab\n");
    PsGamePtr game = lib.usbGame();

    lib.service->launch(game, EmuMode::Pcsx, -1);

    CHECK(lib.runner.only().args[9] == "pcsx-ab");
}

TEST_CASE("the language argument is config.ini's language, by the name of its lang file") {
    Launching lib;
    lib.configure("Language=Polski\n");
    PsGamePtr game = lib.usbGame();

    lib.service->launch(game, EmuMode::Pcsx, -1);

    CHECK(lib.runner.only().args[10] == "Polski");
}

TEST_CASE("a .pbp or .chd image is handed over as it is; anything else gets .cue") {
    Launching lib;
    PsGamePtr game = lib.usbGame();

    game->base = "Tekken 3.pbp";
    lib.service->launch(game, EmuMode::Pcsx, -1);
    CHECK(lib.runner.calls[0].args[1] == lib.tmp.at("Games/Tekken 3/Tekken 3.pbp"));

    game->base = "Tekken 3.chd";
    lib.service->launch(game, EmuMode::Pcsx, -1);
    CHECK(lib.runner.calls[1].args[1] == lib.tmp.at("Games/Tekken 3/Tekken 3.chd"));

    game->base = "Tekken 3";
    lib.service->launch(game, EmuMode::Pcsx, -1);
    CHECK(lib.runner.calls[2].args[1] == lib.tmp.at("Games/Tekken 3/Tekken 3.cue"));
}

TEST_CASE("resuming a slot hands PCSX the disc that slot was playing, and says so") {
    Launching lib;
    PsGamePtr game = lib.usbGame();
    // what ResumePointService kept for slot 1: the disc image on the first line, the state name on the second
    lib.tmp.writeFile("Games/Tekken 3/sstates/filename.1.txt.res", "/media/Games/Tekken 3/Disc 2.cue\nstate\n");

    lib.service->launch(game, EmuMode::Pcsx, 1);

    const vector<string> &args = lib.runner.only().args;
    CHECK(args[1] == lib.tmp.at("Games/Tekken 3/Disc 2.cue"));
    CHECK(args[5] == "1");
    // the slot's disc note is also left where PCSX reads the "last disc" from
    CHECK(contains(lib.tmp.readFile("Games/Tekken 3/sstates/lastcdimg.txt"), lib.tmp.at("Games/Tekken 3/Disc 2.cue")));
}

TEST_CASE("an older emulator's autobleem.cfg becomes the game's own config, and pcsx.cfg is left alone") {
    Launching lib;
    PsGamePtr game = lib.usbGame();
    lib.tmp.writeFile("Games/Tekken 3/pcsx.cfg", "Bios = SET_BY_PCSX\nGpu3 = gpu_peops.so\n");
    lib.runner.whileRunning = [&] {
        lib.tmp.writeFile("Games/Tekken 3/sstates/autobleem.cfg", "Bios = /tmp/scph1001.bin\nGpu3 = builtin_gpu\n");
    };

    lib.service->launch(game, EmuMode::Pcsx, -1);

    CHECK_FALSE(ableem::DirEntry::exists(lib.tmp.at("Games/Tekken 3/sstates/autobleem.cfg")));
    string custom = lib.tmp.readFile("Games/Tekken 3/sstates/pcsx.custom.cfg");
    CHECK(contains(custom, "Bios = SET_BY_PCSX")); // what the old copy-back did to it
    CHECK(contains(custom, "Gpu3 = builtin_gpu"));
    CHECK(contains(lib.tmp.readFile("Games/Tekken 3/pcsx.cfg"), "Gpu3 = gpu_peops.so")); // AutoBleem's, untouched
}

TEST_CASE("an internal game's own config is under its !SaveStates folder too") {
    Launching lib;
    PsGamePtr game = lib.internalGame();
    lib.tmp.makeSubDir("Games/!SaveStates/10");
    lib.runner.whileRunning = [&] {
        lib.tmp.writeFile("Games/!SaveStates/10/autobleem.cfg", "Bios = /tmp/scph1001.bin\n");
    };

    lib.service->launch(game, EmuMode::Pcsx, -1);

    CHECK(lib.runner.only().args[0] == lib.tmp.at("Games/!SaveStates/10"));
    CHECK(contains(lib.tmp.readFile("Games/!SaveStates/10/pcsx.custom.cfg"), "Bios = SET_BY_PCSX"));
    CHECK_FALSE(ableem::DirEntry::exists(lib.tmp.at("Games/!SaveStates/10/autobleem.cfg")));
    CHECK(lib.internalGame()->last_played > 0);
}

TEST_CASE("the filter passed on is the game's own config's while it has one") {
    Launching lib;
    PsGamePtr game = lib.usbGame();
    lib.tmp.writeFile("Games/Tekken 3/pcsx.cfg", "plat_target.hwfilter = 1\n");
    lib.tmp.writeFile("Games/Tekken 3/sstates/pcsx.custom.cfg", "plat_target.hwfilter = 2\n");

    lib.service->launch(game, EmuMode::Pcsx, -1);
    CHECK(lib.runner.only().args[7] == "2");

    // a custom config without the key keeps AutoBleem's
    lib.tmp.writeFile("Games/Tekken 3/sstates/pcsx.custom.cfg", "scanlines = 1\n");
    lib.runner.calls.clear();
    lib.service->launch(game, EmuMode::Pcsx, -1);
    CHECK(lib.runner.only().args[7] == "1");
}

TEST_CASE("the game's memory-card set is in play while PCSX runs and is written back afterwards") {
    Launching lib;
    PsGamePtr game = lib.usbGame();
    lib.memcards->createCard("Fighting");
    lib.memcards->setCardForGame(*game, "Fighting");
    lib.tmp.writeFile("Games/!MemCards/Fighting/card1.mcd", "fighting saves");
    lib.tmp.writeFile("Games/Tekken 3/sstates/memcards/card1.mcd", "the game's own");

    string inPlay;
    lib.runner.whileRunning = [&] {
        inPlay = lib.tmp.readFile("Games/Tekken 3/sstates/memcards/card1.mcd");
        lib.tmp.writeFile("Games/Tekken 3/sstates/memcards/card1.mcd", "fighting saves + this run");
    };

    lib.service->launch(game, EmuMode::Pcsx, -1);

    CHECK(inPlay == "fighting saves");
    CHECK(lib.tmp.readFile("Games/!MemCards/Fighting/card1.mcd") == "fighting saves + this run");
    CHECK(lib.tmp.readFile("Games/Tekken 3/sstates/memcards/card1.mcd") == "the game's own");
}

// --- RetroArch ---

TEST_CASE("RetroArch is started through rc/launch_rb.sh with the image and the core for the game's GPU plugin") {
    Launching lib;
    lib.configure("Raconfig=false\n");
    PsGamePtr game = lib.usbGame();

    SUBCASE("no pcsx.cfg, or the built-in GPU: the NEON core") {
        lib.service->launch(game, EmuMode::RetroArch, -1);
        const FakeProcessRunner::Call &call = lib.runner.only();
        CHECK(call.exe == lib.rcScript("launch_rb.sh"));
        CHECK(call.args == vector<string>{lib.tmp.at("Games/Tekken 3/Tekken 3.cue"), "NEON"});
    }
    SUBCASE("the P.E.Op.S. plugin: the PEOPS core") {
        lib.tmp.writeFile("Games/Tekken 3/pcsx.cfg", "Gpu3 = gpu_peops.so\n");
        lib.service->launch(game, EmuMode::RetroArch, -1);
        CHECK(lib.runner.only().args[1] == "PEOPS");
    }
    SUBCASE("a multi-disc .m3u next to the image is preferred") {
        lib.tmp.writeFile("Games/Tekken 3/Tekken 3.m3u", "Tekken 3.cue\n");
        lib.service->launch(game, EmuMode::RetroArch, -1);
        CHECK(lib.runner.only().args[0] == lib.tmp.at("Games/Tekken 3/Tekken 3.m3u"));
    }

    // a RetroArch launch is a play too (it was not recorded as one until 2026-09-16)
    CHECK(lib.usbGame()->last_played > 0);
}

TEST_CASE("a playlist entry's launch records nothing: its id is a playlist index, not a database row") {
    Launching lib;
    lib.configure("Raconfig=false\n");
    PsGamePtr rom = lib.foreignGame(false);
    rom->gameId = 1; // the same number as Tekken 3's row, by coincidence - which is the point

    lib.service->launch(rom, EmuMode::RetroArch, -1);

    CHECK(lib.usbGame()->last_played == 0);
}

TEST_CASE("a foreign RetroArch game is its playlist image and its own core") {
    Launching lib;
    lib.configure("Raconfig=false\n");
    PsGamePtr game = lib.foreignGame(false);

    lib.service->launch(game, EmuMode::RetroArch, -1);

    CHECK(lib.runner.only().args ==
          vector<string>{"/media/RetroArch/roms/snes/rom.sfc", "/media/RetroArch/bin/cores/snes9x_libretro.so"});
}

TEST_CASE("the game's card1.mcd is RetroArch's .srm for the run, and what RetroArch saved comes back") {
    Launching lib;
    lib.configure("Raconfig=false\n");
    PsGamePtr game = lib.usbGame();
    lib.tmp.writeFile("Games/Tekken 3/sstates/memcards/card1.mcd", "the game's own");
    lib.tmp.writeFile("RetroArch/bin/saves/Tekken 3.srm", "whatever RetroArch had");

    string srmInPlay, srmBackup;
    lib.runner.whileRunning = [&] {
        srmInPlay = lib.tmp.readFile("RetroArch/bin/saves/Tekken 3.srm");
        srmBackup = lib.tmp.readFile("RetroArch/bin/saves/Tekken 3.srm.bak");
        lib.tmp.writeFile("RetroArch/bin/saves/Tekken 3.srm", "the game's own + this run");
    };

    lib.service->launch(game, EmuMode::RetroArch, -1);

    CHECK(srmInPlay == "the game's own");
    CHECK(srmBackup == "whatever RetroArch had");
    CHECK(lib.tmp.readFile("Games/Tekken 3/sstates/memcards/card1.mcd") == "the game's own + this run");
    CHECK(lib.tmp.readFile("RetroArch/bin/saves/Tekken 3.srm") == "whatever RetroArch had");
    CHECK_FALSE(ableem::DirEntry::exists(lib.tmp.at("RetroArch/bin/saves/Tekken 3.srm.bak")));
}

TEST_CASE("with raconfig on, the game's pcsx.cfg settings are RetroArch's for the run and are put back after") {
    Launching lib;
    lib.configure("Raconfig=true\nAspect=true\n");
    PsGamePtr game = lib.usbGame();
    lib.tmp.writeFile("Games/Tekken 3/pcsx.cfg",
                      "plat_target.hwfilter = 1\n" // Linear
                      "gpu_neon.enhancement_enable = 1\n"
                      "gpu_neon.enhancement_no_main = 0\n"
                      "psx_clock = 39\n" // 0x39 = 57
                      "gpu_peops.iUseDither = 1\n"
                      "spu_config.iUseInterpolation = 2\n"
                      "scanlines = 1\n"
                      "scanline_level = 32\n" // 0x32 = 50 -> opacity 0.5
                      "frameskip3 = 1\n");
    const string coreOptionsBefore = "pcsx_rearmed_neon_enhancement_enable = \"disabled\"\n"
                                     "pcsx_rearmed_neon_enhancement_no_main = \"enabled\"\n"
                                     "pcsx_rearmed_dithering = \"disabled\"\n"
                                     "pcsx_rearmed_psxclock = \"50\"\n"
                                     "pcsx_rearmed_spu_interpolation = \"off\"\n"
                                     "pcsx_rearmed_frameskip = \"0\"\n"
                                     "pcsx_rearmed_show_bios_bootlogo = \"disabled\"\n"
                                     "pcsx_rearmed_nocdaudio = \"disabled\"\n";
    const string raConfigBefore = "input_overlay = \"\"\n"
                                  "input_overlay_enable = \"false\"\n"
                                  "input_overlay_opacity = \"1.0\"\n"
                                  "custom_viewport_width = \"960\"\n"
                                  "custom_viewport_height = \"720\"\n"
                                  "custom_viewport_x = \"160\"\n"
                                  "custom_viewport_y = \"0\"\n"
                                  "aspect_ratio_index = \"0\"\n"
                                  "video_smooth = \"false\"\n";
    lib.tmp.writeFile("RetroArch/bin/config/retroarch-core-options.cfg", coreOptionsBefore);
    lib.tmp.writeFile("RetroArch/bin/retroarch.cfg", raConfigBefore);

    // RetroArch gets them through --appendconfig and a core-options copy, both in RAM (the runtime dir):
    // its own files on the stick are not touched for the run
    string coreOptionsInPlay, raConfigInPlay, stickCoreOptions, stickRaConfig;
    lib.runner.whileRunning = [&] {
        coreOptionsInPlay = lib.tmp.readFile("System/Runtime/ra-core-options.cfg");
        raConfigInPlay = lib.tmp.readFile("System/Runtime/ra-append.cfg");
        stickCoreOptions = lib.tmp.readFile("RetroArch/bin/config/retroarch-core-options.cfg");
        stickRaConfig = lib.tmp.readFile("RetroArch/bin/retroarch.cfg");
    };

    lib.service->launch(game, EmuMode::RetroArch, -1);
    CHECK(stickCoreOptions == coreOptionsBefore);
    CHECK(stickRaConfig == raConfigBefore);
    CHECK(contains(raConfigInPlay, "config_save_on_exit = \"true\"")); // rapersist's default
    CHECK(contains(raConfigInPlay, "core_options_path = \"" + lib.tmp.at("System/Runtime/ra-core-options.cfg") + "\""));

    // the core options, from the game's pcsx.cfg
    CHECK(contains(coreOptionsInPlay, "pcsx_rearmed_neon_enhancement_enable = \"enabled\""));
    CHECK(contains(coreOptionsInPlay, "pcsx_rearmed_neon_enhancement_no_main = \"disabled\""));
    CHECK(contains(coreOptionsInPlay, "pcsx_rearmed_dithering = \"enabled\""));
    CHECK(contains(coreOptionsInPlay, "pcsx_rearmed_psxclock = \"57\""));
    CHECK(contains(coreOptionsInPlay, "pcsx_rearmed_spu_interpolation = \"gaussian\""));
    CHECK(contains(coreOptionsInPlay, "pcsx_rearmed_frameskip = \"1\""));
    CHECK(contains(coreOptionsInPlay, "pcsx_rearmed_show_bios_bootlogo = \"enabled\"")); // no SlowBoot line = shown
    CHECK(contains(coreOptionsInPlay, "pcsx_rearmed_nocdaudio = \"enabled\""));
    // retroarch.cfg: the scanline overlay from pcsx.cfg, the viewport and filter from config.ini
    // "input_overlay" is a prefix of the next two keys; each line must be matched as a whole key or the
    // first replacement clobbers the other two (it did, until 2026-09-16)
    CHECK(contains(raConfigInPlay, "input_overlay = \":/overlay/scanlines.cfg\""));
    CHECK(contains(raConfigInPlay, "input_overlay_enable = \"true\""));
    CHECK(contains(raConfigInPlay, "input_overlay_opacity = \"0.500000\""));
    CHECK(contains(raConfigInPlay, "custom_viewport_width = \"1280\""));
    CHECK(contains(raConfigInPlay, "custom_viewport_x = \"0\""));
    CHECK(contains(raConfigInPlay, "aspect_ratio_index = \"23\""));
    CHECK(contains(raConfigInPlay, "video_smooth = \"true\"")); // the game's filter: Linear smooths

    // and afterwards both are exactly what they were
    CHECK(lib.tmp.readFile("RetroArch/bin/config/retroarch-core-options.cfg") == coreOptionsBefore);
    CHECK(lib.tmp.readFile("RetroArch/bin/retroarch.cfg") == raConfigBefore);
    CHECK_FALSE(ableem::DirEntry::exists(lib.tmp.at("RetroArch/bin/retroarch.cfg.bak")));
}

TEST_CASE("a foreign game with raconfig on still gets the viewport, but no core options and no filter") {
    Launching lib;
    lib.configure("Raconfig=true\nAspect=false\n");
    PsGamePtr game = lib.foreignGame(false);
    lib.tmp.writeFile("RetroArch/bin/config/retroarch-core-options.cfg", "pcsx_rearmed_psxclock = \"50\"\n");
    lib.tmp.writeFile("RetroArch/bin/retroarch.cfg", "custom_viewport_width = \"0\"\nvideo_smooth = \"true\"\n");

    string raConfigInPlay;
    lib.runner.whileRunning = [&] { raConfigInPlay = lib.tmp.readFile("System/Runtime/ra-append.cfg"); };

    lib.service->launch(game, EmuMode::RetroArch, -1);

    CHECK_FALSE(contains(raConfigInPlay, "core_options_path")); // no core options of the game's
    CHECK(contains(raConfigInPlay, "custom_viewport_width = \"960\""));
    CHECK_FALSE(contains(raConfigInPlay, "video_smooth")); // RetroArch's own - no pcsx.cfg to read
}

TEST_CASE("RetroArch saving its config keeps what the player changed and puts back what the launcher appended") {
    Launching lib;
    lib.configure("Raconfig=true\nAspect=true\n");
    PsGamePtr game = lib.foreignGame(false);
    const string before = "custom_viewport_width = \"960\"\n"
                          "aspect_ratio_index = \"0\"\n"
                          "menu_driver = \"xmb\"\n";
    lib.tmp.writeFile("RetroArch/bin/retroarch.cfg", before);

    // config_save_on_exit: RetroArch writes every setting it holds - ours from the append file included
    lib.runner.whileRunning = [&] {
        lib.tmp.writeFile("RetroArch/bin/retroarch.cfg", "custom_viewport_width = \"1280\"\n"
                                                         "aspect_ratio_index = \"22\"\n" // the player's change
                                                         "menu_driver = \"ozone\"\n"     // and another
                                                         "custom_viewport_height = \"720\"\n"
                                                         "custom_viewport_x = \"0\"\n"
                                                         "custom_viewport_y = \"0\"\n"
                                                         "config_save_on_exit = \"true\"\n");
    };
    lib.service->launch(game, EmuMode::RetroArch, -1);

    CHECK(lib.tmp.readFile("RetroArch/bin/retroarch.cfg") == "custom_viewport_width = \"960\"\n"
                                                             "aspect_ratio_index = \"22\"\n"
                                                             "menu_driver = \"ozone\"\n");
}

TEST_CASE("rapersist=false: RetroArch is told not to save its config at exit; nothing of its own is written") {
    Launching lib;
    lib.configure("Rapersist=false\nRaconfig=false\n");
    PsGamePtr game = lib.foreignGame(false);
    lib.tmp.writeFile("RetroArch/bin/retroarch.cfg", "menu_driver = \"xmb\"\n");

    test_support::TreeSnapshot before(lib.tmp.at("RetroArch"));
    lib.service->launch(game, EmuMode::RetroArch, -1);
    CHECK(lib.tmp.readFile("System/Runtime/ra-append.cfg") == "config_save_on_exit = \"false\"\n"
                                                              "content_runtime_log = \"false\"\n"
                                                              "content_runtime_log_aggregate = \"false\"\n");
    CHECK(before.changesTo(test_support::TreeSnapshot(lib.tmp.at("RetroArch"))) == vector<string>{});
}

// --- Apps ---

TEST_CASE("an App is its own startup script, run with no arguments and no memory cards") {
    Launching lib;
    PsGamePtr game = lib.foreignGame(true);

    lib.service->launch(game, EmuMode::Launcher, -1);

    const FakeProcessRunner::Call &call = lib.runner.only();
    CHECK(call.exe == "/media/Apps/SomeApp/run.sh");
    CHECK(call.args.empty());
}

namespace {
// a multi-platform App in the fixture's tree: Apps/Tyrian with a binary for this build's own key
PsGamePtr multiPlatformApp(Launching &lib, const string &extraIni = "") {
    const string key = Env::appPlatformKeys().front();
    lib.tmp.makeSubDir("Apps/Tyrian/bin/" + key);
    lib.tmp.writeFile("Apps/Tyrian/app.ini", "Title=OpenTyrian\n"
                                             "Exec=bin/{key}/tyrian\n"
                                             "Args=--data \"my data\" -f\n"
                                             "Lib=lib/{key}\n"
                                             "Env=SDL_AUDIODRIVER=alsa\n" +
                                                 extraIni);
    lib.tmp.writeFile("Apps/Tyrian/bin/" + key + "/tyrian", "x");
    PsGamePtr game = lib.foreignGame(true);
    game->base = lib.tmp.at("Apps/Tyrian");
    game->startup = "bin/" + key + "/tyrian";
    return game;
}

string envValue(const LaunchPlan &plan, const string &name) {
    for (const auto &kv : plan.env)
        if (kv.first == name)
            return kv.second;
    return "<unset>";
}
} // namespace

TEST_CASE("a multi-platform App runs through rc/app_run.sh with what its ini names for this machine") {
    Launching lib;
    PsGamePtr game = multiPlatformApp(lib);
    const string key = Env::appPlatformKeys().front();

    lib.service->launch(game, EmuMode::Launcher, -1);

    const FakeProcessRunner::Call &call = lib.runner.only();
    CHECK(call.exe == lib.rcScript("app_run.sh"));
    CHECK(call.args.empty());
    CHECK(call.cwd == lib.tmp.at("Apps/Tyrian"));
    CHECK(envValue(call, "AB_APP_DIR") == lib.tmp.at("Apps/Tyrian"));
    CHECK(envValue(call, "AB_APP_EXEC") == lib.tmp.at("Apps/Tyrian/bin/" + key + "/tyrian"));
    CHECK(envValue(call, "AB_APP_ARGS") == "--data \"my data\" -f");
    CHECK(envValue(call, "AB_APP_LIB") == lib.tmp.at("Apps/Tyrian/lib/" + key));
    CHECK(envValue(call, "AB_APP_KEY") == key);
    CHECK(envValue(call, "AB_PLATFORM") == string(Env::buildTargetKey()));
    CHECK(envValue(call, "AB_PLATFORM_KEYS").find(key) == 0);
    CHECK(envValue(call, "AB_ROOT") == Env::getPathToUSBRoot());
    CHECK(envValue(call, "SDL_AUDIODRIVER") == "alsa");
    CHECK(envValue(call, "AB_APP_VIRTUAL_PAD") == "1"); // no VirtualPad= in its ini: the mapper is on
}

TEST_CASE("a multi-platform App with a run.sh of its own runs that, with the same environment") {
    Launching lib;
    PsGamePtr game = multiPlatformApp(lib, "Startup=run.sh\n");
    lib.tmp.writeFile("Apps/Tyrian/run.sh", "#!/bin/sh\n");

    lib.service->launch(game, EmuMode::Launcher, -1);

    const FakeProcessRunner::Call &call = lib.runner.only();
    CHECK(call.exe == lib.tmp.at("Apps/Tyrian/run.sh"));
    CHECK(envValue(call, "AB_APP_EXEC") != "<unset>");
}

TEST_CASE("an App with no binary for this machine falls back to its Startup, as before") {
    Launching lib;
    lib.tmp.makeSubDir("Apps/Elsewhere/bin/nowhere");
    lib.tmp.writeFile("Apps/Elsewhere/app.ini", "Exec=bin/{key}/game\n");
    PsGamePtr game = lib.foreignGame(true);
    game->base = lib.tmp.at("Apps/Elsewhere");
    game->startup = "run.sh";

    LaunchPlan plan = LaunchService::planApp(*game);
    CHECK(plan.exe == lib.tmp.at("Apps/Elsewhere/run.sh"));
    CHECK(plan.env.empty());
}

TEST_CASE("which launcher runs is decided by the game first and the mode second") {
    Launching lib;
    lib.configure("Raconfig=false\n");

    PsGamePtr usb = lib.usbGame();
    lib.service->launch(usb, EmuMode::Pcsx, -1);
    lib.service->launch(usb, EmuMode::RetroArch, -1);
    PsGamePtr rom = lib.foreignGame(false);
    lib.service->launch(rom, EmuMode::Pcsx, -1); // a playlist entry can only go to RetroArch
    PsGamePtr app = lib.foreignGame(true);
    lib.service->launch(app, EmuMode::Pcsx, -1); // and an App only to its own script

    REQUIRE(lib.runner.calls.size() == 4);
    CHECK(lib.runner.calls[0].exe == lib.rcScript("launch.sh"));
    CHECK(lib.runner.calls[1].exe == lib.rcScript("launch_rb.sh"));
    CHECK(lib.runner.calls[2].exe == lib.rcScript("launch_rb.sh"));
    CHECK(lib.runner.calls[3].exe == "/media/Apps/SomeApp/run.sh");
}

// --- direct launches (the Windows product: launch_mode=direct, no rc scripts) ---

namespace {
// the Windows layout over the fixture's root: the emulator in <resources>/emu, RetroArch's official tree in
// <root>/RetroArch/bin with .dll cores
struct DirectLaunching : Launching {
    DirectLaunching() {
        Env::setDirectLaunch(true);
        Env::setPcsxDir(tmp.makeSubDir("program/emu"));
        Env::setPcsxNxtDir(tmp.makeSubDir("program/emunxt"));
        putEmulator("emu");
        putEmulator("emunxt");
        ableem::Environment::setRetroarchDir(tmp.makeSubDir("RetroArch/bin"));
        ableem::Environment::setRetroarchCoreExtension(".dll");
        ableem::Environment::setRetroarchCoreFile(tmp.at("RetroArch/bin/cores/pcsx_rearmed_libretro.dll"));
        Env::setRetroArchBinaries({tmp.at("RetroArch/bin/retroarch.exe")});
        tmp.writeFile("RetroArch/bin/retroarch.exe", "MZ");
    }
    // a pcsx-ab binary in <program>/<folder>, named as this build's LaunchService looks for it
    void putEmulator(const string &folder) {
        tmp.writeFile("program/" + folder + "/" + LaunchService::pcsxBinaryIn("x").substr(2), "MZ");
    }
    // the executable the plan names, without the .exe the Windows build adds
    static string stem(const string &exe) {
        return exe.size() > 4 && exe.compare(exe.size() - 4, 4, ".exe") == 0 ? exe.substr(0, exe.size() - 4) : exe;
    }
};
} // namespace

TEST_CASE("direct mode: the PS1 emulator itself - pcsx-ab in launch.sh's run directory, pcsx-abnxt by its options") {
    DirectLaunching lib;
    lib.configure("Aspect=true\nEmulator=pcsx-ab\n");
    PsGamePtr game = lib.usbGame();

    SUBCASE("a fresh start with pcsx-ab (Options' choice): the run directory laid out with directory links") {
        string linksSeen;
        lib.runner.whileRunning = [&] {
            // while the emulator runs: .pcsx is the save-state folder, bios the PS1 BIOS folder
            linksSeen = ableem::DirEntry::isDirectory(lib.tmp.at("System/runpcsx/.pcsx")) &&
                                ableem::DirEntry::exists(lib.tmp.at("System/runpcsx/.pcsx/pcsx.cfg")) &&
                                ableem::DirEntry::isDirectory(lib.tmp.at("System/runpcsx/bios"))
                            ? "linked"
                            : "not linked";
        };
        lib.tmp.writeFile("Games/Tekken 3/sstates/pcsx.cfg", "Gpu3 = x\n");
        lib.service->launch(game, EmuMode::Pcsx, -1);
        const FakeProcessRunner::Call &call = lib.runner.only();
        CHECK(DirectLaunching::stem(call.exe) == lib.tmp.at("program/emu/pcsx-ab"));
        CHECK(call.cwd == lib.tmp.at("System/runpcsx"));
        CHECK(call.args == vector<string>{"-filter", "1", "-ratio", "1", "-lang", "2", "-region", "4", "-enter", "1",
                                          "-cdfile", lib.tmp.at("Games/Tekken 3/Tekken 3.cue")});
        CHECK(linksSeen == "linked");
        // the links go after the run, the save states stay
        CHECK_FALSE(ableem::DirEntry::exists(lib.tmp.at("System/runpcsx/.pcsx")));
        CHECK(ableem::DirEntry::exists(lib.tmp.at("Games/Tekken 3/sstates/pcsx.cfg")));
    }
    SUBCASE("pcsx-abnxt (the default), run from its folder with -dotdir/-biosdir/-fullscreen") {
        lib.configure("Aspect=true\nEmulator=pcsx-abnxt\n");
        lib.service->launch(game, EmuMode::Pcsx, -1);
        const FakeProcessRunner::Call &call = lib.runner.only();
        CHECK(DirectLaunching::stem(call.exe) == lib.tmp.at("program/emunxt/pcsx-ab"));
        CHECK(call.cwd == lib.tmp.at("program/emunxt"));
        CHECK(call.args == vector<string>{"-dotdir", lib.tmp.at("Games/Tekken 3/sstates"), "-biosdir",
                                          lib.tmp.at("System/Bios"), "-filter", "0", "-ratio", "1", "-lang", "2",
                                          "-region", "4", "-enter", "1", "-language", "English", "-fullscreen",
                                          "-cdfile", lib.tmp.at("Games/Tekken 3/Tekken 3.cue")});
        CHECK_FALSE(ableem::DirEntry::exists(lib.tmp.at("System/runpcsx")));
    }
    SUBCASE("the game's filter: pcsx-abnxt gets it as is, pcsx-ab in its own numbering, Sharp as Off") {
        const char *const expectAb[] = {"1", "0", "1"};
        for (int mode = 0; mode <= 2; mode++) {
            lib.tmp.writeFile("Games/Tekken 3/pcsx.cfg", "plat_target.hwfilter = " + std::to_string(mode) + "\n");
            for (const char *emu : {"pcsx-abnxt", "pcsx-ab"}) {
                lib.configure(string("Emulator=") + emu + "\n");
                lib.runner.calls.clear();
                lib.service->launch(game, EmuMode::Pcsx, -1);
                const vector<string> &args = lib.runner.only().args;
                auto it = std::find(args.begin(), args.end(), "-filter");
                REQUIRE(it + 1 < args.end());
                CHECK(*(it + 1) == (string(emu) == "pcsx-abnxt" ? std::to_string(mode) : string(expectAb[mode])));
            }
        }
    }
    SUBCASE("a chosen emulator whose folder has no binary falls back to the other, as launch.sh does") {
        lib.configure("Emulator=pcsx-abnxt\n");
        ableem::DirEntry::removeDirAndContents(lib.tmp.at("program/emunxt"));
        lib.service->launch(game, EmuMode::Pcsx, -1);
        CHECK(DirectLaunching::stem(lib.runner.only().exe) == lib.tmp.at("program/emu/pcsx-ab"));
    }
    SUBCASE("neither emulator on this machine: RetroArch's PS1 core plays the game") {
        ableem::DirEntry::removeDirAndContents(lib.tmp.at("program/emu"));
        ableem::DirEntry::removeDirAndContents(lib.tmp.at("program/emunxt"));
        lib.service->launch(game, EmuMode::Pcsx, -1);
        const FakeProcessRunner::Call &call = lib.runner.only();
        CHECK(call.exe == lib.tmp.at("RetroArch/bin/retroarch.exe"));
        CHECK(call.args[5] == lib.tmp.at("RetroArch/bin/cores/pcsx_rearmed_libretro.dll"));
        CHECK(call.args[7] == lib.tmp.at("Games/Tekken 3/Tekken 3.cue"));
    }
    SUBCASE("resuming a slot adds -load") {
        lib.configure("Emulator=pcsx-abnxt\n");
        lib.service->launch(game, EmuMode::Pcsx, 2);
        const vector<string> &args = lib.runner.only().args;
        auto load = std::find(args.begin(), args.end(), "-load");
        REQUIRE(load != args.end());
        CHECK(*(load + 1) == "2");
    }
    SUBCASE("no selection script is written: nothing would source it") {
        lib.service->launch(game, EmuMode::Pcsx, -1);
        CHECK_FALSE(ableem::DirEntry::exists(lib.tmp.at("System/Runtime/autobleem_cfg.sh")));
    }
    SUBCASE("the game folder's pcsx.cfg is put next to the save states, as the scripts do") {
        lib.tmp.writeFile("Games/Tekken 3/pcsx.cfg", "Gpu3 = gpu_peops.so\n");
        lib.runner.whileRunning = [&] { CHECK(lib.read(lib.ssFile(game, "pcsx.cfg")) == "Gpu3 = gpu_peops.so\n"); };
        lib.service->launch(game, EmuMode::Pcsx, -1);
        CHECK(lib.runner.calls.size() == 1);
    }
}

TEST_CASE("direct mode: RetroArch itself with its config, the core and full screen") {
    DirectLaunching lib;
    lib.configure("Raconfig=false\n");

    SUBCASE("one of our PS1 games gets the platform's PS1 core, whichever GPU plugin it names") {
        PsGamePtr game = lib.usbGame();
        lib.tmp.writeFile("Games/Tekken 3/pcsx.cfg", "Gpu3 = gpu_peops.so\n");
        lib.service->launch(game, EmuMode::RetroArch, -1);
        const FakeProcessRunner::Call &call = lib.runner.only();
        CHECK(call.exe == lib.tmp.at("RetroArch/bin/retroarch.exe"));
        CHECK(call.cwd == lib.tmp.at("RetroArch/bin"));
        CHECK(call.args == vector<string>{"--config", lib.tmp.at("RetroArch/bin/retroarch.cfg"), "--appendconfig",
                                          lib.tmp.at("System/Runtime/ra-append.cfg"), "-L",
                                          lib.tmp.at("RetroArch/bin/cores/pcsx_rearmed_libretro.dll"), "--fullscreen",
                                          lib.tmp.at("Games/Tekken 3/Tekken 3.cue")});
    }
    SUBCASE("a playlist entry brings its own core") {
        PsGamePtr rom = lib.foreignGame(false);
        rom->core_path = lib.tmp.at("RetroArch/bin/cores/snes9x_libretro.dll");
        lib.service->launch(rom, EmuMode::Pcsx, -1);
        const FakeProcessRunner::Call &call = lib.runner.only();
        CHECK(call.args[5] == lib.tmp.at("RetroArch/bin/cores/snes9x_libretro.dll"));
        CHECK(call.args[7] == "/media/RetroArch/roms/snes/rom.sfc");
    }
    SUBCASE("no RetroArch binary installed: the plan names nothing to run") {
        Env::setRetroArchBinaries({lib.tmp.at("RetroArch/bin/missing.exe")});
        CHECK(LaunchService::retroArchExecutable().empty());
    }
}

TEST_CASE("direct mode: an App of the old kind (a Startup= script only) is not run - there is no sh") {
    DirectLaunching lib;
    PsGamePtr app = lib.foreignGame(true);
    lib.service->launch(app, EmuMode::Launcher, -1);
    CHECK(lib.runner.calls.empty());
}

TEST_CASE("direct mode: a multi-platform App is its program itself, with its Args split and its Lib on PATH") {
    DirectLaunching lib;
    PsGamePtr game = multiPlatformApp(lib);
    const string key = Env::appPlatformKeys().front();

    lib.service->launch(game, EmuMode::Launcher, -1);

    const FakeProcessRunner::Call &call = lib.runner.only();
    CHECK(call.exe == lib.tmp.at("Apps/Tyrian/bin/" + key + "/tyrian"));
    CHECK(call.args == vector<string>{"--data", "my data", "-f"});
    CHECK(call.cwd == lib.tmp.at("Apps/Tyrian"));
    const string path = envValue(call, "PATH");
    CHECK(path.find(lib.tmp.at("Apps/Tyrian/lib/" + key)) == 0);
    // then the launcher's own folder, whose SDL2.dll the Apps share
    CHECK(path.find(";" + Env::executableDir()) != string::npos);
    CHECK(envValue(call, "AB_APP_KEY") == key);
}

TEST_CASE("LaunchPlan::toString is the command on one line, the directory when there is one") {
    LaunchPlan plan{"C:/emu/pcsx-ab.exe", {"-cdfile", "Tekken 3.cue"}, "C:/emu", {}};
    CHECK(plan.toString() == "'C:/emu/pcsx-ab.exe' '-cdfile' 'Tekken 3.cue' (in C:/emu)");
    CHECK(LaunchPlan{"/bin/sh", {}, "", {}}.toString() == "'/bin/sh'");
    // an App's environment follows, one NAME=value each
    CHECK(LaunchPlan{"/bin/sh", {}, "", {{"AB_APP_KEY", "psc"}}}.toString() == "'/bin/sh' AB_APP_KEY=psc");
}

TEST_CASE("an emulator that lists them in abfeatures gets the card set in place, an exit dir and the slot to load") {
    Launching lib;
    lib.configure("Emulator=pcsx-abnxt\n");
    lib.tmp.writeFile("Autobleem/bin/emunxt/pcsx-ab", "binary");
    lib.tmp.writeFile("Autobleem/bin/emunxt/abfeatures", "# what it takes\nexitdir\nmemcarddir\nloadstate\n");
    PsGamePtr game = lib.usbGame();
    lib.memcards->createCard("Fighting");
    lib.memcards->setCardForGame(*game, "Fighting");
    // a kept slot 1 to resume from
    lib.tmp.writeFile("Games/Tekken 3/sstates/filename.txt.res",
                      lib.tmp.at("Games/Tekken 3/Tekken 3.cue") + "\nTEKKEN3\n");
    lib.tmp.writeFile("Games/Tekken 3/sstates/sstates/TEKKEN3.001.res", "kept state");

    test_support::TreeSnapshot before(lib.tmp.at("Games"));
    lib.service->launch(game, EmuMode::Pcsx, 1);

    const FakeProcessRunner::Call &call = lib.runner.only();
    auto env = [&call](const string &name) {
        for (const auto &e : call.env)
            if (e.first == name)
                return e.second;
        return string("-");
    };
    CHECK(env("AB_MEMCARD_DIR") == lib.tmp.at("Games/!MemCards/Fighting"));
    CHECK(env("AB_EXIT_DIR") == lib.tmp.at("System/Runtime/exit"));
    CHECK(env("AB_LOAD_STATE") == lib.tmp.at("Games/Tekken 3/sstates/sstates/TEKKEN3.001.res"));
    // no card copied in or out, no state copied to slot 0: only the slot's disc note, and last_played's row
    vector<string> changes = before.changesTo(test_support::TreeSnapshot(lib.tmp.at("Games")));
    CHECK(changes == vector<string>{"+ Tekken 3/sstates/lastcdimg.1.txt"});
}
