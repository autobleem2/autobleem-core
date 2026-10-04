//
// RaOptionsService: the game editor's per-game rows of a RetroArch game and the retroarch.cfg lines they mean.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/temp_dir.h"

#include "core/services/ra_options.h"

#include <string>
#include <vector>

using std::string;

namespace {

PsGame raGame(const string &image = "/media/RetroArch/roms/snes/rom.sfc") {
    PsGame game;
    game.foreign = true;
    game.image_path = image;
    return game;
}

string lineFor(const ableem::ConfigFileEditor::CfgLines &lines, const string &key) {
    for (const auto &line : lines) {
        if (line.first == key)
            return line.second;
    }
    return "";
}

} // namespace

TEST_CASE("every row starts at its first value: a game nobody edited writes no line") {
    RaGameOptions options;
    CHECK(options.isDefault());
    ableem::ConfigFileEditor::CfgLines lines;
    RaOptionsService::apply(options, ":/overlay/scanlines.cfg", lines);
    CHECK(lines.empty());
}

TEST_CASE("the aspect row is RetroArch 1.22.2's aspect_ratio_index") {
    CHECK(RaOptionsService::aspectIndexFor(RaGameOptions::AspectDefault) == "");
    CHECK(RaOptionsService::aspectIndexFor(RaGameOptions::AspectCore) == "22");  // ASPECT_RATIO_CORE
    CHECK(RaOptionsService::aspectIndexFor(RaGameOptions::Aspect43) == "0");     // ASPECT_RATIO_4_3
    CHECK(RaOptionsService::aspectIndexFor(RaGameOptions::AspectFull) == "24");  // ASPECT_RATIO_FULL
    CHECK(RaOptionsService::aspectIndexFor(RaGameOptions::AspectPixel) == "21"); // ASPECT_RATIO_SQUARE
}

TEST_CASE("each row becomes its retroarch.cfg line, and replaces the line the scaler put there") {
    RaGameOptions options;
    options.aspect = RaGameOptions::AspectFull;
    options.integerScaling = RaGameOptions::TriOn;
    options.smoothing = RaGameOptions::TriOff;
    options.showFps = RaGameOptions::TriOn;
    options.analogAsDpad = RaGameOptions::TriOn;
    ableem::ConfigFileEditor::CfgLines lines;
    lines.emplace_back("aspect_ratio_index", "aspect_ratio_index = \"0\"");
    lines.emplace_back("video_smooth", "video_smooth = \"true\"");

    RaOptionsService::apply(options, ":/overlay/scanlines.cfg", lines);

    CHECK(lineFor(lines, "aspect_ratio_index") == "aspect_ratio_index = \"24\"");
    CHECK(lineFor(lines, "video_smooth") == "video_smooth = \"false\"");
    CHECK(lineFor(lines, "video_scale_integer") == "video_scale_integer = \"true\"");
    CHECK(lineFor(lines, "fps_show") == "fps_show = \"true\"");
    CHECK(lineFor(lines, "input_player1_analog_dpad_mode") == "input_player1_analog_dpad_mode = \"1\"");
    CHECK(lines.size() == 5); // the two that were there, replaced - not doubled
}

TEST_CASE("scanlines: off, light and strong use the overlay the PS1 games use") {
    RaGameOptions options;
    options.scanlines = RaGameOptions::ScanOff;
    ableem::ConfigFileEditor::CfgLines off;
    RaOptionsService::apply(options, ":/overlay/scanlines.cfg", off);
    CHECK(lineFor(off, "input_overlay_enable") == "input_overlay_enable = \"false\"");
    CHECK(lineFor(off, "input_overlay") == "");

    options.scanlines = RaGameOptions::ScanLight;
    ableem::ConfigFileEditor::CfgLines light;
    RaOptionsService::apply(options, ":/overlay/scanlines.cfg", light);
    CHECK(lineFor(light, "input_overlay") == "input_overlay = \":/overlay/scanlines.cfg\"");
    CHECK(lineFor(light, "input_overlay_enable") == "input_overlay_enable = \"true\"");
    CHECK(lineFor(light, "input_overlay_opacity") == "input_overlay_opacity = \"0.250000\"");

    options.scanlines = RaGameOptions::ScanStrong;
    ableem::ConfigFileEditor::CfgLines strong;
    RaOptionsService::apply(options, ":/overlay/scanlines.cfg", strong);
    CHECK(lineFor(strong, "input_overlay_opacity") == "input_overlay_opacity = \"0.600000\"");
}

TEST_CASE("the analog stick row writes 0 for off, 1 for the left stick") {
    RaGameOptions options;
    options.analogAsDpad = RaGameOptions::TriOff;
    ableem::ConfigFileEditor::CfgLines lines;
    RaOptionsService::apply(options, ":/overlay/scanlines.cfg", lines);
    CHECK(lineFor(lines, "input_player1_analog_dpad_mode") == "input_player1_analog_dpad_mode = \"0\"");
}

TEST_CASE("the options encode to seven numbers and decode back; bad text is the default") {
    RaGameOptions options;
    options.aspect = RaGameOptions::AspectPixel;
    options.integerScaling = RaGameOptions::TriOff;
    options.smoothing = RaGameOptions::TriOn;
    options.scanlines = RaGameOptions::ScanStrong;
    options.showFps = RaGameOptions::TriOn;
    options.analogAsDpad = RaGameOptions::TriOff;
    options.resume = RaGameOptions::ResumeNever;
    CHECK(options.encode() == "4,2,1,3,1,2,2");
    CHECK(RaGameOptions::decode(options.encode()) == options);

    CHECK(RaGameOptions::decode("").isDefault());
    CHECK(RaGameOptions::decode("junk").isDefault());
    CHECK(RaGameOptions::decode("9,9,9,9,9,9,9").isDefault()); // out of every row's range
    CHECK(RaGameOptions::decode("1").aspect == RaGameOptions::AspectCore);
    CHECK(RaGameOptions::decode("1").resume == RaGameOptions::ResumeAsk); // the rest missing: the default
}

TEST_CASE("the options are kept per game in System/ra-game-options.txt and survive a restart") {
    TempDir tmp("raoptions");
    EnvFixture env;
    env.setUsbRoot(tmp.path());

    RaOptionsService service;
    const PsGame sfc = raGame();
    const PsGame md = raGame("/media/RetroArch/roms/md/Sonic.md");
    CHECK(service.get(sfc).isDefault());

    RaGameOptions options;
    options.smoothing = RaGameOptions::TriOff;
    options.resume = RaGameOptions::ResumeLast;
    service.set(sfc, options);
    CHECK(service.get(sfc) == options);
    CHECK(service.get(md).isDefault()); // another game keeps its own
    CHECK(tmp.readFile("System/ra-game-options.txt") == "0,0,2,0,0,0,1\t" + sfc.image_path + "\n");

    RaOptionsService again; // a new start
    CHECK(again.get(sfc) == options);

    // back to all-default: the game's line goes, and the file with the last one
    again.set(sfc, RaGameOptions());
    CHECK(again.get(sfc).isDefault());
    CHECK_FALSE(ableem::DirEntry::exists(tmp.at("System/ra-game-options.txt")));
}

TEST_CASE("an App, one of our own games and a game with no file take no options") {
    TempDir tmp("raoptions2");
    EnvFixture env;
    env.setUsbRoot(tmp.path());
    RaOptionsService service;
    RaGameOptions options;
    options.showFps = RaGameOptions::TriOn;

    PsGame app = raGame();
    app.app = true;
    service.set(app, options);
    PsGame ps1;
    ps1.image_path = "x";
    service.set(ps1, options);
    PsGame none = raGame("");
    service.set(none, options);

    CHECK(service.get(app).isDefault());
    CHECK(service.get(ps1).isDefault());
    CHECK(service.get(none).isDefault());
    CHECK_FALSE(ableem::DirEntry::exists(tmp.at("System/ra-game-options.txt")));
}
