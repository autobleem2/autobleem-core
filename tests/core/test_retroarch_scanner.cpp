//
// RetroArchScanner: the offline scan of RetroArch's ROM folders into playlists, and the CoreInfoTable it
// takes its system table from. Every tree is built in a TempDir; archives through ZipWriter.
//
#include "doctest/doctest.h"

#include "../support/env_fixture.h"
#include "../support/rdb_builder.h"
#include "../support/string_maker.h"
#include "../support/temp_dir.h"

#include <ableem/engine/crc32.h>
#include <ableem/engine/filesystem.h>
#include <ableem/engine/game_scanner.h>
#include <ableem/engine/retroarch_cores.h>
#include <ableem/engine/retroarch_playlist.h>
#include <ableem/engine/retroarch_scanner.h>
#include <ableem/engine/zip_archive.h>
#include <ableem/engine/zip_writer.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using ableem::CoreInfoPtr;
using ableem::CoreInfos;
using ableem::CoreInfoTable;
using ableem::Crc32;
using ableem::DirEntry;
using ableem::RetroArchPlaylist;
using ableem::RetroArchPlaylistEntries;
using ableem::RetroArchPlaylistEntry;
using ableem::RetroArchPlaylistHeader;
using ableem::RetroArchScanner;
using ableem::RetroArchScanResult;
using ableem::RetroArchSystem;
using ableem::RetroArchSystems;
using ableem::ScannedRom;
using ableem::ScannedRoms;
using ableem::ScanStage;
using ableem::ZipWriter;
using std::string;
using std::vector;

namespace {

// a .zip at `path` holding the given entries (name -> bytes)
void writeZip(const string &path, const vector<std::pair<string, string>> &entries) {
    ZipWriter zip;
    REQUIRE(zip.open(path));
    for (const auto &e : entries)
        REQUIRE(zip.addBytes(e.first, e.second));
    REQUIRE(zip.close());
}

RetroArchSystem nesSystem() {
    RetroArchSystem nes;
    nes.name = "Nintendo - Nintendo Entertainment System";
    nes.coreName = "Nintendo - NES / Famicom (Nestopia UE)";
    nes.corePath = "/media/retroarch/cores/nestopia_libretro.so";
    nes.extensions = {"nes", "fds", "unf", "unif"};
    return nes;
}

RetroArchSystem cdSystem() {
    RetroArchSystem sega;
    sega.name = "Sega - Mega-CD - Sega CD";
    sega.coreName = "Sega - MS/GG/MD/CD (Genesis Plus GX)";
    sega.corePath = "/media/retroarch/cores/genesis_plus_gx_libretro.so";
    sega.extensions = {"bin", "cue", "iso", "chd", "m3u", "ccd"};
    return sega;
}

// as fbneo's .info really reads: "zip" among the extensions, no block_extract line
RetroArchSystem arcadeSystem() {
    RetroArchSystem fbneo;
    fbneo.name = "FBNeo - Arcade Games";
    fbneo.coreName = "Arcade (FinalBurn Neo)";
    fbneo.corePath = "/media/retroarch/cores/fbneo_libretro.so";
    fbneo.extensions = {"zip", "7z", "cue", "ccd"};
    return fbneo;
}

const RetroArchPlaylistEntry *findByLabel(const RetroArchPlaylistEntries &entries, const string &label) {
    for (const auto &e : entries) {
        if (e.label == label)
            return &e;
    }
    return nullptr;
}

// a path as a JSON string body: the temp dir may hold backslashes (C:\msys64\tmp on the MSYS2 host)
string jsonPath(const string &path) {
    string out;
    for (char c : path) {
        if (c == '\\' || c == '"')
            out += '\\';
        out += c;
    }
    return out;
}

// the playlist entries of a folder scan, and the reverse for a merge() test that starts from entries
RetroArchPlaylistEntries entriesOf(const ableem::ScannedRoms &roms) {
    RetroArchPlaylistEntries out;
    for (const auto &r : roms)
        out.push_back(r.entry);
    return out;
}
ableem::ScannedRoms scanned(const RetroArchPlaylistEntries &entries, bool identified = false) {
    ableem::ScannedRoms out;
    for (const auto &e : entries) {
        ableem::ScannedRom r;
        r.entry = e;
        r.identified = identified;
        out.push_back(r);
    }
    return out;
}

vector<string> labelsOf(const RetroArchPlaylistEntries &entries) {
    vector<string> out;
    for (const auto &e : entries)
        out.push_back(e.label);
    return out;
}

struct RecordingListener : ableem::ScanProgressListener {
    void onScanProgress(ScanStage stage, const string &detail, int done, int total) override {
        stages.push_back(stage);
        details.push_back(detail);
        dones.push_back(done);
        totals.push_back(total);
    }
    vector<ScanStage> stages;
    vector<string> details;
    vector<int> dones, totals;
};

// a RetroArch tree with an info/ + cores/ pair per core and a roms/ dir, the scanner pointed at them
struct RomsTree {
    RomsTree() : tmp("rascan") {
        tmp.makeSubDir("retroarch/info");
        tmp.makeSubDir("retroarch/cores");
        tmp.makeSubDir("retroarch/playlists");
        tmp.makeSubDir("roms");
        options.romsDir = tmp.at("roms");
        options.playlistsDir = tmp.at("retroarch/playlists");
    }

    void addCore(const string &file, const string &displayName, const string &extensions, const string &database,
                 bool blockExtract = false, bool installed = true) {
        string info = "display_name = \"" + displayName + "\"\nsupported_extensions = \"" + extensions +
                      "\"\ndatabase = \"" + database + "\"\n";
        if (blockExtract)
            info += "block_extract = \"true\"\n";
        tmp.writeFile("retroarch/info/" + file + ".info", info);
        if (installed)
            tmp.writeFile("retroarch/cores/" + file + ".so", "core");
    }

    CoreInfoTable cores(const string &coresCfg = "") {
        CoreInfoTable table;
        table.load(tmp.at("retroarch"), coresCfg);
        return table;
    }

    string playlist(const string &system) const { return tmp.at("retroarch/playlists/" + system + ".lpl"); }

    RetroArchPlaylistEntries loadPlaylist(const string &system, RetroArchPlaylistHeader *header = nullptr) const {
        RetroArchPlaylistEntries entries;
        RetroArchPlaylist::load(playlist(system), entries, header);
        return entries;
    }

    string readFile(const string &relative) const { return tmp.readFile(relative); }

    TempDir tmp;
    RetroArchScanner::Options options;
};

const char *const NES = "Nintendo - Nintendo Entertainment System";
const char *const SNES = "Nintendo - Super Nintendo Entertainment System";

} // namespace

//*******************************
// CoreInfoTable
//*******************************
TEST_CASE("CoreInfoTable: an .info's fields, block_extract included; only installed cores count") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds|unf|unif", NES);
    t.addCore("fbneo_libretro", "Arcade (FinalBurn Neo)", "zip|7z", "FBNeo - Arcade Games", true);
    t.addCore("mesen_libretro", "Nintendo - NES / Famicom (Mesen)", "nes|fds", NES, false, false);

    CoreInfoTable cores = t.cores();
    REQUIRE(cores.cores().size() == 2);
    CHECK(cores.databases().size() == 2);

    CoreInfoPtr nes = cores.coreForDatabase(NES);
    REQUIRE(nes);
    CHECK(nes->name == "Nintendo - NES / Famicom (Nestopia UE)");
    CHECK(nes->core_path == t.tmp.at("retroarch/cores/nestopia_libretro.so"));
    CHECK(nes->extensions == vector<string>{"nes", "fds", "unf", "unif"});
    CHECK_FALSE(nes->block_extract);

    CoreInfoPtr fbneo = cores.coreForDatabase("FBNeo - Arcade Games.lpl"); // the .lpl suffix is ignored
    REQUIRE(fbneo);
    CHECK(fbneo->block_extract);

    CHECK(cores.coreForDatabase("Sony - PlayStation") == nullptr);
}

TEST_CASE("CoreInfoTable: the core with the most extensions is the default, a cores.cfg overrides it") {
    RomsTree t;
    t.addCore("bsnes_libretro", "Nintendo - SNES / SFC (bsnes)", "sfc",
              "Nintendo - Super Nintendo Entertainment System");
    t.addCore("snes9x_libretro", "Nintendo - SNES / SFC (Snes9x)", "sfc|smc|fig",
              "Nintendo - Super Nintendo Entertainment System");

    CHECK(t.cores().coreForDatabase("Nintendo - Super Nintendo Entertainment System")->name ==
          "Nintendo - SNES / SFC (Snes9x)");

    t.tmp.writeFile("cores.cfg", "# comment\n\nNintendo - Super Nintendo Entertainment System = bsnes\n");
    CoreInfoTable cores = t.cores(t.tmp.at("cores.cfg"));
    CHECK(cores.coreForDatabase("Nintendo - Super Nintendo Entertainment System")->name ==
          "Nintendo - SNES / SFC (bsnes)");
    CHECK(cores.overrideCoreFor("nintendo - super nintendo entertainment system")->name ==
          "Nintendo - SNES / SFC (bsnes)");
    CHECK(cores.defaultCoreFor("Nintendo - Super Nintendo Entertainment System")->name ==
          "Nintendo - SNES / SFC (Snes9x)");
}

TEST_CASE("systemsFrom: one system per database an installed core plays") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    t.addCore("fbneo_libretro", "Arcade (FinalBurn Neo)", "zip|7z", "FBNeo - Arcade Games|FBNeo - Arcade Games (Other)",
              true);

    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());
    REQUIRE(systems.size() == 3);
    auto find = [&](const string &name) -> const RetroArchSystem * {
        for (const auto &s : systems)
            if (s.name == name)
                return &s;
        return nullptr;
    };
    REQUIRE(find(NES));
    CHECK(find(NES)->coreName == "Nintendo - NES / Famicom (Nestopia UE)");
    CHECK(find(NES)->corePath == t.tmp.at("retroarch/cores/nestopia_libretro.so"));
    CHECK(find(NES)->extensions == vector<string>{"nes", "fds"});
    CHECK_FALSE(find(NES)->blockExtract);
    REQUIRE(find("FBNeo - Arcade Games"));
    CHECK(find("FBNeo - Arcade Games")->blockExtract);
}

//*******************************
// ZipArchive::listEntries
//*******************************
TEST_CASE("ZipArchive::listEntries gives each entry's CRC and size from the central directory") {
    TempDir tmp("zipcrc");
    writeZip(tmp.at("a.zip"), {{"game.nes", "rom"}, {"readme.txt", "other rom"}});

    vector<ableem::ZipEntry> entries;
    REQUIRE(ableem::ZipArchive::listEntries(tmp.at("a.zip"), entries));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "game.nes");
    CHECK(entries[0].crc == 0x79520FA1u); // crc32("rom")
    CHECK(entries[0].size == 3);
    CHECK(entries[1].crc == 0x089A93F8u); // crc32("other rom")
    CHECK_FALSE(ableem::ZipArchive::listEntries(tmp.at("missing.zip"), entries));
}

//*******************************
// scanFolder
//*******************************
TEST_CASE("scanFolder: a zip with one ROM is 'zip#rom' named after the zip, with the ROM's CRC") {
    TempDir tmp("scanfolder");
    tmp.makeSubDir("nes");
    writeZip(tmp.at("nes/Adventures of Lolo (USA).zip"), {{"Adventures of Lolo (USA).nes", "rom"}});

    RetroArchPlaylistEntries entries =
        entriesOf(RetroArchScanner::scanFolder(tmp.at("nes"), "/media/roms/nes", nesSystem()));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].path == "/media/roms/nes/Adventures of Lolo (USA).zip#Adventures of Lolo (USA).nes");
    CHECK(entries[0].label == "Adventures of Lolo (USA)");
    CHECK(entries[0].crc32 == "79520FA1|crc");
    CHECK(entries[0].core_name == "Nintendo - NES / Famicom (Nestopia UE)");
    CHECK(entries[0].core_path == "/media/retroarch/cores/nestopia_libretro.so");
    CHECK(entries[0].db_name == "Nintendo - Nintendo Entertainment System.lpl");
}

TEST_CASE("scanFolder: a pack of several ROMs is several games named after each; a zip with none is skipped") {
    TempDir tmp("scanfolder");
    tmp.makeSubDir("nes");
    writeZip(tmp.at("nes/pack.zip"),
             {{"Alpha (USA).nes", "rom"}, {"notes.txt", "x"}, {"Beta (Europe).nes", "other rom"}});
    writeZip(tmp.at("nes/bios.zip"), {{"disksys.rom", "rom"}}); // .rom is not an NES extension
    tmp.writeFile("nes/broken.zip", "this is not a zip");

    RetroArchPlaylistEntries entries =
        entriesOf(RetroArchScanner::scanFolder(tmp.at("nes"), tmp.at("nes"), nesSystem()));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].path == tmp.at("nes/pack.zip") + "#Alpha (USA).nes");
    CHECK(entries[0].label == "Alpha (USA)");
    CHECK(entries[1].path == tmp.at("nes/pack.zip") + "#Beta (Europe).nes");
    CHECK(entries[1].label == "Beta (Europe)");
    CHECK(entries[1].crc32 == "089A93F8|crc");
}

TEST_CASE("scanFolder: a loose ROM is a plain entry; other files are ignored; sub-folders are walked") {
    TempDir tmp("scanfolder");
    tmp.makeSubDir("nes/Homebrew");
    tmp.writeFile("nes/Zelda.NES", "rom"); // any case
    tmp.writeFile("nes/Zelda.sav", "save");
    tmp.writeFile("nes/readme.txt", "text");
    tmp.writeFile("nes/.hidden.nes", "rom");
    tmp.writeFile("nes/Homebrew/Micro Mages.nes", "rom");

    RetroArchPlaylistEntries entries =
        entriesOf(RetroArchScanner::scanFolder(tmp.at("nes"), "/media/roms/nes", nesSystem()));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].path == "/media/roms/nes/Homebrew/Micro Mages.nes");
    CHECK(entries[0].label == "Micro Mages");
    CHECK(entries[0].crc32 == "00000000|crc");
    CHECK(entries[1].path == "/media/roms/nes/Zelda.NES");
    CHECK(entries[1].label == "Zelda");
}

TEST_CASE("scanFolder: a cue hides its bins, an m3u hides its discs, a ccd hides its img") {
    TempDir tmp("scanfolder");
    tmp.makeSubDir("cd");
    tmp.writeFile("cd/Sonic CD (USA).cue", "FILE \"Sonic CD (USA) (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n"
                                           "FILE \"Sonic CD (USA) (Track 2).bin\" BINARY\n  TRACK 02 AUDIO\n");
    tmp.writeFile("cd/Sonic CD (USA) (Track 1).bin", "data");
    tmp.writeFile("cd/Sonic CD (USA) (Track 2).bin", "audio");
    tmp.writeFile("cd/Lunar (USA).m3u", "Lunar (USA) (Disc 1).cue\r\nLunar (USA) (Disc 2).cue\r\n");
    tmp.writeFile("cd/Lunar (USA) (Disc 1).cue", "FILE \"Lunar (USA) (Disc 1).bin\" BINARY\n");
    tmp.writeFile("cd/Lunar (USA) (Disc 1).bin", "d1");
    tmp.writeFile("cd/Lunar (USA) (Disc 2).cue", "FILE \"lunar (usa) (disc 2).bin\" BINARY\n"); // case differs
    tmp.writeFile("cd/Lunar (USA) (Disc 2).bin", "d2");
    tmp.writeFile("cd/Popful Mail (USA).ccd", "[CloneCD]\nVersion=3\n");
    tmp.writeFile("cd/Popful Mail (USA).img", "img");
    tmp.writeFile("cd/Popful Mail (USA).sub", "sub");
    tmp.writeFile("cd/Loose Track.bin", "a lone bin is a game of its own");

    RetroArchPlaylistEntries entries = entriesOf(RetroArchScanner::scanFolder(tmp.at("cd"), "/r/cd", cdSystem()));
    CHECK(labelsOf(entries) == vector<string>{"Loose Track", "Lunar (USA)", "Popful Mail (USA)", "Sonic CD (USA)"});
    CHECK(findByLabel(entries, "Lunar (USA)")->path == "/r/cd/Lunar (USA).m3u");
    CHECK(findByLabel(entries, "Sonic CD (USA)")->path == "/r/cd/Sonic CD (USA).cue");
    CHECK(findByLabel(entries, "Popful Mail (USA)")->path == "/r/cd/Popful Mail (USA).ccd");
}

TEST_CASE("scanFolder: a core that reads archives itself gets the zip whole") {
    TempDir tmp("scanfolder");
    tmp.makeSubDir("arcade");
    writeZip(tmp.at("arcade/mslug.zip"), {{"201-c1.c1", "rom"}, {"201-c2.c2", "rom"}});
    tmp.writeFile("arcade/neogeo.zip", "not even a zip - the core is the one to complain");

    RetroArchPlaylistEntries entries =
        entriesOf(RetroArchScanner::scanFolder(tmp.at("arcade"), "/r/arcade", arcadeSystem()));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].path == "/r/arcade/mslug.zip");
    CHECK(entries[0].label == "mslug");
    CHECK(entries[0].crc32 == "00000000|crc");
    CHECK(entries[1].path == "/r/arcade/neogeo.zip");

    // block_extract alone says the same
    RetroArchSystem flagged = nesSystem();
    flagged.blockExtract = true;
    CHECK(flagged.readsArchives());
    CHECK_FALSE(nesSystem().readsArchives());
    CHECK(arcadeSystem().readsArchives());
}

TEST_CASE("scan: an aliased folder feeds its database's playlist; two folders may share one") {
    RomsTree t;
    t.addCore("fbneo_libretro", "Arcade (FinalBurn Neo)", "zip|7z|cue|ccd", "FBNeo - Arcade Games");
    t.tmp.writeFile("roms/Arcade/mslug.zip", "set");
    t.tmp.writeFile("roms/SNK - Neo Geo/aof.zip", "set");
    t.tmp.writeFile("aliases.cfg", "# folders\nArcade = FBNeo - Arcade Games\nSNK - Neo Geo=FBNeo - Arcade Games\n");
    t.options.folderAliases = RetroArchScanner::loadFolderAliases(t.tmp.at("aliases.cfg"));
    REQUIRE(t.options.folderAliases.size() == 2);
    CHECK(RetroArchScanner::loadFolderAliases(t.tmp.at("missing.cfg")).empty());
    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());

    RetroArchScanner scanner;
    RetroArchScanResult result = scanner.scan(t.options, systems);
    CHECK(result.systemsScanned == 2);
    CHECK(result.gamesFound == 2);
    CHECK(result.playlistsWritten == vector<string>{"FBNeo - Arcade Games.lpl"});
    CHECK(result.unknownFolders.empty());
    RetroArchPlaylistEntries entries = t.loadPlaylist("FBNeo - Arcade Games");
    CHECK(labelsOf(entries) == vector<string>{"aof", "mslug"});
    CHECK(entries[0].path == t.tmp.at("roms/SNK - Neo Geo/aof.zip"));
    CHECK(entries[1].path == t.tmp.at("roms/Arcade/mslug.zip"));
    CHECK(scanner.scan(t.options, systems).playlistsWritten.empty());

    // a set gone from one folder is dropped by that folder's pass, the other folder's entries untouched
    DirEntry::removeFile(t.tmp.at("roms/Arcade/mslug.zip"));
    CHECK(scanner.scan(t.options, systems).playlistsWritten.size() == 1);
    CHECK(labelsOf(t.loadPlaylist("FBNeo - Arcade Games")) == vector<string>{"aof"});
}

//*******************************
// merge
//*******************************
TEST_CASE("merge: foreign entries stay, a vanished file's entry goes, an existing entry beats ours, new ones join") {
    TempDir tmp("merge");
    tmp.makeSubDir("roms/nes");
    tmp.writeFile("roms/nes/Kept.nes", "rom");
    tmp.writeFile("roms/nes/New.nes", "rom");
    writeZip(tmp.at("roms/nes/Identified.zip"), {{"Identified.nes", "rom"}});

    auto entry = [](const string &path, const string &label, const string &crc = "00000000|crc") {
        RetroArchPlaylistEntry e;
        e.path = path;
        e.label = label;
        e.core_path = "DETECT";
        e.core_name = "DETECT";
        e.crc32 = crc;
        e.db_name = "Nintendo - Nintendo Entertainment System.lpl";
        return e;
    };
    const string source = tmp.at("roms/nes");
    const string target = "/media/roms/nes";

    RetroArchPlaylistEntries existing = {
        entry("/home/user/Downloads/Elsewhere.nes", "Elsewhere"),       // not under our folder: the user's
        entry(target + "/Gone.nes", "Gone"),                            // file no longer there
        entry(target + "/Kept.nes", "Kept As Written", "AAAAAAAA|crc"), // still there: kept exactly
        entry(source + "/Identified.zip#Identified.nes", "Identified (USA)", "79520FA1|crc"), // by the source path
    };
    RetroArchPlaylistEntries fresh = {
        entry(target + "/Identified.zip#Identified.nes", "Identified", "79520FA1|crc"),
        entry(target + "/Kept.nes", "Kept"),
        entry(target + "/New.nes", "New"),
    };

    RetroArchPlaylistEntries merged = RetroArchScanner::merge(existing, scanned(fresh), source, target);
    CHECK(labelsOf(merged) == vector<string>{"Elsewhere", "Identified (USA)", "Kept As Written", "New"});
    CHECK(findByLabel(merged, "Kept As Written")->crc32 == "AAAAAAAA|crc");
}

TEST_CASE("merge: sorted by label ignoring case, stable") {
    RetroArchPlaylistEntries fresh;
    for (const char *label : {"beta", "Alpha", "gamma", "alpha"}) {
        RetroArchPlaylistEntry e;
        e.path = string("/r/x/") + label + ".nes";
        e.label = label;
        fresh.push_back(e);
    }
    RetroArchPlaylistEntries merged = RetroArchScanner::merge({}, scanned(fresh), "/r/x", "/r/x");
    CHECK(labelsOf(merged) == vector<string>{"Alpha", "alpha", "beta", "gamma"});
}

//*******************************
// scan
//*******************************
TEST_CASE("scan: a playlist per known folder, an unknown folder reported, nothing written for an empty one") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    t.addCore("fbneo_libretro", "Arcade (FinalBurn Neo)", "zip|7z", "FBNeo - Arcade Games");
    t.tmp.makeSubDir(string("roms/") + NES);
    t.tmp.makeSubDir("roms/FBNeo - Arcade Games");
    t.tmp.makeSubDir("roms/Sony - PlayStation"); // no core plays it
    t.tmp.makeSubDir("roms/AutoBleem");          // a reserved name, whatever is in it
    t.tmp.writeFile("roms/AutoBleem/x.nes", "rom");
    writeZip(t.tmp.at(string("roms/") + NES + "/Adventures of Lolo (USA).zip"),
             {{"Adventures of Lolo (USA).nes", "rom"}});
    t.tmp.writeFile(string("roms/") + NES + "/Battletoads (USA).nes", "rom");

    RecordingListener listener;
    RetroArchScanner scanner(&listener);
    RetroArchScanResult result = scanner.scan(t.options, RetroArchScanner::systemsFrom(t.cores()));

    CHECK(result.systemsScanned == 2);
    CHECK(result.gamesFound == 2);
    CHECK(result.playlistsWritten == vector<string>{string(NES) + ".lpl"});
    CHECK(result.unknownFolders == vector<string>{"AutoBleem", "Sony - PlayStation"});
    CHECK_FALSE(DirEntry::exists(t.playlist("FBNeo - Arcade Games"))); // empty folder, no playlist before
    CHECK_FALSE(DirEntry::exists(t.playlist("AutoBleem")));
    CHECK_FALSE(DirEntry::exists(t.playlist(NES) + ".tmp"));

    RetroArchPlaylistHeader header;
    RetroArchPlaylistEntries entries = t.loadPlaylist(NES, &header);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].label == "Adventures of Lolo (USA)");
    CHECK(entries[0].path ==
          t.tmp.at(string("roms/") + NES + "/Adventures of Lolo (USA).zip#Adventures of Lolo (USA).nes"));
    CHECK(entries[0].core_path == t.tmp.at("retroarch/cores/nestopia_libretro.so"));
    CHECK(entries[1].label == "Battletoads (USA)");
    REQUIRE(header.size() == 1);
    CHECK(header[0].first == "version");
    CHECK(header[0].second == "\"1.0\"");

    // one progress report per known folder
    REQUIRE(listener.stages.size() == 2);
    CHECK(listener.stages[0] == ScanStage::ScanningRoms);
    CHECK(listener.details == vector<string>{"FBNeo - Arcade Games", NES});
    CHECK(listener.dones == vector<int>{1, 2});
    CHECK(listener.totals == vector<int>{2, 2});
}

TEST_CASE("scan: a second run over the same tree writes nothing; a removed ROM rewrites the playlist") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    t.tmp.writeFile(string("roms/") + NES + "/A.nes", "rom");
    t.tmp.writeFile(string("roms/") + NES + "/B.nes", "rom");
    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());

    RetroArchScanner scanner;
    CHECK(scanner.scan(t.options, systems).playlistsWritten.size() == 1);
    CHECK(scanner.scan(t.options, systems).playlistsWritten.empty());

    DirEntry::removeFile(t.tmp.at(string("roms/") + NES + "/B.nes"));
    RetroArchScanResult result = scanner.scan(t.options, systems);
    CHECK(result.playlistsWritten.size() == 1);
    CHECK(result.gamesFound == 1);
    CHECK(labelsOf(t.loadPlaylist(NES)) == vector<string>{"A"});
}

TEST_CASE("scan: a playlist whose games all went away is removed, not left as an empty tab") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    t.addCore("snes9x_libretro", "Nintendo - SNES / SFC (Snes9x)", "sfc|smc", SNES);
    t.tmp.writeFile(string("roms/") + NES + "/A.nes", "rom");
    t.tmp.writeFile(string("roms/") + SNES + "/B.sfc", "rom");
    t.options.stateFile = t.tmp.at("roms.scanstate");
    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());
    RetroArchScanner scanner;
    REQUIRE(scanner.scan(t.options, systems).playlistsWritten.size() == 2);

    DirEntry::removeFile(t.tmp.at(string("roms/") + NES + "/A.nes"));
    RetroArchScanResult result = scanner.scan(t.options, systems);
    CHECK_FALSE(DirEntry::exists(t.playlist(NES)));
    CHECK(DirEntry::exists(t.playlist(SNES)));
    CHECK(result.playlistsWritten.empty());
    CHECK(result.gamesFound == 1);
    // settled: the next scan skips both folders and does not bring the file back
    RetroArchScanResult again = scanner.scan(t.options, systems);
    CHECK(again.systemsSkipped == 2);
    CHECK_FALSE(DirEntry::exists(t.playlist(NES)));
    // the games come back: the playlist is written again
    t.tmp.writeFile(string("roms/") + NES + "/A.nes", "rom");
    CHECK(scanner.scan(t.options, systems).playlistsWritten == vector<string>{string(NES) + ".lpl"});
    CHECK(labelsOf(t.loadPlaylist(NES)) == vector<string>{"A"});

    // an entry of the user's own (not under the folder) keeps the playlist alive
    DirEntry::removeFile(t.tmp.at(string("roms/") + NES + "/A.nes"));
    RetroArchPlaylistEntries entries = t.loadPlaylist(NES);
    RetroArchPlaylistEntry mine = entries[0];
    mine.path = "/elsewhere/Mine.nes";
    mine.label = "Mine";
    REQUIRE(RetroArchPlaylist::save(t.playlist(NES), RetroArchPlaylistEntries{mine}));
    scanner.scan(t.options, systems);
    CHECK(labelsOf(t.loadPlaylist(NES)) == vector<string>{"Mine"});
}

TEST_CASE("scan: an empty playlist already there is removed on the next real scan; an unreadable one stays") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    t.addCore("snes9x_libretro", "Nintendo - SNES / SFC (Snes9x)", "sfc|smc", SNES);
    t.tmp.makeSubDir(string("roms/") + NES);
    t.tmp.makeSubDir(string("roms/") + SNES);
    REQUIRE(RetroArchPlaylist::save(t.playlist(NES), RetroArchPlaylistEntries()));
    t.tmp.writeFile("retroarch/playlists/" + string(SNES) + ".lpl", "{ \"items\": [ truncated");
    t.tmp.writeFile("retroarch/playlists/Other - Unknown.lpl", "{}");
    t.tmp.writeFile("retroarch/playlists/Favorites.lpl", "{}");
    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());
    RetroArchScanner scanner;
    scanner.scan(t.options, systems);
    CHECK_FALSE(DirEntry::exists(t.playlist(NES)));
    CHECK(DirEntry::exists(t.playlist(SNES)));
    CHECK(DirEntry::exists(t.tmp.at("retroarch/playlists/Other - Unknown.lpl")));
    CHECK(DirEntry::exists(t.tmp.at("retroarch/playlists/Favorites.lpl")));
}

TEST_CASE("scan: a playlist RetroArch wrote keeps its header, its identified entries and the user's own") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    const string folder = string("roms/") + NES;
    t.tmp.makeSubDir(folder);
    writeZip(t.tmp.at(folder + "/lolo.zip"), {{"lolo.nes", "rom"}});
    t.tmp.writeFile(folder + "/Battletoads (USA).nes", "rom");
    t.tmp.writeFile("elsewhere.nes", "rom");

    // what RetroArch 1.22's own scanner leaves behind: a 1.5 header, the archive entry identified by CRC
    t.tmp.writeFile("retroarch/playlists/" + string(NES) + ".lpl",
                    "{\n"
                    "  \"version\": \"1.5\",\n"
                    "  \"default_core_path\": \"\",\n"
                    "  \"default_core_name\": \"\",\n"
                    "  \"label_display_mode\": 0,\n"
                    "  \"right_thumbnail_mode\": 0,\n"
                    "  \"left_thumbnail_mode\": 0,\n"
                    "  \"thumbnail_match_mode\": 0,\n"
                    "  \"sort_mode\": 0,\n"
                    "  \"scan_content_dir\": \"" +
                        jsonPath(t.tmp.at(folder)) +
                        "\",\n"
                        "  \"scan_file_exts\": \"\",\n"
                        "  \"scan_dat_file_path\": \"\",\n"
                        "  \"scan_search_recursively\": true,\n"
                        "  \"scan_search_archives\": false,\n"
                        "  \"scan_filter_dat_content\": false,\n"
                        "  \"scan_overwrite_playlist\": false,\n"
                        "  \"items\": [\n"
                        "    {\n"
                        "      \"path\": \"" +
                        jsonPath(t.tmp.at(folder)) +
                        "/lolo.zip#lolo.nes\",\n"
                        "      \"label\": \"Adventures of Lolo (USA)\",\n"
                        "      \"core_path\": \"DETECT\",\n"
                        "      \"core_name\": \"DETECT\",\n"
                        "      \"crc32\": \"79520FA1|crc\",\n"
                        "      \"db_name\": \"Nintendo - Nintendo Entertainment System.lpl\"\n"
                        "    },\n"
                        "    {\n"
                        "      \"path\": \"" +
                        jsonPath(t.tmp.at("elsewhere.nes")) +
                        "\",\n"
                        "      \"label\": \"Elsewhere\",\n"
                        "      \"core_path\": \"DETECT\",\n"
                        "      \"core_name\": \"DETECT\",\n"
                        "      \"crc32\": \"00000000|crc\",\n"
                        "      \"db_name\": \"Nintendo - Nintendo Entertainment System.lpl\"\n"
                        "    }\n"
                        "  ]\n"
                        "}\n");

    RetroArchScanner scanner;
    RetroArchScanResult result = scanner.scan(t.options, RetroArchScanner::systemsFrom(t.cores()));
    CHECK(result.playlistsWritten.size() == 1);
    CHECK(result.gamesFound == 2);

    RetroArchPlaylistHeader header;
    RetroArchPlaylistEntries entries = t.loadPlaylist(NES, &header);
    CHECK(labelsOf(entries) == vector<string>{"Adventures of Lolo (USA)", "Battletoads (USA)", "Elsewhere"});
    CHECK(findByLabel(entries, "Adventures of Lolo (USA)")->core_path == "DETECT"); // as RetroArch left it
    CHECK(findByLabel(entries, "Battletoads (USA)")->core_path == t.tmp.at("retroarch/cores/nestopia_libretro.so"));

    REQUIRE(header.size() == 15);
    CHECK(header[0].first == "version");
    CHECK(header[0].second == "\"1.5\"");
    CHECK(header[8].first == "scan_content_dir");
    CHECK(header[11].first == "scan_search_recursively");
    CHECK(header[11].second == "true");
    // and the file itself reads as RetroArch wrote it
    string text = t.readFile("retroarch/playlists/" + string(NES) + ".lpl");
    CHECK(text.find("\"version\": \"1.5\"") == text.find("\"version\""));
    CHECK(text.find("\"scan_search_recursively\": true") != string::npos);
}

TEST_CASE("scan: the playlists name the target root, not where the ROMs are on this machine") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    t.tmp.writeFile(string("roms/") + NES + "/A.nes", "rom");
    t.options.targetRomsDir = "/media/roms";
    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());

    RetroArchScanner scanner;
    CHECK(scanner.scan(t.options, systems).playlistsWritten.size() == 1);
    RetroArchPlaylistEntries entries = t.loadPlaylist(NES);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].path == string("/media/roms/") + NES + "/A.nes");

    // and a second run recognises its own entries through that root
    CHECK(scanner.scan(t.options, systems).playlistsWritten.empty());
    DirEntry::removeFile(t.tmp.at(string("roms/") + NES + "/A.nes"));
    CHECK(scanner.scan(t.options, systems).playlistsWritten.empty());
    CHECK_FALSE(DirEntry::exists(t.playlist(NES))); // nothing left in it: no empty tab, the file is removed
}

TEST_CASE("scan: without a roms dir or without systems nothing happens") {
    RomsTree t;
    RetroArchScanner scanner;
    DirEntry::rmDir(t.tmp.at("roms"));
    RetroArchScanResult none = scanner.scan(t.options, {nesSystem()});
    CHECK(none.systemsScanned == 0);

    t.tmp.makeSubDir(string("roms/") + NES);
    t.tmp.writeFile(string("roms/") + NES + "/A.nes", "rom");
    RetroArchScanResult noSystems = scanner.scan(t.options, {});
    CHECK(noSystems.systemsScanned == 0);
    CHECK(noSystems.unknownFolders == vector<string>{NES});
    CHECK_FALSE(DirEntry::exists(t.playlist(NES)));
}

//*******************************
// RetroArchPlaylist header round trip
//*******************************
TEST_CASE("RetroArchPlaylist: save() puts the loaded header back, version first; a six-line file has none") {
    TempDir tmp("lplheader");
    tmp.writeFile("a.lpl", "{\"sort_mode\": 2, \"version\": \"1.5\", \"items\": [], \"default_core_name\": \"x\"}");
    RetroArchPlaylistEntries entries;
    RetroArchPlaylistHeader header;
    REQUIRE(RetroArchPlaylist::load(tmp.at("a.lpl"), entries, &header));
    CHECK(entries.empty());
    REQUIRE(header.size() == 3);
    CHECK(header[0].first == "sort_mode");
    CHECK(header[0].second == "2");
    CHECK(header[2].first == "default_core_name");

    RetroArchPlaylistEntry e;
    e.path = "/r/x.nes";
    e.label = "x";
    REQUIRE(RetroArchPlaylist::save(tmp.at("b.lpl"), {e}, header));
    string text = tmp.readFile("b.lpl");
    CHECK(text.find("\"version\": \"1.5\"") < text.find("\"sort_mode\": 2"));
    CHECK(text.find("\"sort_mode\": 2") < text.find("\"items\""));

    RetroArchPlaylistHeader again;
    REQUIRE(RetroArchPlaylist::load(tmp.at("b.lpl"), entries, &again));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].core_path == ""); // written as given
    REQUIRE(again.size() == 3);
    CHECK(again[0].first == "version");

    tmp.writeFile("six.lpl", "/r/y.nes\ny\nDETECT\nDETECT\n00000000|crc\nx.lpl\n");
    REQUIRE(RetroArchPlaylist::load(tmp.at("six.lpl"), entries, &again));
    CHECK(entries.size() == 1);
    CHECK(again.empty());
}

//*******************************
// identification by database
//*******************************
namespace {

// crc32("rom") and crc32("other rom"), the bytes writeZip and the loose files below hold
const uint32_t CrcRom = 0x79520FA1u;
const uint32_t CrcOtherRom = 0x089A93F8u;

// the NES and arcade databases a test writes into <dir>: Lolo by CRC, Metal Slug by rom_name
string writeNesRdb(const TempDir &tmp, const string &dir) {
    test_support::Bytes records;
    test_support::appendRomRecord(records, "Adventures of Lolo (USA)", "Adventures of Lolo (USA).nes", CrcRom,
                                  "HAL Laboratory", 1989, 1);
    test_support::appendRomRecord(records, "Battletoads (USA)", "Battletoads (USA).nes", CrcOtherRom, "Tradewest", 1991,
                                  2);
    records.push_back(0xc0);
    tmp.makeSubDir(dir);
    return test_support::writeRdb(tmp, dir + "/" + NES + ".rdb", test_support::makeRdb(0, records));
}
string writeArcadeRdb(const TempDir &tmp, const string &dir) {
    test_support::Bytes records;
    test_support::appendRomRecord(records, "Metal Slug (NGM-2510)", "mslug.zip", 0x0AC09D00u, "Nazca", 1996, 2);
    records.push_back(0xc0);
    tmp.makeSubDir(dir);
    return test_support::writeRdb(tmp, dir + "/FBNeo - Arcade Games.rdb", test_support::makeRdb(0, records));
}

} // namespace

TEST_CASE("Crc32::ofFile streams a file, refuses one over the limit, spells a CRC as a playlist does") {
    TempDir tmp("crc");
    tmp.writeFile("a.bin", "rom");
    uint32_t crc = 1;
    CHECK(Crc32::ofFile(tmp.at("a.bin"), crc));
    CHECK(crc == CrcRom);
    CHECK(Crc32::ofFile(tmp.at("a.bin"), crc, 3));
    CHECK_FALSE(Crc32::ofFile(tmp.at("a.bin"), crc, 2));
    CHECK(crc == 0);
    CHECK_FALSE(Crc32::ofFile(tmp.at("missing.bin"), crc));
    CHECK(Crc32::ofBytes("other rom") == CrcOtherRom);
    CHECK(Crc32::playlistText(CrcRom) == "79520FA1|crc");
    CHECK(Crc32::playlistText(0) == "00000000|crc");
}

TEST_CASE("Crc32::fromPlaylistText reads back what playlistText wrote and refuses anything else") {
    uint32_t crc = 1;
    CHECK(Crc32::fromPlaylistText("089A93F8|crc", crc));
    CHECK(crc == CrcOtherRom);
    CHECK(Crc32::fromPlaylistText("79520fa1|crc", crc)); // lower case is fine
    CHECK(crc == CrcRom);
    crc = 1;
    CHECK_FALSE(Crc32::fromPlaylistText("", crc));
    CHECK_FALSE(Crc32::fromPlaylistText("00000000|crc", crc)); // "none", as a playlist spells it
    CHECK_FALSE(Crc32::fromPlaylistText("089A93F8", crc));
    CHECK_FALSE(Crc32::fromPlaylistText("089A93FX|crc", crc));
    CHECK(crc == 1);
}

TEST_CASE("identify: a zipped ROM by the archive's CRC, a loose one by hashing it, a miss keeps the file's name") {
    TempDir tmp("identify");
    tmp.makeSubDir("nes");
    writeZip(tmp.at("nes/lolo.zip"), {{"lolo.nes", "rom"}});     // the database knows this CRC
    tmp.writeFile("nes/toads.nes", "other rom");                 // and this one, hashed from the file
    tmp.writeFile("nes/Homebrew Thing (World).nes", "unknown!"); // not in the database
    ableem::RdbReader rdb;
    REQUIRE(rdb.open(writeNesRdb(tmp, "rdb")));

    ScannedRoms roms = RetroArchScanner::scanFolder(tmp.at("nes"), "/r/nes", nesSystem());
    REQUIRE(roms.size() == 3);
    CHECK(RetroArchScanner::identify(roms, rdb, 0) == 2);

    const ScannedRom &homebrew = roms[0];
    CHECK_FALSE(homebrew.identified);
    CHECK(homebrew.entry.label == "Homebrew Thing (World)");
    CHECK(homebrew.entry.crc32 == Crc32::playlistText(Crc32::ofBytes("unknown!"))); // hashed all the same

    const ScannedRom &lolo = roms[1];
    CHECK(lolo.identified);
    CHECK(lolo.entry.label == "Adventures of Lolo (USA)");
    CHECK(lolo.entry.path == "/r/nes/lolo.zip#lolo.nes");
    CHECK(lolo.entry.crc32 == "79520FA1|crc");

    const ScannedRom &toads = roms[2];
    CHECK(toads.identified);
    CHECK(toads.entry.label == "Battletoads (USA)");
    CHECK(toads.entry.crc32 == "089A93F8|crc");
}

TEST_CASE("identify: a loose file over the size limit is not hashed and keeps its name") {
    TempDir tmp("identify");
    tmp.makeSubDir("nes");
    tmp.writeFile("nes/toads.nes", "other rom"); // 9 bytes
    ableem::RdbReader rdb;
    REQUIRE(rdb.open(writeNesRdb(tmp, "rdb")));

    ScannedRoms roms = RetroArchScanner::scanFolder(tmp.at("nes"), "/r/nes", nesSystem());
    CHECK(RetroArchScanner::identify(roms, rdb, 8) == 0);
    CHECK(roms[0].entry.label == "toads");
    CHECK(roms[0].entry.crc32 == "00000000|crc");
}

TEST_CASE("identify: an arcade set by its archive's name, whatever its bytes") {
    TempDir tmp("identify");
    tmp.makeSubDir("arcade");
    tmp.writeFile("arcade/mslug.zip", "a repacked set, not the reference bytes");
    tmp.writeFile("arcade/neogeo.zip", "bios");
    ableem::RdbReader rdb;
    REQUIRE(rdb.open(writeArcadeRdb(tmp, "rdb")));

    ScannedRoms roms = RetroArchScanner::scanFolder(tmp.at("arcade"), "/r/arcade", arcadeSystem());
    REQUIRE(roms.size() == 2);
    CHECK(roms[0].wholeArchive);
    CHECK(RetroArchScanner::identify(roms, rdb, 0) == 1);
    CHECK(roms[0].entry.label == "Metal Slug (NGM-2510)");
    CHECK(roms[0].entry.crc32 == "00000000|crc"); // the archive is not hashed
    CHECK(roms[1].entry.label == "neogeo");
}

TEST_CASE("seedCrcsFromPlaylist: a loose ROM the playlist knows takes the entry's CRC; zips and sets do not") {
    TempDir tmp("seed");
    tmp.makeSubDir("nes");
    tmp.writeFile("nes/toads.nes", "other rom");
    tmp.writeFile("nes/new.nes", "brand new");
    writeZip(tmp.at("nes/lolo.zip"), {{"lolo.nes", "rom"}});
    ScannedRoms roms = RetroArchScanner::scanFolder(tmp.at("nes"), "/r/nes", nesSystem());
    REQUIRE(roms.size() == 3); // lolo.zip#lolo.nes, new, toads - sorted by path

    RetroArchPlaylistEntries existing;
    RetroArchPlaylistEntry toads;
    toads.path = "/r/nes/toads.nes";
    toads.label = "toads";
    toads.crc32 = "089A93F8|crc"; // what an earlier scan (or RetroArch) recorded
    existing.push_back(toads);
    RetroArchPlaylistEntry lolo;
    lolo.path = tmp.at("nes/lolo.zip") + "#lolo.nes"; // under the source folder, spelled this machine's way
    lolo.label = "lolo";
    lolo.crc32 = "DEADBEEF|crc";
    existing.push_back(lolo);
    RetroArchPlaylistEntry noCrc;
    noCrc.path = "/r/nes/new.nes";
    noCrc.label = "new";
    noCrc.crc32 = "00000000|crc"; // no CRC on record: nothing to take
    existing.push_back(noCrc);

    CHECK(RetroArchScanner::seedCrcsFromPlaylist(roms, existing, tmp.at("nes"), "/r/nes") == 1);
    CHECK(roms[0].crc == Crc32::ofBytes("rom")); // the archive's own CRC stands, not DEADBEEF
    CHECK(roms[1].crc == 0);                     // new.nes: no CRC to take, identify() will hash it
    CHECK(roms[2].crc == CrcOtherRom);
    CHECK(roms[2].entry.crc32 == "089A93F8|crc");
}

TEST_CASE("scan: a loose ROM with a CRC in the playlist is not hashed again - the entry's CRC is what the database "
          "is asked about") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    writeNesRdb(t.tmp, "retroarch/database/rdb");
    t.options.rdbDir = t.tmp.at("retroarch/database/rdb");
    const string nes = string("roms/") + NES;
    t.tmp.writeFile(nes + "/game.nes", "rom"); // its real CRC is Lolo's
    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());

    // a playlist that says the file is Battletoads' CRC, unidentified - as RetroArch would have written
    // for a file it hashed and did not know at the time
    RetroArchPlaylistEntries entries;
    RetroArchPlaylistEntry e;
    e.path = t.tmp.at(nes + "/game.nes");
    e.label = "game";
    e.core_path = "DETECT";
    e.core_name = "DETECT";
    e.crc32 = "089A93F8|crc";
    e.db_name = string(NES) + ".lpl";
    entries.push_back(e);
    REQUIRE(RetroArchPlaylist::save(t.playlist(NES), entries));

    RetroArchScanner scanner;
    RetroArchScanResult result = scanner.scan(t.options, systems);
    CHECK(result.gamesIdentified == 1);
    // named by the playlist's CRC, not by hashing the bytes (which would have said Lolo)
    CHECK(labelsOf(t.loadPlaylist(NES)) == vector<string>{"Battletoads (USA)"});
}

TEST_CASE("scan: with a state file a folder nothing changed in is skipped; a ROM, playlist or database change "
          "brings it back") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    t.addCore("snes9x_libretro", "Nintendo - SNES / SFC (Snes9x)", "sfc|smc", SNES);
    t.tmp.writeFile(string("roms/") + NES + "/A.nes", "rom");
    t.tmp.writeFile(string("roms/") + SNES + "/B.sfc", "rom");
    t.options.stateFile = t.tmp.at("roms.scanstate");
    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());
    RetroArchScanner scanner;

    RetroArchScanResult first = scanner.scan(t.options, systems);
    CHECK(first.systemsScanned == 2);
    CHECK(first.systemsSkipped == 0);
    CHECK(first.gamesFound == 2);
    CHECK(DirEntry::exists(t.options.stateFile));

    RetroArchScanResult second = scanner.scan(t.options, systems);
    CHECK(second.systemsScanned == 2);
    CHECK(second.systemsSkipped == 2); // nothing changed anywhere
    CHECK(second.gamesFound == 2);     // the counts still come, from the playlists
    CHECK(second.games.size() == 2);
    CHECK(second.playlistsWritten.empty());

    // a ROM added to one folder: that folder is scanned, the other still skipped
    t.tmp.writeFile(string("roms/") + NES + "/C.nes", "rom2");
    RetroArchScanResult third = scanner.scan(t.options, systems);
    CHECK(third.systemsSkipped == 1);
    CHECK(third.playlistsWritten == vector<string>{string(NES) + ".lpl"});
    CHECK(labelsOf(t.loadPlaylist(NES)) == vector<string>{"A", "C"});
    CHECK(scanner.scan(t.options, systems).systemsSkipped == 2); // and settles again

    // the playlist changed under us (RetroArch, or a hand edit): its folder is looked at again
    RetroArchPlaylistEntries entries = t.loadPlaylist(NES);
    entries[0].label = "A (renamed by hand)";
    entries[0].label += " and longer";
    REQUIRE(RetroArchPlaylist::save(t.playlist(NES), entries));
    RetroArchScanResult fourth = scanner.scan(t.options, systems);
    CHECK(fourth.systemsSkipped == 1);
    CHECK(fourth.playlistsWritten.empty()); // an existing entry is kept as it is - nothing to rewrite
    CHECK(scanner.scan(t.options, systems).systemsSkipped == 2);

    // a database arriving for the system: looked at again, and the games named
    writeNesRdb(t.tmp, "retroarch/database/rdb");
    t.options.rdbDir = t.tmp.at("retroarch/database/rdb");
    RetroArchScanResult fifth = scanner.scan(t.options, systems);
    CHECK(fifth.systemsSkipped == 1);  // the SNES folder has no database, its digest is what it was
    CHECK(fifth.gamesIdentified == 1); // A.nes ("rom") is Lolo
    CHECK(scanner.scan(t.options, systems).systemsSkipped == 2);

    // without a state file every folder is scanned every time
    t.options.stateFile = "";
    CHECK(scanner.scan(t.options, systems).systemsSkipped == 0);
}

TEST_CASE("merge: a database name replaces an existing unidentified label for the same ROM, and only that") {
    TempDir tmp("merge");
    tmp.makeSubDir("roms/nes");
    writeZip(tmp.at("roms/nes/pack.zip"), {{"a.nes", "rom"}, {"b.nes", "other rom"}});
    tmp.writeFile("roms/nes/Kept.nes", "rom");
    const string source = tmp.at("roms/nes");
    const string target = "/media/roms/nes";

    auto entry = [](const string &path, const string &label, const string &crc = "00000000|crc") {
        RetroArchPlaylistEntry e;
        e.path = path;
        e.label = label;
        e.core_path = "DETECT";
        e.core_name = "DETECT";
        e.crc32 = crc;
        e.db_name = "Nintendo - Nintendo Entertainment System.lpl";
        return e;
    };
    RetroArchPlaylistEntries existing = {
        entry(target + "/pack.zip#a.nes", "a"),                          // an earlier file-name scan
        entry(target + "/pack.zip#b.nes", "Battletoads (USA)", "X|crc"), // RetroArch's own, already named
        entry(target + "/Kept.nes", "Kept"),                             // ours: nothing to say about it
    };
    ScannedRoms fresh = scanned(
        {
            entry(target + "/pack.zip#a.nes", "Adventures of Lolo (USA)", "79520FA1|crc"),
            entry(target + "/pack.zip#b.nes", "Battletoads (USA)", "089A93F8|crc"),
        },
        true);
    fresh.push_back(scanned({entry(target + "/Kept.nes", "Kept")})[0]);

    RetroArchPlaylistEntries merged = RetroArchScanner::merge(existing, fresh, source, target);
    CHECK(labelsOf(merged) == vector<string>{"Adventures of Lolo (USA)", "Battletoads (USA)", "Kept"});
    CHECK(merged[0].crc32 == "79520FA1|crc"); // the replacement is ours, whole
    CHECK(merged[1].crc32 == "X|crc");        // the same name: RetroArch's entry stays as it was
    CHECK(merged[1].core_path == "DETECT");
}

TEST_CASE("scan: with the databases in place the playlists carry their names; a system without one keeps the "
          "file names, and a name an earlier scan wrote is corrected") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    t.addCore("snes9x_libretro", "Nintendo - SNES / SFC (Snes9x)", "sfc|smc",
              "Nintendo - Super Nintendo Entertainment System");
    t.addCore("fbneo_libretro", "Arcade (FinalBurn Neo)", "zip|7z", "FBNeo - Arcade Games");
    writeNesRdb(t.tmp, "retroarch/database/rdb");
    writeArcadeRdb(t.tmp, "retroarch/database/rdb"); // no SNES database
    t.options.rdbDir = t.tmp.at("retroarch/database/rdb");
    t.options.folderAliases = {{"Arcade", "FBNeo - Arcade Games"}};
    const string nes = string("roms/") + NES;
    t.tmp.makeSubDir(nes);
    writeZip(t.tmp.at(nes + "/lolo.zip"), {{"lolo.nes", "rom"}});
    t.tmp.writeFile("roms/Nintendo - Super Nintendo Entertainment System/Chrono Trigger (USA).sfc", "rom");
    t.tmp.writeFile("roms/Arcade/mslug.zip", "set");
    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());

    // a first scan without databases, as a stick before its rdb pack would have it
    RetroArchScanner::Options blind = t.options;
    blind.rdbDir = "";
    RetroArchScanner scanner;
    RetroArchScanResult first = scanner.scan(blind, systems);
    CHECK(first.gamesFound == 3);
    CHECK(first.gamesIdentified == 0);
    CHECK(labelsOf(t.loadPlaylist(NES)) == vector<string>{"lolo"});

    RetroArchScanResult second = scanner.scan(t.options, systems);
    CHECK(second.gamesFound == 3);
    CHECK(second.gamesIdentified == 2);
    CHECK(second.playlistsWritten == vector<string>{"FBNeo - Arcade Games.lpl", string(NES) + ".lpl"});
    CHECK(labelsOf(t.loadPlaylist(NES)) == vector<string>{"Adventures of Lolo (USA)"});
    CHECK(labelsOf(t.loadPlaylist("FBNeo - Arcade Games")) == vector<string>{"Metal Slug (NGM-2510)"});
    // the SNES folder comes after the NES one: its games must not be looked up in the NES database
    CHECK(labelsOf(t.loadPlaylist("Nintendo - Super Nintendo Entertainment System")) ==
          vector<string>{"Chrono Trigger (USA)"});

    CHECK(scanner.scan(t.options, systems).playlistsWritten.empty());
}

//*******************************
// exact core names, the order's tie-break, ROM folders at scan time, a kept core pick
//*******************************
TEST_CASE("CoreInfoTable: a cores.cfg value is a file stem first, then a whole display name, then a part of one") {
    RomsTree t;
    // "Snes9x" is a part of both display names and the stem of neither; "snes9x" the stem of the first only
    t.addCore("snes9x_libretro", "Nintendo - SNES (Snes9x)", "sfc|smc", SNES);
    t.addCore("snes9x2010_libretro", "Nintendo - SNES (Snes9x 2010)", "sfc|smc|fig|swc", SNES);

    auto picked = [&](const string &value) {
        t.tmp.writeFile("cores.cfg", string(SNES) + " = " + value + "\n");
        return t.cores(t.tmp.at("cores.cfg")).coreForDatabase(SNES)->name;
    };
    CHECK(picked("snes9x") == "Nintendo - SNES (Snes9x)"); // the stem, though the other has more extensions
    CHECK(picked("snes9x2010") == "Nintendo - SNES (Snes9x 2010)");
    CHECK(picked("Nintendo - SNES (Snes9x)") == "Nintendo - SNES (Snes9x)"); // a whole name, though a part of both
    CHECK(picked("(Snes9x)") == "Nintendo - SNES (Snes9x)");                 // a part: today's behaviour
    CHECK(picked("Snes9x") == "Nintendo - SNES (Snes9x 2010)");       // two parts: the first in the table's order
    CHECK(picked("no such core") == "Nintendo - SNES (Snes9x 2010)"); // nothing matches: the .info mapping stays
}

TEST_CASE("CoreInfoTable: cores that tie on extensions are ordered by file stem, whatever the stick lists first") {
    RomsTree t;
    t.addCore("zeta_libretro", "Z (Tie Zeta)", "sfc|smc", SNES);
    t.addCore("alpha_libretro", "A (Tie Alpha)", "sfc|smc", SNES);
    t.addCore("mid_libretro", "M (Tie Mid)", "sfc|smc", SNES);
    t.addCore("big_libretro", "B (Big)", "sfc|smc|fig", SNES);

    CoreInfoTable cores = t.cores();
    CHECK(cores.coreForDatabase(SNES)->stem == "big");
    CoreInfos all = cores.coresForDatabase(SNES);
    REQUIRE(all.size() == 4);
    CHECK(all[0]->stem == "big");
    CHECK(all[1]->stem == "alpha"); // after the default: by stem
    CHECK(all[2]->stem == "mid");
    CHECK(all[3]->stem == "zeta");

    // a fragment that two cores match gives the first in that order
    t.tmp.writeFile("cores.cfg", string(SNES) + " = Tie\n");
    CHECK(t.cores(t.tmp.at("cores.cfg")).coreForDatabase(SNES)->stem == "alpha");
    CHECK(cores.cores()[1]->stem == "alpha");
}

TEST_CASE("CoreInfoTable::coresForDatabase: the cfg's core first even when its .info does not list the database") {
    RomsTree t;
    t.addCore("fbneo_libretro", "Arcade (FinalBurn Neo)", "zip", "FBNeo - Arcade Games");
    t.addCore("fbalpha_libretro", "Arcade (FB Alpha)", "zip|7z", "FB Alpha - Arcade Games");
    t.tmp.writeFile("cores.cfg", "FB Alpha - Arcade Games = fbneo\n");

    CoreInfoTable cores = t.cores(t.tmp.at("cores.cfg"));
    CoreInfos all = cores.coresForDatabase("FB Alpha - Arcade Games.lpl");
    REQUIRE(all.size() == 2);
    CHECK(all[0]->stem == "fbneo");
    CHECK(all[1]->stem == "fbalpha");
    CHECK(cores.coresForDatabase("Atari - 2600").empty());
}

TEST_CASE("createMissingFolders: a folder for every database an installed core plays, bar the skipped ones") {
    RomsTree t;
    t.addCore("puae_libretro", "Commodore - Amiga (P-UAE)", "adf|zip", "Commodore - Amiga");
    t.addCore("hatari_libretro", "Atari - ST (Hatari)", "st|zip", "Atari - ST");
    t.addCore("nestopia_libretro", "Nintendo - NES (Nestopia)", "nes", NES);
    t.addCore("fbneo_libretro", "Arcade (FinalBurn Neo)", "zip", "FBNeo - Arcade Games");
    t.addCore("ffmpeg_libretro", "FFmpeg", "mp4|mkv", "FFmpeg");
    t.addCore("mame2010_libretro", "ARC (MAME 2010)", "zip", "MAME 2010");
    t.addCore("bk_libretro", "Elektronika BK", "bin", "BK-0010/BK-0011");
    t.addCore("notinstalled_libretro", "Sega - Not Installed", "bin", "Sega - Not Installed", false, false);
    t.tmp.makeSubDir("roms/nintendo - nintendo entertainment system"); // a folder there, in another case
    t.tmp.makeSubDir("roms/Arcade");
    t.tmp.writeFile("roms/Arcade/placeholder.txt", "x");

    const auto aliases = std::map<string, string>{{"Arcade", "FBNeo - Arcade Games"}};
    const auto skip = RetroArchScanner::loadSkipList("");
    CHECK(skip.empty()); // no file, no skips
    t.tmp.writeFile("skip.cfg", "# not games\nFFmpeg\n\n  mame 2010  \n");
    const auto skipped = RetroArchScanner::loadSkipList(t.tmp.at("skip.cfg"));
    CHECK(skipped == std::set<string>{"ffmpeg", "mame 2010"});

    CoreInfoTable cores = t.cores();
    vector<string> created = RetroArchScanner::createMissingFolders(t.options.romsDir, cores, aliases, skipped);
    CHECK(created == vector<string>{"Atari - ST", "Commodore - Amiga"});
    CHECK(DirEntry::isDirectory(t.tmp.at("roms/Commodore - Amiga")));
    CHECK(DirEntry::isDirectory(t.tmp.at("roms/Atari - ST")));
    CHECK_FALSE(DirEntry::isDirectory(t.tmp.at("roms/FFmpeg")));
    CHECK_FALSE(DirEntry::isDirectory(t.tmp.at("roms/MAME 2010")));
    CHECK_FALSE(DirEntry::isDirectory(t.tmp.at("roms/FBNeo - Arcade Games"))); // the "Arcade" alias is its home
    CHECK_FALSE(DirEntry::isDirectory(t.tmp.at("roms/Sega - Not Installed")));
    CHECK_FALSE(DirEntry::isDirectory(t.tmp.at("roms/BK-0010"))); // not a usable folder name
    // the folder is there already (in another case): no second one is made. Counted by name, because on a
    // case-insensitive file system (Windows, macOS) "roms/<NES>" IS the lower-case folder and isDirectory() says yes
    int nesFolders = 0;
    for (const DirEntry &e : DirEntry::diru(t.tmp.at("roms"))) {
        string name = e.name, wanted = NES;
        for (char &c : name)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        for (char &c : wanted)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (name == wanted)
            nesFolders++;
    }
    CHECK(nesFolders == 1);

    // nothing left to make: a second pass does nothing, and what is in the folders stays
    CHECK(RetroArchScanner::createMissingFolders(t.options.romsDir, cores, aliases, skipped).empty());
    CHECK(t.readFile("roms/Arcade/placeholder.txt") == "x");
    // no ROM folder tree at all: nothing is created
    CHECK(RetroArchScanner::createMissingFolders(t.tmp.at("nowhere"), cores, aliases, skipped).empty());
}

TEST_CASE("merge: a core picked by hand stays when the database's name replaces the label") {
    TempDir tmp("mergecore");
    tmp.makeSubDir("roms/nes");
    tmp.writeFile("roms/nes/Lolo.nes", "rom");
    tmp.writeFile("roms/nes/Plain.nes", "rom");
    const string source = tmp.at("roms/nes");
    const string target = "/media/roms/nes";

    auto entry = [&](const string &file, const string &label, const string &corePath, const string &coreName) {
        RetroArchPlaylistEntry e;
        e.path = target + "/" + file;
        e.label = label;
        e.core_path = corePath;
        e.core_name = coreName;
        e.crc32 = "00000000|crc";
        e.db_name = "Nintendo - Nintendo Entertainment System.lpl";
        return e;
    };
    RetroArchPlaylistEntries existing = {
        entry("Lolo.nes", "Lolo", "/media/cores/nestopia_libretro.so", "Nestopia"), // picked by hand
        entry("Plain.nes", "Plain", "DETECT", "DETECT"),                            // no core of its own yet
    };
    ScannedRoms fresh = scanned(
        {
            entry("Lolo.nes", "Adventures of Lolo (USA)", "/media/cores/km_fceumm_libretro.so", "FCEUmm"),
            entry("Plain.nes", "Plain (USA)", "/media/cores/km_fceumm_libretro.so", "FCEUmm"),
        },
        true);

    RetroArchPlaylistEntries merged = RetroArchScanner::merge(existing, fresh, source, target);
    REQUIRE(merged.size() == 2);
    CHECK(merged[0].label == "Adventures of Lolo (USA)");              // the database's name
    CHECK(merged[0].core_path == "/media/cores/nestopia_libretro.so"); // the pick, not the system's default
    CHECK(merged[0].core_name == "Nestopia");
    CHECK(merged[1].label == "Plain (USA)");
    CHECK(merged[1].core_name == "FCEUmm"); // no pick: the fresh entry's core
}

TEST_CASE("scan: a core picked for a game survives the rescan that names the game from the database") {
    RomsTree t;
    t.addCore("nestopia_libretro", "Nintendo - NES / Famicom (Nestopia UE)", "nes|fds", NES);
    t.addCore("fceumm_libretro", "Nintendo - NES / Famicom (FCEUmm)", "nes|fds|unf", NES);
    t.options.stateFile = t.tmp.at("scanstate");
    writeNesRdb(t.tmp, "retroarch/database/rdb");
    const string nes = string("roms/") + NES;
    t.tmp.makeSubDir(nes);
    writeZip(t.tmp.at(nes + "/lolo.zip"), {{"lolo.nes", "rom"}});
    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores());
    RetroArchScanner scanner;

    RetroArchScanner::Options blind = t.options; // a first scan before the database pack
    blind.rdbDir = "";
    scanner.scan(blind, systems);
    RetroArchPlaylistEntries entries = t.loadPlaylist(NES);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].core_name == "Nintendo - NES / Famicom (FCEUmm)"); // the system's default
    entries[0].core_path = t.tmp.at("retroarch/cores/nestopia_libretro.so");
    entries[0].core_name = "Nintendo - NES / Famicom (Nestopia UE)";
    REQUIRE(RetroArchPlaylist::save(t.playlist(NES), entries));

    t.options.rdbDir = t.tmp.at("retroarch/database/rdb");
    scanner.scan(t.options, systems); // the database names the game
    entries = t.loadPlaylist(NES);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].label == "Adventures of Lolo (USA)");
    CHECK(entries[0].core_name == "Nintendo - NES / Famicom (Nestopia UE)");
    CHECK(entries[0].core_path == t.tmp.at("retroarch/cores/nestopia_libretro.so"));
}

TEST_CASE("scan: once per stick every entry of ours moves to the current core, then a hand pick stays") {
    RomsTree t;
    t.addCore("km_fceumm_legacy_libretro", "Nintendo - NES (km_FCEUmm Legacy)", "nes|fds", NES);
    t.addCore("km_fceumm_libretro", "Nintendo - NES (km_FCEUmm)", "nes|fds", NES);
    t.tmp.writeFile("cores.cfg", string(NES) + " = km_fceumm\n");
    t.options.stateFile = t.tmp.at("scanstate");
    t.options.coreMigrationMarker = t.tmp.at("picks.done");
    const string nes = string("roms/") + NES;
    t.tmp.makeSubDir(nes);
    t.tmp.writeFile(nes + "/A.nes", "rom");
    t.tmp.writeFile(nes + "/B.nes", "rom2");
    t.tmp.writeFile("elsewhere/Mine.nes", "rom3");
    const string legacy = t.tmp.at("retroarch/cores/km_fceumm_legacy_libretro.so");
    const string current = t.tmp.at("retroarch/cores/km_fceumm_libretro.so");
    const string target = t.tmp.at(nes);

    // an old playlist: the scanner's entries on the legacy core, and the user's own addition elsewhere
    RetroArchPlaylistEntries old;
    for (const char *file : {"A.nes", "B.nes", "../../elsewhere/Mine.nes"}) {
        RetroArchPlaylistEntry e;
        e.path = target + "/" + file;
        e.label = file;
        e.core_path = legacy;
        e.core_name = "Nintendo - NES (km_FCEUmm Legacy)";
        e.crc32 = "00000000|crc";
        e.db_name = string(NES) + ".lpl";
        old.push_back(e);
    }
    old[2].path = t.tmp.at("elsewhere/Mine.nes");
    old[2].label = "Mine";
    old[0].label = "A";
    old[1].label = "B";
    REQUIRE(RetroArchPlaylist::save(t.playlist(NES), old));

    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores(t.tmp.at("cores.cfg")));
    RetroArchScanner scanner;
    scanner.scan(t.options, systems);
    RetroArchPlaylistEntries now = t.loadPlaylist(NES);
    REQUIRE(now.size() == 3);
    for (const auto &e : now) {
        if (e.label == "Mine")
            CHECK(e.core_path == legacy); // not ours: untouched
        else
            CHECK(e.core_path == current);
    }
    CHECK(DirEntry::exists(t.options.coreMigrationMarker));

    // a pick made after that survives the next scan, even one that looks at the folder again
    for (auto &e : now) {
        if (e.label == "A")
            e.core_path = legacy;
    }
    REQUIRE(RetroArchPlaylist::save(t.playlist(NES), now));
    t.tmp.writeFile(nes + "/C.nes", "rom4");
    scanner.scan(t.options, systems);
    now = t.loadPlaylist(NES);
    REQUIRE(now.size() == 4);
    for (const auto &e : now) {
        if (e.label == "A" || e.label == "Mine")
            CHECK(e.core_path == legacy);
        else
            CHECK(e.core_path == current);
    }
}

TEST_CASE("CoreInfoTable: the user's file is read after the platform's and wins; the platform's pick stays known") {
    RomsTree t;
    t.addCore("snes9x_libretro", "Nintendo - SNES (Snes9x)", "sfc|smc", SNES);
    t.addCore("bsnes_libretro", "Nintendo - SNES (bsnes)", "sfc", SNES);
    t.addCore("mesen_libretro", "Nintendo - SNES (Mesen)", "sfc|smc|fig", SNES);
    t.tmp.writeFile("platform.cfg", string(SNES) + " = bsnes\n");
    const string user = t.tmp.at("user.cfg");

    auto load = [&]() {
        CoreInfoTable table;
        table.load(t.tmp.at("retroarch"), t.tmp.at("platform.cfg"), user);
        return table;
    };
    CHECK(load().coreForDatabase(SNES)->stem == "bsnes"); // no user file: the platform's
    REQUIRE(CoreInfoTable::saveUserPicks(user, {{SNES, "snes9x"}}));
    CoreInfoTable table = load();
    CHECK(table.coreForDatabase(SNES)->stem == "snes9x");
    CHECK(table.platformCoreFor(SNES)->stem == "bsnes");
    CHECK(table.defaultCoreFor(SNES)->stem == "mesen"); // the .info mapping is a third thing
    CoreInfos order = table.platformOrder(SNES);
    REQUIRE(order.size() == 3);
    CHECK(order[0]->stem == "bsnes"); // the platform's pick first, the rest by stem - whatever the user chose
    CHECK(order[1]->stem == "mesen");
    CHECK(order[2]->stem == "snes9x");
    CHECK(CoreInfoTable::loadUserPicks(user) == std::map<string, string>{{SNES, "snes9x"}});

    // a line naming a core that is not there is ignored
    t.tmp.writeFile("user.cfg", string(SNES) + " = nothere\n");
    CHECK(load().coreForDatabase(SNES)->stem == "bsnes");

    // an empty set of picks removes the file
    REQUIRE(CoreInfoTable::saveUserPicks(user, {}));
    CHECK_FALSE(DirEntry::exists(user));
}

TEST_CASE("seedCrcsFromPlaylist: an entry's path with redundant parts is still the same file") {
    TempDir tmp("seedodd");
    tmp.makeSubDir("nes");
    tmp.writeFile("nes/toads.nes", "other rom");
    ScannedRoms roms = RetroArchScanner::scanFolder(tmp.at("nes"), "/r/nes", nesSystem());
    REQUIRE(roms.size() == 1);

    RetroArchPlaylistEntry toads;
    toads.path = "/r//nes/./toads.nes";
    toads.label = "toads";
    toads.crc32 = "089A93F8|crc";
    CHECK(RetroArchScanner::seedCrcsFromPlaylist(roms, {toads}, tmp.at("nes"), "/r/nes") == 1);
    CHECK(roms[0].entry.crc32 == "089A93F8|crc");
}

TEST_CASE("merge: an existing entry whose path has redundant parts is ours, not a second game") {
    TempDir tmp("mergeodd");
    tmp.makeSubDir("roms/nes");
    tmp.writeFile("roms/nes/Kept.nes", "rom");
    RetroArchPlaylistEntry old;
    old.path = "/media/roms//nes/./Kept.nes";
    old.label = "Kept As Written";
    old.core_path = "DETECT";
    old.core_name = "DETECT";
    old.crc32 = "AAAAAAAA|crc";
    RetroArchPlaylistEntry fresh = old;
    fresh.path = "/media/roms/nes/Kept.nes";
    fresh.label = "Kept";
    fresh.crc32 = "00000000|crc";

    RetroArchPlaylistEntries merged =
        RetroArchScanner::merge({old}, scanned({fresh}), tmp.at("roms/nes"), "/media/roms/nes");
    CHECK(labelsOf(merged) == vector<string>{"Kept As Written"});
}

TEST_CASE("scan: the core migration reaches entries whose ROM or core path is written another way") {
    RomsTree t;
    t.addCore("km_fceumm_legacy_libretro", "Nintendo - NES (km_FCEUmm Legacy)", "nes|fds", NES);
    t.addCore("km_fceumm_libretro", "Nintendo - NES (km_FCEUmm)", "nes|fds", NES);
    t.tmp.writeFile("cores.cfg", string(NES) + " = km_fceumm\n");
    t.options.stateFile = t.tmp.at("scanstate");
    t.options.coreMigrationMarker = t.tmp.at("picks.done");
    const string nes = string("roms/") + NES;
    t.tmp.makeSubDir(nes);
    t.tmp.writeFile(nes + "/A.nes", "rom");
    t.tmp.writeFile(nes + "/B.nes", "rom2");
    const string legacy = t.tmp.at("retroarch/cores/km_fceumm_legacy_libretro.so");
    const string current = t.tmp.at("retroarch/cores/km_fceumm_libretro.so");
    const string currentOdd = t.tmp.at("retroarch/cores/./km_fceumm_libretro.so"); // the current core, spelled oddly
    const string target = t.tmp.at(nes);

    RetroArchPlaylistEntries old;
    for (const char *file : {"A", "B"}) {
        RetroArchPlaylistEntry e;
        e.label = file;
        e.path = target + "/" + file + ".nes";
        e.core_path = legacy;
        e.core_name = "Nintendo - NES (km_FCEUmm Legacy)";
        e.crc32 = "00000000|crc";
        e.db_name = string(NES) + ".lpl";
        old.push_back(e);
    }
    old[0].path = target + "//./A.nes"; // ours, written with redundant parts
    old[1].core_path = currentOdd;      // already on the current core: nothing to move
    REQUIRE(RetroArchPlaylist::save(t.playlist(NES), old));

    RetroArchSystems systems = RetroArchScanner::systemsFrom(t.cores(t.tmp.at("cores.cfg")));
    RetroArchScanner scanner;
    scanner.scan(t.options, systems);
    RetroArchPlaylistEntries now = t.loadPlaylist(NES);
    REQUIRE(now.size() == 2); // the odd A is the same game as the scanned A
    for (const auto &e : now) {
        if (e.label == "A")
            CHECK(e.core_path == current);
        else
            CHECK(e.core_path == currentOdd); // the same core: left as it was written
    }
}
