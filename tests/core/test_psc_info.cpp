//
// CoreInfoTable and the ROM-folder pass on the console's real core set: tests/data/psc-info is the info/ folder
// of the cores tarball the PSC installs (the .info files as fixed at the source), with an empty fake .so beside
// each - what the launcher's scan sees on a stick with the whole pack.
//
#include "doctest/doctest.h"

#include "../support/psc_info_tree.h"

#include <ableem/engine/retroarch_cores.h>
#include <ableem/engine/retroarch_scanner.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using ableem::CoreInfos;
using ableem::CoreInfoTable;
using ableem::DirEntry;
using ableem::RetroArchScanner;
using std::string;
using std::vector;

namespace {

vector<string> stems(const CoreInfos &cores) {
    vector<string> out;
    for (const auto &core : cores)
        out.push_back(core->stem);
    return out;
}

} // namespace

TEST_CASE("the fixture is the console's pack: every .info has its core, and the stems are the file names") {
    PscInfoTree ra;
    CHECK(ra.infoCount == 166);
    CoreInfoTable cores;
    cores.load(ra.retroarch(), "");
    CHECK(cores.cores().size() == 166);

    ra.tmp.writeFile("cores.cfg", "Nintendo - Super Nintendo Entertainment System = km_snes9x2010\n");
    cores.load(ra.retroarch(), ra.tmp.at("cores.cfg"));
    auto core = cores.coreForDatabase("Nintendo - Super Nintendo Entertainment System");
    REQUIRE(core);
    CHECK(core->stem == "km_snes9x2010");
    CHECK(core->core_path == ra.tmp.at("retroarch/cores/km_snes9x2010_libretro.so"));
}

TEST_CASE("an exact stem picks the core, never a sibling build whose name contains it") {
    PscInfoTree ra;
    ra.tmp.writeFile("cores.cfg", "Nintendo - Nintendo Entertainment System = km_fceumm\n"
                                  "Nintendo - Super Nintendo Entertainment System = km_snes9x2010\n"
                                  "Nintendo - Nintendo 64 = km_glupen64\n"
                                  "MAME = km_mame2003_plus\n");
    CoreInfoTable cores;
    cores.load(ra.retroarch(), ra.tmp.at("cores.cfg"));
    // the fragment "km_FCEUmm" would also be in km_FCEUmm Legacy / Xtreme; the stem is not
    CHECK(cores.coreForDatabase("Nintendo - Nintendo Entertainment System")->stem == "km_fceumm");
    CHECK(cores.coreForDatabase("Nintendo - Super Nintendo Entertainment System")->stem == "km_snes9x2010");
    CHECK(cores.coreForDatabase("Nintendo - Nintendo 64")->stem == "km_glupen64");
    CHECK(cores.coreForDatabase("MAME")->stem == "km_mame2003_plus"); // its .info lists "MAME 2003-Plus", not "MAME"
}

TEST_CASE("a database nobody configured always gets the same core: most extensions, then the file stem") {
    PscInfoTree ra;
    CoreInfoTable cores;
    cores.load(ra.retroarch(), "");
    // Nintendo 64DD: seven cores with six extensions each
    CHECK(cores.coreForDatabase("Nintendo - Nintendo 64DD")->stem == "km_mupen64_plus");
    // Neo Geo CD: km_neocd and neocd tie on two
    CHECK(cores.coreForDatabase("SNK - Neo Geo CD")->stem == "km_neocd");
    // Mega Drive: the Genesis Plus GX family, most extensions first
    CHECK(cores.coreForDatabase("Sega - Mega Drive - Genesis")->stem == "genesis_plus_gx");
}

TEST_CASE("coresForDatabase on the pack: the default first, the others by stem, none twice") {
    PscInfoTree ra;
    ra.tmp.writeFile("cores.cfg", "Nintendo - Super Nintendo Entertainment System = km_snes9x2010\n");
    CoreInfoTable cores;
    cores.load(ra.retroarch(), ra.tmp.at("cores.cfg"));

    CoreInfos snes = cores.coresForDatabase("Nintendo - Super Nintendo Entertainment System");
    REQUIRE(snes.size() == 12);
    CHECK(snes[0]->stem == "km_snes9x2010");
    const vector<string> all = stems(snes);
    const vector<string> rest(all.begin() + 1, all.end());
    CHECK(std::is_sorted(rest.begin(), rest.end()));
    CHECK(std::set<string>(rest.begin(), rest.end()).size() == rest.size());
    CHECK(std::find(rest.begin(), rest.end(), "km_snes9x2010_xtreme") != rest.end());
    CHECK(std::find(rest.begin(), rest.end(), "km_snes9x2010") == rest.end());

    // a platform with one core
    CHECK(stems(cores.coresForDatabase("Atari - ST")) == vector<string>{"km_hatari"});
    // a platform no core plays
    CHECK(cores.coresForDatabase("Nintendo - Wii").empty());
}

TEST_CASE("a scan on the pack makes the Amiga and Atari ST folders, and none for FFmpeg or MAME 2010") {
    PscInfoTree ra;
    CoreInfoTable cores;
    cores.load(ra.retroarch(), "");
    const std::set<string> skip{"ffmpeg", "mame 2010", "sony - playstation"};
    vector<string> created = RetroArchScanner::createMissingFolders(ra.roms(), cores, {}, skip);

    CHECK(DirEntry::isDirectory(ra.tmp.at("roms/Commodore - Amiga")));
    CHECK(DirEntry::isDirectory(ra.tmp.at("roms/Atari - ST")));
    CHECK(DirEntry::isDirectory(ra.tmp.at("roms/NEC - PC-8001 - PC-8801")));
    CHECK_FALSE(DirEntry::isDirectory(ra.tmp.at("roms/FFmpeg")));
    CHECK_FALSE(DirEntry::isDirectory(ra.tmp.at("roms/MAME 2010")));
    CHECK_FALSE(DirEntry::isDirectory(ra.tmp.at("roms/Sony - PlayStation")));
    CHECK(std::find(created.begin(), created.end(), "Commodore - Amiga") != created.end());
    CHECK(DirEntry::diru_DirsOnly(ra.roms()).size() == created.size());
}

TEST_CASE("shortName: what is inside the last parentheses of the display name, else the whole name") {
    auto shortOf = [](const string &name) {
        ableem::CoreInfo info;
        info.name = name;
        return info.shortName();
    };
    CHECK(shortOf("Nintendo - SNES (km_Snes9x 2010)") == "km_Snes9x 2010");
    CHECK(shortOf("Sony - PSX Peops(PCSX ReARMed)") == "PCSX ReARMed");
    CHECK(shortOf("Sony - PlayStation (PCSX ReARMed) [NEON Lightgun]") ==
          "Sony - PlayStation (PCSX ReARMed) [NEON Lightgun]");
    CHECK(shortOf("LowRes NX") == "LowRes NX");
    CHECK(shortOf("Empty ()") == "Empty ()");
    CHECK(shortOf("") == "");

    PscInfoTree ra;
    CoreInfoTable cores;
    cores.load(ra.retroarch(), "");
    // every core of the pack gives a short name that is not empty, so a Core row is never blank
    for (const auto &core : cores.cores())
        CHECK_FALSE(core->shortName().empty());
}

TEST_CASE("an .info key is read whole: database_match_archive_member is not the database line") {
    PscInfoTree ra;
    CoreInfoTable cores;
    cores.load(ra.retroarch(), "");
    // km_FinalBurn Neo's .info says `database = "FBA - Arcade Games"` and then `database_match_archive_member = "true"`
    CoreInfos fba = cores.coresForDatabase("FBA - Arcade Games");
    REQUIRE(fba.size() == 2);
    CHECK(fba[0]->stem == "km_fbneo");
    CHECK(fba[1]->stem == "km_fbneo_xtreme");
    CHECK(cores.databases().count("true") == 0);
}
