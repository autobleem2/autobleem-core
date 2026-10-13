//
// GameLibrary: the RetroArch playlist export.
//
#include "doctest/doctest.h"

#include "../support/game_library_fixture.h"

#include <ableem/engine/retroarch_playlist.h>

#include <map>
#include <string>

using std::string;

namespace {

// the exported AutoBleem.lpl as label -> path
std::map<string, string> exportedPaths(GameLibraryFixture &lib) {
    lib.tmp.makeSubDir("RetroArch/bin/playlists");
    REQUIRE(lib.library.exportToRetroArchPlaylist());
    ableem::RetroArchPlaylistEntries entries;
    REQUIRE(ableem::RetroArchPlaylist::load(lib.tmp.at("RetroArch/bin/playlists/AutoBleem.lpl"), entries));
    std::map<string, string> paths;
    for (const auto &e : entries)
        paths[e.label] = e.path;
    return paths;
}

} // namespace

TEST_CASE("the RetroArch playlist names a cue game's .cue and a PBP's or a CHD's own file") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Cue Game"); // disc name "Cue Game" -> Cue Game.cue
    lib.addUsbGame(2, "Pbp Game");
    lib.library.usbGames().replaceDiscs(2, {"Pbp Game.pbp"});
    lib.addUsbGame(3, "Chd Game");
    lib.library.usbGames().replaceDiscs(3, {"Chd Game.chd"}); // used to come out as "Chd Game.chd.cue"

    auto paths = exportedPaths(lib);
    REQUIRE(paths.size() == 3);
    CHECK(paths["Cue Game"] == lib.tmp.at("Games/Cue Game") + "/" + "Cue Game.cue");
    CHECK(paths["Pbp Game"] == lib.tmp.at("Games/Pbp Game") + "/" + "Pbp Game.pbp");
    CHECK(paths["Chd Game"] == lib.tmp.at("Games/Chd Game") + "/" + "Chd Game.chd");
}

TEST_CASE("a game folder with a <base>.m3u is exported as the .m3u, for a CHD too") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Multi");
    lib.library.usbGames().replaceDiscs(1, {"Multi (Disc 1).chd", "Multi (Disc 2).chd"});
    lib.tmp.writeFile("Games/Multi/Multi (Disc 1).m3u", "Multi (Disc 1).chd\nMulti (Disc 2).chd\n");

    auto paths = exportedPaths(lib);
    CHECK(paths["Multi"] == lib.tmp.at("Games/Multi") + "/" + "Multi (Disc 1).m3u");
}

TEST_CASE("without a RetroArch playlists dir the export writes nothing and says so") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Tekken 3");
    CHECK_FALSE(lib.library.exportToRetroArchPlaylist());
    CHECK_FALSE(ableem::DirEntry::exists(lib.tmp.at("RetroArch/bin/playlists/AutoBleem.lpl")));
}

TEST_CASE("the RetroArch playlist names the PS1 core: the user's pick, else the platform's default") {
    GameLibraryFixture lib;
    lib.addUsbGame(1, "Tekken 3");
    for (const char *stem : {"pcsx_rearmed", "swanstation"}) {
        lib.tmp.writeFile(string("RetroArch/bin/info/") + stem + "_libretro.info",
                          "display_name = \"Sony - PlayStation (" + string(stem) +
                              ")\"\nsupported_extensions = \"cue\"\ndatabase = \"Sony - PlayStation\"\n");
        lib.tmp.writeFile(string("RetroArch/bin/cores/") + stem + "_libretro.so", "core");
    }
    ableem::Environment::setRetroarchCoreFile(lib.tmp.at("RetroArch/bin/cores/pcsx_rearmed_libretro.so"));
    ableem::Environment::setStateDir(lib.tmp.path());
    auto exportedCore = [&] {
        lib.tmp.makeSubDir("RetroArch/bin/playlists");
        REQUIRE(lib.library.exportToRetroArchPlaylist());
        ableem::RetroArchPlaylistEntries entries;
        REQUIRE(ableem::RetroArchPlaylist::load(lib.tmp.at("RetroArch/bin/playlists/AutoBleem.lpl"), entries));
        REQUIRE(entries.size() == 1);
        return entries[0].core_path;
    };
    CHECK(exportedCore() == lib.tmp.at("RetroArch/bin/cores/pcsx_rearmed_libretro.so"));
    lib.tmp.writeFile("cores.user.cfg", "Sony - PlayStation=swanstation\n");
    CHECK(exportedCore() == lib.tmp.at("RetroArch/bin/cores/swanstation_libretro.so"));
}
