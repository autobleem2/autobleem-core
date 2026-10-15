// WindowsInstallJob: the Windows product's data tree from a fake download repository, fresh and as an update
#include <doctest/doctest.h>

#include "installer/windows_install_job.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/sha256.h>
#include <ableem/engine/zip_writer.h>

#include "support/tar_builder.h"
#include "support/temp_dir.h"

#include <map>
#include <string>
#include <vector>

using namespace std;
using ableem::DirEntry;
using ableem::Sha256;
using test_support::TarBuilder;

namespace {

const char *const Site = "http://site";
const char *const Buildbot = "http://buildbot";
const string dataDir = AB_TEST_DATA_DIR;

//******************
// FakeSite
//******************
class FakeSite : public Downloader {
public:
    map<string, string> files;
    vector<string> fetched;
    bool fetch(const string &url, const string &destFile, const Progress &progress, string &error) override {
        fetched.push_back(url);
        auto it = files.find(url);
        if (it == files.end()) {
            error = url + ": HTTP 404";
            return false;
        }
        if (!DirEntry::copyFile(it->second, destFile)) {
            error = "cannot copy " + it->second;
            return false;
        }
        if (progress)
            progress(1, 1);
        return true;
    }
    int count(const string &urlPart) const {
        int n = 0;
        for (const string &u : fetched)
            if (u.find(urlPart) != string::npos)
                n++;
        return n;
    }
};

//******************
// Recorder
//******************
class Recorder : public InstallListener {
public:
    vector<string> phases, lines;
    void onPhase(int, int, const string &title) override { phases.push_back(title); }
    void onProgress(uint64_t, uint64_t) override {}
    void onLine(const string &line) override { lines.push_back(line); }
    bool said(const string &part) const {
        for (const string &l : lines)
            if (l.find(part) != string::npos)
                return true;
        return false;
    }
};

string json(const string &name, const string &path, const string &extra = "") {
    return "{\"name\": \"" + name + "\", \"size\": " + to_string(DirEntry::fileSize(path)) + ", \"sha256\": \"" +
           Sha256::ofFile(path) + "\", \"url\": \"" + Site + "/" + name + "\"" + extra + "}";
}

//******************
// Fixture
//******************
// a program folder with shipped themes, an empty data root, and the site's files for every option
struct Fixture {
    TempDir tmp{"win_install"};
    FakeSite site;
    Recorder out;
    WindowsInstallOptions options;
    string root;

    Fixture() {
        root = tmp.at("Documents/AutoBleem");
        options.dataRoot = root;
        options.programDir = tmp.makeSubDir("Program Files/AutoBleem");
        options.repoUrl = Site;
        options.buildbotUrl = Buildbot;
        options.scratchDir = tmp.makeSubDir("scratch");
        tmp.writeFile("Program Files/AutoBleem/Themes/ab2.0.0/theme.json", "{ab2.0.0}");
        tmp.writeFile("Program Files/AutoBleem/Themes/ab2/theme.json", "{ab2}");
        tmp.writeFile("Program Files/AutoBleem/Themes/ab2/images/bg.png", "png");
        tmp.writeFile("Program Files/AutoBleem/Themes/default/theme.json", "{default}");
        // the shipped scanner processor (proc_unzip)
        tmp.writeFile("Program Files/AutoBleem/Processors/unzip/processor.ini", "[Processor]\nVersion=1.0.1\n");
        tmp.writeFile("Program Files/AutoBleem/Processors/unzip/bin/windows-x86_64/unzip.exe", "exe 1.0.1");
        // the shipped extension (the Store)
        tmp.writeFile("Program Files/AutoBleem/Extensions/store/extension.ini", "[extension]\nVersion=1.0.1\n");
        tmp.writeFile("Program Files/AutoBleem/Extensions/store/bin/win/store.dll", "dll 1.0.1");
        // the cover databases
        for (const char *r : {"J", "U", "P"}) {
            string name = string("covers") + r + ".db";
            tmp.writeFile("site/" + name, "sqlite " + name);
            site.files[string(Site) + "/db/" + name] = tmp.at("site/" + name);
            tmp.writeFile("site/" + name + ".sha256", Sha256::ofFile(tmp.at("site/" + name)) + "  " + name + "\n");
            site.files[string(Site) + "/db/" + name + ".sha256"] = tmp.at("site/" + name + ".sha256");
        }
        // the shipped OpenBIOS
        tmp.writeFile("Program Files/AutoBleem/Bios/openbios.bin", "openbios");
        // the BIOS list: the two PlayStation files and one of a core's
        sitePack({"scph5501.bin", "scph5500.bin", "disksys.rom"});
        // the samples
        {
            TarBuilder b;
            b.file("Games/Tetrade/Tetrade.cue", "cue").file("SAMPLES.md", "# samples\n");
            b.file("RetroArch/roms/Nintendo - Nintendo Entertainment System/Nova.nes", "nes");
            b.file("RetroArch/thumbnails/Nintendo - Nintendo Entertainment System/Named_Boxarts/Nova.png", "png");
            REQUIRE(b.writeTarGz(tmp.at("site/samples-20260920.tar.gz")));
            tmp.writeFile("site/samples.json", json("samples-20260920.tar.gz", tmp.at("site/samples-20260920.tar.gz"),
                                                    ", \"date\": \"20260920\""));
            site.files[string(Site) + "/samples/latest.json"] = tmp.at("site/samples.json");
            site.files[string(Site) + "/samples-20260920.tar.gz"] = tmp.at("site/samples-20260920.tar.gz");
        }
    }

    // the BIOS pack's list and catalog for these names (disksys.rom lives in a subfolder)
    void sitePack(const vector<string> &names) {
        string list = "# the list\n";
        for (const string &name : names) {
            tmp.writeFile("site/" + name, "bios " + name);
            site.files[string(Site) + "/bios/" + name] = tmp.at("site/" + name);
            list += Sha256::ofFile(tmp.at("site/" + name)) + " " + to_string(DirEntry::fileSize(tmp.at("site/" + name))) +
                    " " + Site + "/bios/" + name + " " + (name == "disksys.rom" ? "Nintendo - Famicom Disk System/" : "") +
                    name + "\n";
        }
        tmp.writeFile("site/biospack.txt", list);
        site.files[string(Site) + "/win/bios/biospack.txt"] = tmp.at("site/biospack.txt");
        string j = json("biospack.txt", tmp.at("site/biospack.txt"), ", \"count\": 3, \"total_bytes\": 48");
        size_t at = j.find("http://site/biospack.txt");
        j.replace(at, strlen("http://site/biospack.txt"), string(Site) + "/win/bios/biospack.txt");
        tmp.writeFile("site/bios.json", j);
        site.files[string(Site) + "/win/bios/latest.json"] = tmp.at("site/bios.json");
    }

    // the BIOS pack's list with these exact manifest paths (a path as the manifest writes it, even a hostile one);
    // each is served at Site/bios/<index> with the content "pack <path>"
    void sitePackPaths(const vector<string> &paths) {
        string list = "# the list\n";
        for (size_t i = 0; i < paths.size(); i++) {
            const string file = "site/packed" + to_string(i);
            tmp.writeFile(file, "pack " + paths[i]);
            const string url = string(Site) + "/bios/" + to_string(i);
            site.files[url] = tmp.at(file);
            list += Sha256::ofFile(tmp.at(file)) + " " + to_string(DirEntry::fileSize(tmp.at(file))) + " " + url + " " +
                    paths[i] + "\n";
        }
        tmp.writeFile("site/biospack.txt", list);
        site.files[string(Site) + "/win/bios/biospack.txt"] = tmp.at("site/biospack.txt");
        string j = json("biospack.txt", tmp.at("site/biospack.txt"), ", \"count\": 3, \"total_bytes\": 48");
        size_t at = j.find("http://site/biospack.txt");
        j.replace(at, strlen("http://site/biospack.txt"), string(Site) + "/win/bios/biospack.txt");
        tmp.writeFile("site/bios.json", j);
        site.files[string(Site) + "/win/bios/latest.json"] = tmp.at("site/bios.json");
    }

    // the site's own RetroArch build and cores pack
    void siteRetroArch() {
        REQUIRE(TarBuilder()
                    .file("retroarch.exe", "MZ retroarch")
                    .file("assets/xmb/x.png", "png")
                    .file("info/snes9x_libretro.info", "info")
                    .writeTarGz(tmp.at("site/retroarch-win64-1.22.2.tar.gz")));
        tmp.writeFile("site/retroarch.json",
                      json("retroarch-win64-1.22.2.tar.gz", tmp.at("site/retroarch-win64-1.22.2.tar.gz"),
                           ", \"version\": \"1.22.2\""));
        site.files[string(Site) + "/win/retroarch/latest.json"] = tmp.at("site/retroarch.json");
        site.files[string(Site) + "/retroarch-win64-1.22.2.tar.gz"] = tmp.at("site/retroarch-win64-1.22.2.tar.gz");
        REQUIRE(TarBuilder()
                    .file("cores/snes9x_libretro.dll", "core")
                    .file("info/snes9x_libretro.info", "info")
                    .writeTarGz(tmp.at("site/cores-win64-20260920.tar.gz")));
        tmp.writeFile("site/cores.json", json("cores-win64-20260920.tar.gz", tmp.at("site/cores-win64-20260920.tar.gz"),
                                              ", \"date\": \"20260920\", \"count\": 1"));
        site.files[string(Site) + "/win/cores/latest.json"] = tmp.at("site/cores.json");
        site.files[string(Site) + "/cores-win64-20260920.tar.gz"] = tmp.at("site/cores-win64-20260920.tar.gz");
    }

    // libretro's own: the official archive (tests/data/test_bcj2.7z stands in - a real 7z, but with none of
    // RetroArch-Win64/retroarch.exe in it, which the job must notice) and the per-core index
    void buildbotRetroArch() {
        site.files[string(Buildbot) + "/stable/1.22.2/windows/x86_64/RetroArch.7z"] = dataDir + "/test_bcj2.7z";
        tmp.writeFile("site/index",
                      "2026-09-20 abcdef01 snes9x_libretro.dll.zip\n2026-09-20 abcdef02 gone_libretro.dll.zip\n");
        site.files[string(Buildbot) + "/nightly/windows/x86_64/latest/.index-extended"] = tmp.at("site/index");
        ableem::ZipWriter zip;
        REQUIRE(zip.open(tmp.at("site/snes9x_libretro.dll.zip")));
        zip.addBytes("snes9x_libretro.dll", "core");
        REQUIRE(zip.close());
        site.files[string(Buildbot) + "/nightly/windows/x86_64/latest/snes9x_libretro.dll.zip"] =
            tmp.at("site/snes9x_libretro.dll.zip");
    }

    bool run(string &error) {
        return WindowsInstallJob::run(options, site, out, []() { return false; }, error);
    }
    bool has(const string &rel) const { return DirEntry::exists(root + "/" + rel); }
};

} // namespace

TEST_CASE("a fresh install: the data tree, the shipped themes and the three cover databases") {
    Fixture fx;
    WindowsInstallInfo before = WindowsInstallJob::inspect(fx.options);
    CHECK(before.exists);
    CHECK_FALSE(before.installed);
    CHECK(WindowsInstallJob::phasesFor(fx.options, before) ==
          vector<string>{"Data folder", "Cover databases", "Finishing"});

    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.out.phases == vector<string>{"Data folder", "Cover databases", "Finishing"});
    for (const char *d :
         {"Games", "System/Databases", "System/Logs", "System/Bios", "Themes", "RetroArch/roms", "Apps"})
        CHECK(DirEntry::isDirectory(fx.root + "/" + d));
    CHECK(fx.tmp.readFile("Documents/AutoBleem/Themes/ab2/theme.json") == "{ab2}");
    CHECK(fx.has("Themes/ab2/images/bg.png"));
    CHECK(fx.has("Themes/default/theme.json"));
    CHECK(fx.out.said("3 themes copied in"));
    for (const char *r : {"J", "U", "P"})
        CHECK(fx.tmp.readFile(string("Documents/AutoBleem/System/Databases/covers") + r + ".db") ==
              string("sqlite covers") + r + ".db");
    CHECK_FALSE(fx.has("System/Install"));
    CHECK(fx.out.said("Installed."));
    // the scanner processors' folder, with its README
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Processors/README.txt").find("scanner processors") !=
          string::npos);
    CHECK(fx.tmp.readFile("Documents/AutoBleem/Extensions/README.txt").find("AutoBleem extensions") != string::npos);
    // the shipped processor is in the data tree
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Processors/unzip/bin/windows-x86_64/unzip.exe") == "exe 1.0.1");
    CHECK(fx.out.said("1 scanner processors"));
    // a fresh install decides nothing about on/off: no sequence.ini, so every processor starts off (SDK-11)
    CHECK_FALSE(fx.has("System/Processors/sequence.ini"));
    // and the shipped extension in the data tree's Extensions/
    CHECK(fx.tmp.readFile("Documents/AutoBleem/Extensions/store/bin/win/store.dll") == "dll 1.0.1");
    CHECK(fx.out.said("1 extensions"));
}

TEST_CASE("an update keeps the user's settings and themes, removes the scan fingerprints") {
    Fixture fx;
    fx.tmp.writeFile("Documents/AutoBleem/System/config.ini", "theme=aergb\n");
    fx.tmp.writeFile("Documents/AutoBleem/System/games.fingerprint", "old");
    fx.tmp.writeFile("Documents/AutoBleem/System/roms.fingerprint", "old");
    fx.tmp.writeFile("Documents/AutoBleem/Themes/ab2/theme.json", "{edited}");
    // the default theme was there already: the user's choice stays
    fx.tmp.writeFile("Documents/AutoBleem/Themes/ab2.0.0/theme.json", "{mine}");
    fx.tmp.writeFile("Documents/AutoBleem/Games/Tekken 3/Tekken 3.cue", "cue");
    fx.tmp.writeFile("Documents/AutoBleem/System/Databases/coversU.db", "sqlite coversU.db");
    fx.tmp.writeFile("Documents/AutoBleem/System/Processors/README.txt", "my notes");
    // an older unzip, switched off by the user
    fx.tmp.writeFile("Documents/AutoBleem/System/Processors/unzip/bin/windows-x86_64/unzip.exe", "exe 1.0.0");
    fx.tmp.writeFile("Documents/AutoBleem/System/Processors/sequence.ini", "[ps1]\n-unzip\nmine\n");
    // a processor the user installed, switched on, with the data folder it keeps for itself (SDK-11)
    fx.tmp.writeFile("Documents/AutoBleem/System/Processors/mine/processor.ini", "[Processor]\nVersion=1.0\n");
    fx.tmp.writeFile("Documents/AutoBleem/System/Processors/mine/bin/windows-x86_64/mine.exe", "exe mine");
    fx.tmp.writeFile("Documents/AutoBleem/Home/processors/mine/cache.dat", "data");
    // an older Store with a file the new one has not, the Store's own state, one of the user's own extensions
    fx.tmp.writeFile("Documents/AutoBleem/Extensions/store/bin/win/store.dll", "dll 1.0.0");
    fx.tmp.writeFile("Documents/AutoBleem/Extensions/store/stale.txt", "old");
    fx.tmp.writeFile("Documents/AutoBleem/System/Extensions/store/sources.txt", "https://x/list.tsv\n");
    fx.tmp.writeFile("Documents/AutoBleem/System/Extensions/disabled.txt", "hello\n");
    fx.tmp.writeFile("Documents/AutoBleem/Extensions/mine/extension.ini", "[extension]");
    fx.options.update = true;
    fx.options.coversJapan = fx.options.coversPal = false;

    WindowsInstallInfo before = WindowsInstallJob::inspect(fx.options);
    CHECK(before.installed);
    CHECK(before.hasCovers[1]);
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Processors/README.txt") == "my notes"); // never rewritten
    // the update brings the new program; the user's order and on/off stay
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Processors/unzip/bin/windows-x86_64/unzip.exe") == "exe 1.0.1");
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Processors/sequence.ini") == "[ps1]\n-unzip\nmine\n");
    // a processor the user installed stays, with its program and its data folder
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Processors/mine/bin/windows-x86_64/mine.exe") == "exe mine");
    CHECK(fx.has("System/Processors/mine/processor.ini"));
    CHECK(fx.tmp.readFile("Documents/AutoBleem/Home/processors/mine/cache.dat") == "data");
    // the Store replaced whole; its state, the crash guard's list and the user's extension kept
    CHECK(fx.tmp.readFile("Documents/AutoBleem/Extensions/store/bin/win/store.dll") == "dll 1.0.1");
    CHECK_FALSE(fx.has("Extensions/store/stale.txt"));
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Extensions/store/sources.txt") == "https://x/list.tsv\n");
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Extensions/disabled.txt") == "hello\n");
    CHECK(fx.has("Extensions/mine/extension.ini"));
    {
        // the settings kept, the PS1 emulator every install lands on set (capitalised: the launcher's writer)
        const string cfg = fx.tmp.readFile("Documents/AutoBleem/System/config.ini");
        CHECK(cfg.find("Theme=aergb") != string::npos);
        CHECK(cfg.find("Emulator=pcsx-abnxt") != string::npos);
    }
    CHECK(fx.tmp.readFile("Documents/AutoBleem/Themes/ab2/theme.json") == "{edited}");
    CHECK(fx.has("Themes/default/theme.json")); // a shipped theme not there yet still comes in
    CHECK(fx.has("Games/Tekken 3/Tekken 3.cue"));
    CHECK_FALSE(fx.has("System/games.fingerprint"));
    CHECK_FALSE(fx.has("System/roms.fingerprint"));
    CHECK(fx.out.said("coversU.db is already there"));
    CHECK(fx.site.count("coversJ.db") == 0);
    CHECK(fx.out.said("Updated."));
}

TEST_CASE("an update that brings the default theme to a data tree without it switches the theme once") {
    Fixture fx;
    fx.tmp.writeFile("Documents/AutoBleem/System/config.ini", "theme=aergb\nlanguage=Polish\n");
    fx.tmp.writeFile("Documents/AutoBleem/Themes/ab2/theme.json", "{ab2}");
    fx.options.update = true;
    fx.options.coversJapan = fx.options.coversUsa = fx.options.coversPal = false;
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    {
        const string cfg = fx.tmp.readFile("Documents/AutoBleem/System/config.ini");
        CHECK(cfg.find("Theme=ab2.0.0") != string::npos);
        CHECK(cfg.find("Language=Polish") != string::npos);
    }
    CHECK(fx.has("Themes/ab2.0.0/theme.json"));
    // the next update: the folder is there, the user's choice stays
    fx.tmp.writeFile("Documents/AutoBleem/System/config.ini", "theme=aergb\n");
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/config.ini").find("Theme=aergb") != string::npos);
}

TEST_CASE("RetroArch and its cores from the download repository, then the BIOS files and the samples") {
    Fixture fx;
    fx.siteRetroArch();
    fx.options.retroarch = true;
    fx.options.bios = true;
    fx.options.samples = true;
    WindowsInstallInfo before = WindowsInstallJob::inspect(fx.options);
    CHECK(WindowsInstallJob::phasesFor(fx.options, before) ==
          vector<string>{"Data folder", "Cover databases", "RetroArch", "RetroArch cores", "BIOS files", "Sample games",
                         "Finishing"});

    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("Documents/AutoBleem/RetroArch/bin/retroarch.exe") == "MZ retroarch");
    CHECK(fx.tmp.readFile("Documents/AutoBleem/RetroArch/bin/VERSION") == "1.22.2\n");
    CHECK(fx.has("RetroArch/bin/assets/xmb/x.png"));
    CHECK(fx.has("RetroArch/bin/cores/snes9x_libretro.dll"));
    CHECK(fx.has("RetroArch/bin/info/snes9x_libretro.info"));
    for (const char *d : {"playlists", "saves", "states", "system", "thumbnails"})
        CHECK(DirEntry::isDirectory(fx.root + "/RetroArch/bin/" + d));
    string cfg = fx.tmp.readFile("Documents/AutoBleem/RetroArch/bin/retroarch.cfg");
    CHECK(cfg.find("video_fullscreen = \"true\"") != string::npos);
    CHECK(cfg.find("quit_on_close_content = \"2\"") != string::npos);
    CHECK(cfg.find("rgui_browser_directory = \"") != string::npos);
    CHECK(cfg.find("RetroArch\\roms\"") != string::npos);
    // the whole pack into RetroArch's own system dir, and the PlayStation pair under the emulator's names
    CHECK(fx.tmp.readFile("Documents/AutoBleem/RetroArch/bin/system/scph5501.bin") == "bios scph5501.bin");
    CHECK(fx.has("RetroArch/bin/system/Nintendo - Famicom Disk System/disksys.rom"));
    CHECK(fx.out.said("3 fetched, 0 already there, 0 failed"));
    // the pack never reaches System/Bios: the shipped OpenBIOS is there, not the Sony files
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romw.bin") == "openbios");
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romJP.bin") == "openbios");
    // the samples, RetroArch's part laid out for the Windows tree
    CHECK(fx.has("Games/Tetrade/Tetrade.cue"));
    CHECK(fx.has("SAMPLES.md"));
    CHECK(fx.has("RetroArch/roms/Nintendo - Nintendo Entertainment System/Nova.nes"));
    CHECK(fx.has("RetroArch/bin/thumbnails/Nintendo - Nintendo Entertainment System/Named_Boxarts/Nova.png"));
    CHECK_FALSE(fx.has("RetroArch/thumbnails"));
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/samples.txt") == "samples-20260920.tar.gz\n");
    CHECK(fx.site.count("buildbot") == 0);

    // again: RetroArch is there at that version, the cfg is kept, the samples are not put back
    Fixture again;
    again.siteRetroArch();
    again.options = fx.options;
    fx.tmp.writeFile("Documents/AutoBleem/RetroArch/bin/retroarch.cfg", cfg + "video_smooth = \"true\"\n");
    DirEntry::removeDirAndContents(fx.root + "/Games/Tetrade");
    REQUIRE_MESSAGE(WindowsInstallJob::run(again.options, again.site, again.out, []() { return false; }, error), error);
    CHECK(again.out.said("RetroArch 1.22.2 is installed already"));
    CHECK(again.out.said("keeping the existing retroarch.cfg"));
    CHECK(again.out.said("not again"));
    CHECK_FALSE(fx.has("Games/Tetrade"));
    CHECK(again.site.count("retroarch-win64") == 0);
    CHECK(fx.tmp.readFile("Documents/AutoBleem/RetroArch/bin/retroarch.cfg").find("video_smooth") != string::npos);
}

TEST_CASE("without a build on the site, libretro's own archive and one zip per core") {
    Fixture fx;
    fx.buildbotRetroArch();
    fx.options.retroarch = true;
    string error;
    // the stand-in archive has no RetroArch-Win64/retroarch.exe: the job says so instead of going on
    CHECK_FALSE(fx.run(error));
    CHECK(error.find("no retroarch.exe") != string::npos);
    CHECK(fx.out.said("taking libretro's RetroArch.7z"));
    CHECK(fx.site.count("stable/1.22.2/windows/x86_64/RetroArch.7z") == 1);

    // the cores' fallback on its own, with RetroArch "installed" by hand
    Fixture cores;
    cores.buildbotRetroArch();
    cores.tmp.writeFile("Documents/AutoBleem/RetroArch/bin/retroarch.exe", "MZ");
    cores.tmp.writeFile("Documents/AutoBleem/RetroArch/bin/VERSION", "1.22.2\n");
    cores.options.retroarch = true;
    REQUIRE_MESSAGE(cores.run(error), error);
    CHECK(cores.out.said("RetroArch 1.22.2 is installed already"));
    CHECK(cores.out.said("taking them one by one from libretro"));
    CHECK(cores.tmp.readFile("Documents/AutoBleem/RetroArch/bin/cores/snes9x_libretro.dll") == "core");
    CHECK(cores.out.said("could not fetch gone_libretro.dll.zip"));
    CHECK(cores.out.said("1 fetched, 1 failed"));
}

TEST_CASE("an installed OpenBIOS or a user's Sony BIOS is never replaced, whatever the pack holds") {
    Fixture fx;
    fx.siteRetroArch();
    fx.sitePack({"scph5501.bin", "scph5500.bin", "romw.bin", "openbios.bin"});
    fx.options.retroarch = true;
    fx.options.bios = true;
    fx.tmp.writeFile("Documents/AutoBleem/System/Bios/romw.bin", "my sony");
    fx.tmp.writeFile("Documents/AutoBleem/System/Bios/romJP.bin", "older openbios");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romw.bin") == "my sony");
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romJP.bin") == "older openbios");
    // the pack's Sony files went to RetroArch, the manifest's romw.bin / openbios.bin were not fetched
    CHECK(fx.has("RetroArch/bin/system/scph5501.bin"));
    CHECK_FALSE(fx.has("RetroArch/bin/system/romw.bin"));
    CHECK_FALSE(fx.has("RetroArch/bin/system/openbios.bin"));
    CHECK(fx.site.count("/bios/romw.bin") == 0);
    CHECK(fx.site.count("/bios/openbios.bin") == 0);
    CHECK(fx.out.said("2 fetched, 0 already there, 0 failed"));

    // a fresh System/Bios, a pack with scph5501: the OpenBIOS, not the pack's file
    Fixture fresh;
    fresh.siteRetroArch();
    fresh.options.retroarch = true;
    fresh.options.bios = true;
    REQUIRE_MESSAGE(fresh.run(error), error);
    CHECK(fresh.tmp.readFile("Documents/AutoBleem/System/Bios/romw.bin") == "openbios");
}

TEST_CASE("without RetroArch the BIOS step downloads nothing; the samples skip the other systems") {
    Fixture fx;
    fx.options.bios = true;
    fx.options.samples = true;
    fx.options.coversJapan = fx.options.coversUsa = fx.options.coversPal = false;
    // the user's own NTSC-U BIOS is there already: kept, the Japanese one filled in
    fx.tmp.writeFile("Documents/AutoBleem/System/Bios/romw.bin", "mine");
    WindowsInstallInfo before = WindowsInstallJob::inspect(fx.options);
    CHECK(WindowsInstallJob::phasesFor(fx.options, before) ==
          vector<string>{"Data folder", "BIOS files", "Sample games", "Finishing"});
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.out.said("PlayStation only"));
    CHECK(fx.site.count("/bios/") == 0);
    CHECK(fx.site.count("win/bios") == 0);
    CHECK_FALSE(fx.has("RetroArch/bin/system/scph5501.bin"));
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romw.bin") == "mine");
    CHECK(fx.out.said("keeping the existing romw.bin"));
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romJP.bin") == "openbios");
    CHECK(fx.has("Games/Tetrade/Tetrade.cue"));
    CHECK_FALSE(fx.has("RetroArch/roms/Nintendo - Nintendo Entertainment System/Nova.nes"));
    CHECK(fx.out.said("need RetroArch"));
}

TEST_CASE("the pack's romJP.bin is skipped too: its URL is never requested") {
    Fixture fx;
    fx.siteRetroArch();
    fx.sitePackPaths({"scph5501.bin", "romJP.bin"});
    fx.options.retroarch = true;
    fx.options.bios = true;
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.site.count("/bios/0") == 1);
    CHECK(fx.site.count("/bios/1") == 0);
    CHECK_FALSE(fx.has("RetroArch/bin/system/romJP.bin"));
    CHECK(fx.out.said("1 files"));
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romJP.bin") == "openbios");
}

TEST_CASE("scph5501 reaches RetroArch while System/Bios stays untouched in the same run") {
    Fixture fx;
    fx.siteRetroArch();
    fx.options.retroarch = true;
    fx.options.bios = true;
    fx.tmp.writeFile("Documents/AutoBleem/System/Bios/romw.bin", "my sony");
    fx.tmp.writeFile("Documents/AutoBleem/System/Bios/romJP.bin", "my sony jp");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("Documents/AutoBleem/RetroArch/bin/system/scph5501.bin") == "bios scph5501.bin");
    CHECK_FALSE(fx.has("System/Bios/scph5501.bin"));
    CHECK_FALSE(fx.has("System/Bios/scph5500.bin"));
    CHECK_FALSE(fx.has("System/Bios/disksys.rom"));
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romw.bin") == "my sony");
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romJP.bin") == "my sony jp");
    // and System/Bios holds exactly the two names
    int n = 0;
    for (const DirEntry &e : DirEntry::diru(fx.root + "/System/Bios"))
        n += e.isDir ? 0 : 1;
    CHECK(n == 2);
}

TEST_CASE("a user's own Sony romJP.bin is kept, with the log saying so") {
    Fixture fx;
    fx.options.bios = true;
    fx.tmp.writeFile("Documents/AutoBleem/System/Bios/romJP.bin", "my sony jp");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romJP.bin") == "my sony jp");
    CHECK(fx.out.said("keeping the existing romJP.bin"));
    CHECK_FALSE(fx.out.said("keeping the existing romw.bin"));
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romw.bin") == "openbios");
    CHECK(fx.out.said("OpenBIOS -> System/Bios/romw.bin"));
    CHECK_FALSE(fx.out.said("OpenBIOS -> System/Bios/romJP.bin"));

    // both there: both kept, nothing copied
    Fixture both;
    both.options.bios = true;
    both.tmp.writeFile("Documents/AutoBleem/System/Bios/romw.bin", "a");
    both.tmp.writeFile("Documents/AutoBleem/System/Bios/romJP.bin", "b");
    REQUIRE_MESSAGE(both.run(error), error);
    CHECK(both.out.said("keeping the existing romw.bin"));
    CHECK(both.out.said("keeping the existing romJP.bin"));
    CHECK_FALSE(both.out.said("OpenBIOS ->"));
}

TEST_CASE("without RetroArch there is no network call at all, update or not, bios or not") {
    // bios on, an install and an update
    for (bool update : {false, true}) {
        Fixture fx;
        fx.options.bios = true;
        fx.options.update = update;
        fx.options.coversJapan = fx.options.coversUsa = fx.options.coversPal = false;
        string error;
        REQUIRE_MESSAGE(fx.run(error), error);
        CHECK(fx.site.fetched.empty());
        CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romw.bin") == "openbios");
    }
    // bios off as well: the OpenBIOS still lands, and still nothing is asked for
    for (bool update : {false, true}) {
        Fixture fx;
        fx.options.retroarch = false;
        fx.options.bios = false;
        fx.options.update = update;
        fx.options.coversJapan = fx.options.coversUsa = fx.options.coversPal = false;
        string error;
        REQUIRE_MESSAGE(fx.run(error), error);
        CHECK(fx.site.fetched.empty());
        CHECK(fx.site.count("latest.json") == 0);
        CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romw.bin") == "openbios");
        CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romJP.bin") == "openbios");
    }
}

TEST_CASE("case, subfolder and escaping variants of the skip names never reach System/Bios") {
    Fixture fx;
    fx.siteRetroArch();
    // 0 ROMW.BIN, 1 sub/romw.bin, 2 and 3 climb out of the pack's folder, 4 absolute, 5 backslash, 6 the real one
    fx.sitePackPaths({"ROMW.BIN", "sub/romw.bin", "../System/Bios/romw.bin", "../../../System/Bios/romJP.bin",
                      "/System/Bios/romw.bin", "..\\..\\System\\Bios\\romw.bin", "romw.bin"});
    fx.options.retroarch = true;
    fx.options.bios = true;
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    // the intended rule: the skip is for the exact names; a variant is an ordinary pack file in RetroArch's
    // own system dir (and the Windows file system there is a different folder from System/Bios), while a path that
    // leaves that dir is dropped unread
    CHECK(fx.site.count("/bios/0") == 1);
    CHECK(fx.site.count("/bios/1") == 1);
    for (const char *dropped : {"/bios/2", "/bios/3", "/bios/4", "/bios/5", "/bios/6"})
        CHECK_MESSAGE(fx.site.count(dropped) == 0, dropped);
    CHECK(fx.tmp.readFile("Documents/AutoBleem/RetroArch/bin/system/sub/romw.bin") == "pack sub/romw.bin");
    // System/Bios: the OpenBIOS twice, nothing else
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romw.bin") == "openbios");
    CHECK(fx.tmp.readFile("Documents/AutoBleem/System/Bios/romJP.bin") == "openbios");
    int n = 0;
    for (const DirEntry &e : DirEntry::diru(fx.root + "/System/Bios"))
        n += e.isDir ? 0 : 1;
    CHECK(n == 2);
    CHECK_FALSE(fx.has("System/Bios/ROMW.BIN"));
    CHECK_FALSE(fx.has("System/Bios/sub"));
    CHECK_FALSE(fx.has("RetroArch/bin/System"));
    CHECK_FALSE(fx.has("RetroArch/System"));
}

TEST_CASE("no openbios.bin shipped: the emulator's built-in BIOS, nothing written; a failed copy is said") {
    Fixture fx;
    fx.options.bios = true;
    DirEntry::removeFile(fx.options.programDir + "/Bios/openbios.bin");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK_FALSE(fx.has("System/Bios/romw.bin"));
    CHECK_FALSE(fx.has("System/Bios/romJP.bin"));
    CHECK(fx.out.said("no romw.bin - the PlayStation emulator will use its built-in BIOS"));
    CHECK(fx.out.said("no romJP.bin - the PlayStation emulator will use its built-in BIOS"));

    // no program folder at all: the same
    Fixture none;
    none.options.bios = true;
    none.options.programDir.clear();
    REQUIRE_MESSAGE(none.run(error), error);
    CHECK(none.out.said("no romw.bin - the PlayStation emulator will use its built-in BIOS"));

    // a copy that cannot be made: a directory stands where romw.bin would go? no - exists() would see it. A
    // directory as the OpenBIOS path exists() but cannot be read as a file
    Fixture bad;
    bad.options.bios = true;
    DirEntry::removeFile(bad.options.programDir + "/Bios/openbios.bin");
    bad.tmp.makeSubDir("Program Files/AutoBleem/Bios/openbios.bin");
    REQUIRE_MESSAGE(bad.run(error), error);
    CHECK(bad.out.said("could not copy OpenBIOS to"));
    CHECK_FALSE(bad.out.said("OpenBIOS -> System/Bios"));
}

TEST_CASE("a stop request and a missing site are reported") {
    Fixture fx;
    string error;
    bool stopNow = true;
    CHECK_FALSE(WindowsInstallJob::run(fx.options, fx.site, fx.out, [&]() { return stopNow; }, error));
    CHECK(error == "Stopped");

    Fixture down;
    down.site.files.clear();
    CHECK_FALSE(down.run(error));
    CHECK(error.find("404") != string::npos);
}

TEST_CASE("retroArchConfigText names the roms folder the Windows way") {
    string cfg = WindowsInstallJob::retroArchConfigText("C:/Users/me/Documents/AutoBleem");
    CHECK(cfg.find("rgui_browser_directory = \"C:\\Users\\me\\Documents\\AutoBleem\\RetroArch\\roms\"") !=
          string::npos);
}
