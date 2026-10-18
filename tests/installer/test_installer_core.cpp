// InstallerJob: a stick from a package and a fake download repository, fresh and as an update
#include <doctest/doctest.h>

#include "installer/command_downloader.h"
#include "installer/installer_job.h"
#include "installer/progress_file.h"
#include "installer/retroarch_runner.h"
#include "installer/legacy_layout.h"
#include "core/services/retroarch_version.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/sha256.h>
#include <ableem/engine/zip_archive.h>
#include <ableem/engine/zip_writer.h>

#include "support/tar_builder.h"
#include "support/temp_dir.h"
#include "support/zip_builder.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace std;
using ableem::DirEntry;
using ableem::Sha256;
using test_support::TarBuilder;
using test_support::ZipBuilder;

namespace {

const char *const Site = "http://site";

//******************
// FakeSite
//******************
// a Downloader over files in a temp tree: url -> path; a url not there is a 404
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
    // HEAD: the size of the file behind the url; `noHead` = a server that does not say
    bool noHead = false;
    bool contentLength(const string &url, const string &, uint64_t &size) override {
        auto it = files.find(url);
        if (noHead || it == files.end())
            return false;
        size = static_cast<uint64_t>(DirEntry::fileSize(it->second));
        return size > 0;
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

// the package: what autobleem-psc-<v>.tar.gz carries, in miniature
string makePackage(TempDir &tmp, const string &version) {
    TarBuilder b;
    b.dir("./Autobleem").dir("./Autobleem/bin").dir("./Autobleem/bin/autobleem");
    b.file("./Autobleem/bin/autobleem/autobleem-gui", "ELF " + version, 0755);
    b.file("./Autobleem/bin/autobleem/config.ini", "theme=ab2.0.0\nlanguage=English\n");
    b.file("./Autobleem/bin/autobleem/platform/roms_systems.cfg",
           "# the systems\nNintendo - Nintendo Entertainment System\n  \nSega - Mega Drive - Genesis\r\n");
    b.file("./Autobleem/bin/emu/pcsx-ab", "ELF pcsx", 0755);
    b.file("./Autobleem/rc/launch.sh", "#!/bin/sh\n", 0755);
    b.file("./Autobleem/lib/libs.tar.gz", "gz");
    b.file("./Autobleem/start.sh", "#!/bin/sh\n", 0755);
    b.dir("./Apps").file("./Apps/abflashkit/abflashkit", "ELF", 0755).file("./Apps/abflashkit/app.ini", "Title=Kit\n");
    b.dir("./Extensions").file("./Extensions/pscbios/bin/psc/pscbios.so", "ELF", 0755);
    b.file("./Extensions/pscbios/extension.ini", "[extension]\nName=PSC-Bios\n");
    // the Store, which every package ships since 2026-09-25
    b.file("./Extensions/store/extension.ini", "[extension]\nName=AutoBleem Store\nVersion=" + version + "\n");
    b.file("./Extensions/store/bin/psc/store.so", "ELF store", 0755);
    b.dir("./Themes")
        .dir("./Themes/ab2.0.0")
        .file("./Themes/ab2.0.0/theme.json", "{}")
        .dir("./Themes/ab2")
        .file("./Themes/ab2/theme.json", "{}")
        .dir("./Themes/default")
        .file("./Themes/default/theme.json", "{}");
    b.dir("./Games").dir("./System").dir("./Docs").file("./Docs/readme.txt", "hi");
    b.file("./VERSION", version + "\n");
    string path = tmp.at("pkg/autobleem-psc-" + version + ".tar.gz");
    DirEntry::createDirs(tmp.at("pkg"));
    REQUIRE(b.writeTarGz(path));
    return path;
}

string json(const string &name, const string &path, const string &extra = "") {
    return "{\"name\": \"" + name + "\", \"size\": " + to_string(DirEntry::fileSize(path)) + ", \"sha256\": \"" +
           Sha256::ofFile(path) + "\", \"url\": \"" + Site + "/" + name + "\"" + extra + "}";
}

//******************
// Fixture
//******************
// a stick, a package, and the site's files for every option
struct Fixture {
    TempDir tmp{"installer"};
    FakeSite site;
    Recorder out;
    InstallOptions options;
    string root;

    Fixture() {
        root = tmp.makeSubDir("stick");
        options.root = root;
        options.repoUrl = Site;
        options.buildbotUrl = string(Site) + "/buildbot";
        options.scratchDir = tmp.makeSubDir("scratch");
        options.packageFile = makePackage(tmp, "v2.0.0-pre0-abc1234");
        // the cover databases
        for (const char *r : {"J", "U", "P"}) {
            string name = string("covers") + r + ".db";
            tmp.writeFile("site/" + name, "sqlite " + name);
            site.files[string(Site) + "/db/" + name] = tmp.at("site/" + name);
            tmp.writeFile("site/" + name + ".sha256", Sha256::ofFile(tmp.at("site/" + name)) + "  " + name + "\n");
            site.files[string(Site) + "/db/" + name + ".sha256"] = tmp.at("site/" + name + ".sha256");
        }
        // RetroArch: the zip, the cores, the libs, the apps, the bundles
        {
            ableem::ZipWriter zip;
            REQUIRE(zip.open(tmp.at("site/retroarch-psc-v1.22.2-4.zip")));
            zip.addBytes("retroarch", "ELF retroarch");
            zip.addBytes("VERSION", "v1.22.2-4\n");
            // the old theme files stay in the zip: installers already in the field copy them by these names
            zip.addBytes("theme/Autobleem2.png", "old wallpaper");
            zip.addBytes("theme/selawik-light.ttf", "ttf");
            zip.addBytes("theme/OFL.txt", "ofl");
            zip.addBytes("theme/ab2-1280x720.png", "wallpaper");
            zip.addBytes("theme/ab2-theme.cfg",
                         "# the theme\nxmb_theme = \"6\"\nxmb_font = \":/assets/xmb/custom/font.ttf\"\n");
            // the theme's assets tree: the whole folder is taken, whatever names the designer adds to it
            zip.addBytes("theme/assets/xmb/custom/png/bg.png", "bg");
            zip.addBytes("theme/assets/xmb/custom/png/phase2-new.png", "phase 2");
            zip.addBytes("theme/assets/xmb/custom/font.ttf", "red hat");
            zip.addBytes("theme/assets/xmb/monochrome/png/setting.png", "ab2 setting");
            zip.addBytes("theme/assets/ozone/regular.ttf", "ozone font");
            zip.addBytes("theme/retroarch-psc.cfg", "# the keys\nxmb_theme = \"7\"\nquit_on_close_content = \"2\"\n");
            zip.addBytes("theme/ab2-states.cfg",
                         "# the save states\nsavestate_auto_save = \"true\"\nsavestate_thumbnail_enable = \"true\"\n");
            REQUIRE(zip.close());
            string zipPath = tmp.at("site/retroarch-psc-v1.22.2-4.zip");
            tmp.writeFile("site/retroarch.json",
                          "{\"version\": \"v1.22.2-4\", \"zip\": " + json("retroarch-psc-v1.22.2-4.zip", zipPath) +
                              ", \"manifest\": \"http://site/m.json\"}");
            site.files[string(Site) + "/psc/retroarch/latest.json"] = tmp.at("site/retroarch.json");
            site.files[string(Site) + "/retroarch-psc-v1.22.2-4.zip"] = zipPath;
        }
        pack("cores",
             TarBuilder()
                 .file("cores/snes9x_libretro.so", "core")
                 .file("info/snes9x_libretro.info", "info")
                 .file("cores.json", "{}"),
             ", \"count\": 1");
        pack("libs",
             TarBuilder()
                 .file("apps/libSDL2_image-2.0.so.0.0.1", "so")
                 .symlink("apps/libSDL2_image-2.0.so.0", "libSDL2_image-2.0.so.0.0.1")
                 .file("modules/xpad.ko", "ko")
                 .file("libs.json", "{}"),
             ", \"count\": 2");
        pack("apps",
             TarBuilder()
                 .file("Apps/doom/run.sh", "run", 0755)
                 .file("Apps/doom/app.ini", "Title=Doom\n")
                 .file("apps.json", "{}"),
             ", \"count\": 1");
        for (const char *b :
             {"assets", "autoconfig", "database-rdb", "database-cursors", "cheats", "overlays", "shaders_glsl"}) {
            ableem::ZipWriter zip;
            string path = tmp.at(string("site/") + b + ".zip");
            REQUIRE(zip.open(path));
            zip.addBytes(string(b) + "-file.txt", b);
            if (string(b) == "assets")
                zip.addBytes("xmb/monochrome/png/setting.png", "stock setting");
            REQUIRE(zip.close());
            site.files[string(Site) + "/buildbot/" + b + ".zip"] = path;
        }
        // the BIOS list: two files, one of them not on the "server"
        tmp.writeFile("site/scph5501.bin", "bios!");
        site.files[string(Site) + "/bios/scph5501.bin"] = tmp.at("site/scph5501.bin");
        string list = "# the list\n" + Sha256::ofFile(tmp.at("site/scph5501.bin")) + " 5 " + Site +
                      "/bios/scph5501.bin scph5501.bin\n" + string(64, 'a') + " 3 " + Site +
                      "/bios/gone.bin sub dir/gone.bin\n";
        tmp.writeFile("site/biospack.txt", list);
        site.files[string(Site) + "/psc/bios/biospack.txt"] = tmp.at("site/biospack.txt");
        // (the url json() writes is Site/<name>; the list's is psc/bios/biospack.txt)
        {
            string j = json("biospack.txt", tmp.at("site/biospack.txt"), ", \"count\": 2, \"total_bytes\": 8");
            size_t at = j.find("http://site/biospack.txt");
            j.replace(at, strlen("http://site/biospack.txt"), string(Site) + "/psc/bios/biospack.txt");
            tmp.writeFile("site/bios.json", j);
        }
        site.files[string(Site) + "/psc/bios/latest.json"] = tmp.at("site/bios.json");
        // the releases: the pre-release this package is from (with UpdateRoms), a stable one without it
        {
            ableem::ZipWriter zip;
            REQUIRE(zip.open(tmp.at("site/UpdateRoms-v2.0.0-pre0-abc1234.zip")));
            zip.addBytes("UpdateRoms/UpdateRoms.exe", "MZ updateroms");
            zip.addBytes("UpdateRoms/README.txt", "readme");
            REQUIRE(zip.close());
            string ur = json("UpdateRoms-v2.0.0-pre0-abc1234.zip", tmp.at("site/UpdateRoms-v2.0.0-pre0-abc1234.zip"));
            string fs = json("autobleem-psc-v2.0.0-pre0-abc1234.tar.gz", options.packageFile);
            tmp.writeFile("site/unstable.json",
                          "{\"version\": \"v2.0.0-pre0-abc1234\", \"prerelease\": true, \"files\": {\"psc-fs\": " + fs +
                              ", \"updateroms\": " + ur + "}}");
            tmp.writeFile("site/latest.json", "{\"version\": \"v1.9.9\", \"files\": {\"psc\": " + fs + "}}");
            site.files[string(Site) + "/releases/unstable.json"] = tmp.at("site/unstable.json");
            site.files[string(Site) + "/releases/latest.json"] = tmp.at("site/latest.json");
            site.files[string(Site) + "/UpdateRoms-v2.0.0-pre0-abc1234.zip"] =
                tmp.at("site/UpdateRoms-v2.0.0-pre0-abc1234.zip");
        }
        // the samples: a PS1 game, a NES ROM with its box art
        {
            TarBuilder b;
            b.file("Games/Tetrade/Tetrade.cue", "cue")
                .file("Games/Tetrade/Game.ini", "Title=Tetrade\n")
                .file("SAMPLES.md", "# samples\n");
            b.file("RetroArch/roms/Nintendo - Nintendo Entertainment System/Nova.nes", "nes");
            b.file("RetroArch/thumbnails/Nintendo - Nintendo Entertainment System/Named_Boxarts/Nova.png", "png");
            REQUIRE(b.writeTarGz(tmp.at("site/samples-20260920.tar.gz")));
            tmp.writeFile("site/samples.json", json("samples-20260920.tar.gz", tmp.at("site/samples-20260920.tar.gz"),
                                                    ", \"date\": \"20260920\""));
            site.files[string(Site) + "/samples/latest.json"] = tmp.at("site/samples.json");
            site.files[string(Site) + "/samples-20260920.tar.gz"] = tmp.at("site/samples-20260920.tar.gz");
        }
    }

    void pack(const string &kind, const TarBuilder &b, const string &extra) {
        string name = kind + "-psc-20260920.tar.gz";
        REQUIRE(b.writeTarGz(tmp.at("site/" + name)));
        tmp.writeFile("site/" + kind + ".json", json(name, tmp.at("site/" + name), ", \"date\": \"20260920\"" + extra));
        site.files[string(Site) + "/psc/" + kind + "/latest.json"] = tmp.at("site/" + kind + ".json");
        site.files[string(Site) + "/" + name] = tmp.at("site/" + name);
    }

    bool run(string &error) {
        return InstallerJob::run(options, site, out, []() { return false; }, error);
    }
    bool has(const string &rel) const { return DirEntry::exists(root + "/" + rel); }
};

} // namespace

TEST_CASE("packageNextTo finds the newest package beside the program") {
    TempDir tmp("installer");
    tmp.writeFile("bin/autobleem-psc-v2.0.0-pre0-aaa.tar.gz", "x");
    tmp.writeFile("bin/autobleem-psc-v2.0.1.tar.gz", "x");
    tmp.writeFile("bin/other.tar.gz", "x");
    CHECK(InstallerJob::packageNextTo(tmp.at("bin/AutoBleemInstaller.exe")) ==
          InstallerJob::normalizeRoot(tmp.at("bin/autobleem-psc-v2.0.1.tar.gz")));
    CHECK(InstallerJob::packageNextTo(tmp.at("nowhere/x.exe")).empty());
    CHECK(InstallerJob::normalizeRoot("F:") == "F:/");
    CHECK(InstallerJob::normalizeRoot("F:\\") == "F:/");
    CHECK(InstallerJob::normalizeRoot("/media/me/SONY/") == "/media/me/SONY");
}

TEST_CASE("a fresh install with the default options: the package and the three cover databases") {
    Fixture fx;
    StickInfo before = InstallerJob::inspect(fx.options);
    CHECK(before.isStick);
    CHECK_FALSE(before.installed);
    CHECK(before.packageVersion == "v2.0.0-pre0-abc1234");
    CHECK(InstallerJob::phasesFor(fx.options, before) == vector<string>{"Getting the package", "Preparing the stick",
                                                                        "Unpacking AutoBleem", "UpdateRoms",
                                                                        "Cover databases", "Finishing"});

    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.out.phases == vector<string>{"Getting the package", "Preparing the stick", "Unpacking AutoBleem",
                                          "UpdateRoms", "Cover databases", "Finishing"});
    // UpdateRoms from the release this package belongs to (the pre-release names it)
    CHECK(fx.tmp.readFile("stick/UpdateRoms/UpdateRoms.exe") == "MZ updateroms");
    CHECK(fx.site.count("UpdateRoms-v2.0.0-pre0-abc1234.zip") == 1);
    CHECK(fx.tmp.readFile("stick/Autobleem/bin/autobleem/autobleem-gui") == "ELF v2.0.0-pre0-abc1234");
    CHECK(fx.tmp.readFile("stick/VERSION") == "v2.0.0-pre0-abc1234\n");
    CHECK(fx.has("Games/!SaveStates"));
    CHECK(fx.has("Games/!MemCards"));
    CHECK(fx.has("System/Logs"));
    // a fresh install decides nothing about the processors' on/off: with no sequence.ini the launcher's first
    // boot stores every one of them off, the shipped Unzip included (SDK-11)
    CHECK_FALSE(fx.has("System/Processors/sequence.ini"));
    CHECK(fx.tmp.readFile("stick/Autobleem/bin/db/coversJ.db") == "sqlite coversJ.db");
    CHECK(fx.has("Autobleem/bin/db/coversU.db"));
    CHECK(fx.has("Autobleem/bin/db/coversP.db"));
    CHECK_FALSE(fx.has("RetroArch"));
    CHECK(fx.site.count("psc/") == 0);     // nothing of RetroArch's was asked for
    CHECK_FALSE(fx.has("System/Install")); // the scratch dir is elsewhere in the tests, and cleaned in any case

    StickInfo after = InstallerJob::inspect(fx.options);
    CHECK(after.installed);
    CHECK(after.installedVersion == "v2.0.0-pre0-abc1234");
    CHECK(after.hasCovers[0]);
    CHECK_FALSE(after.hasRetroArch);
}

TEST_CASE("only the chosen cover databases, and one already there is not fetched again") {
    Fixture fx;
    fx.options.coversJapan = false;
    fx.tmp.writeFile("stick/Autobleem/bin/db/coversU.db", "sqlite coversU.db"); // the very file
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK_FALSE(fx.has("Autobleem/bin/db/coversJ.db"));
    CHECK(fx.has("Autobleem/bin/db/coversP.db"));
    CHECK(fx.site.count("/db/coversU.db.sha256") == 1);
    CHECK(fx.site.count("/db/coversU.db") == 1); // the sidecar only, not the database
    CHECK(fx.out.said("coversU.db is already there"));
}

TEST_CASE("an update replaces what the package ships and keeps the user's files and settings") {
    Fixture fx;
    string error;
    REQUIRE(fx.run(error));
    // the user's life on the stick
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/config.ini", "theme=aergb\nlanguage=Polish\n");
    fx.tmp.writeFile("stick/Games/Crash/Crash.cue", "cue");
    fx.tmp.writeFile("stick/Games/!MemCards/Crash/card1.mcd", "mcd");
    fx.tmp.writeFile("stick/System/Databases/regional.db", "db");
    fx.tmp.writeFile("stick/Themes/mine/theme.json", "{}");
    fx.tmp.writeFile("stick/Themes/ab2/stale.png", "old");                   // a shipped theme's stray file goes
    fx.tmp.writeFile("stick/Apps/doom/run.sh", "run");                       // a user's app stays
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/old-file.txt", "stale"); // the launcher's folder is replaced
    fx.tmp.writeFile("stick/Autobleem/rc/stale.sh", "old");
    fx.tmp.writeFile("stick/Docs/old.txt", "old");
    fx.tmp.writeFile("stick/UpdateRoms/stale.dll", "old");
    fx.tmp.writeFile("stick/Extensions/pscbios/bin/psc/old.so", "old");     // PSC-Bios, shipped: replaced
    fx.tmp.writeFile("stick/Extensions/store/stale.txt", "old");            // the Store, shipped: replaced
    fx.tmp.writeFile("stick/Extensions/mine/extension.ini", "[extension]"); // one the user put there stays
    fx.tmp.writeFile("stick/System/Extensions/store/sources.txt", "https://x/list.tsv\n"); // the Store's own
    // SDK-11: a processor the user installed, its on/off, and the data folder it keeps for itself
    fx.tmp.writeFile("stick/System/Processors/mine/processor.ini", "[Processor]\nVersion=1.0\n");
    fx.tmp.writeFile("stick/System/Processors/mine/bin/psc/mine", "prog");
    fx.tmp.writeFile("stick/System/Processors/sequence.ini", "[ps1]\nmine\n\n[roms]\n-mine\n");
    fx.tmp.writeFile("stick/Home/processors/mine/cache.dat", "data");

    Fixture next; // a newer package, the same stick
    next.options = fx.options;
    next.options.packageFile = makePackage(next.tmp, "v2.0.1");
    StickInfo info = InstallerJob::inspect(next.options);
    CHECK(info.installed);
    CHECK(InstallerJob::phasesFor(next.options, info)[1] == "Preparing the update");
    REQUIRE_MESSAGE(InstallerJob::run(next.options, next.site, next.out, []() { return false; }, error), error);

    CHECK(fx.tmp.readFile("stick/Autobleem/bin/autobleem/autobleem-gui") == "ELF v2.0.1");
    CHECK(fx.tmp.readFile("stick/VERSION") == "v2.0.1\n");
    // the user's settings kept, and the PS1 emulator every install lands on set (the file rewritten as
    // the launcher writes it: capitalised keys)
    {
        const string cfg = fx.tmp.readFile("stick/Autobleem/bin/autobleem/config.ini");
        CHECK(cfg.find("Theme=aergb") != string::npos);
        CHECK(cfg.find("Language=Polish") != string::npos);
        CHECK(cfg.find("Emulator=pcsx-abnxt") != string::npos);
    }
    CHECK(fx.has("Games/Crash/Crash.cue"));
    CHECK(fx.has("Games/!MemCards/Crash/card1.mcd"));
    CHECK(fx.has("System/Databases/regional.db"));
    CHECK(fx.has("Themes/mine/theme.json"));
    CHECK(fx.has("Apps/doom/run.sh"));
    CHECK(fx.has("Autobleem/bin/db/coversJ.db"));
    // the scanner processors' folder is made, with its README
    CHECK(fx.tmp.readFile("stick/System/Processors/README.txt").find("scanner processors") != string::npos);
    // an update never removes a processor the user installed, nor its settings or data (SDK-11)
    CHECK(fx.tmp.readFile("stick/System/Processors/mine/bin/psc/mine") == "prog");
    CHECK(fx.has("System/Processors/mine/processor.ini"));
    CHECK(fx.tmp.readFile("stick/System/Processors/sequence.ini") == "[ps1]\nmine\n\n[roms]\n-mine\n");
    CHECK(fx.tmp.readFile("stick/Home/processors/mine/cache.dat") == "data");
    // and the extensions', next to the one the user put there (kept)
    CHECK(fx.tmp.readFile("stick/Extensions/README.txt").find("AutoBleem extensions") != string::npos);
    CHECK_FALSE(fx.has("Themes/ab2/stale.png"));
    CHECK_FALSE(fx.has("Autobleem/bin/autobleem/old-file.txt"));
    CHECK_FALSE(fx.has("Autobleem/rc/stale.sh"));
    CHECK_FALSE(fx.has("Docs/old.txt"));
    CHECK_FALSE(fx.has("UpdateRoms/stale.dll"));
    CHECK_FALSE(fx.has("Extensions/pscbios/bin/psc/old.so"));
    CHECK(fx.has("Extensions/pscbios/bin/psc/pscbios.so"));
    // the Store: the package's version, nothing of the old one; its state and the user's own extension kept
    CHECK_FALSE(fx.has("Extensions/store/stale.txt"));
    CHECK(fx.tmp.readFile("stick/Extensions/store/extension.ini").find("Version=v2.0.1") != string::npos);
    CHECK(fx.has("Extensions/store/bin/psc/store.so"));
    CHECK(fx.has("Extensions/mine/extension.ini"));
    CHECK(fx.tmp.readFile("stick/System/Extensions/store/sources.txt") == "https://x/list.tsv\n");
    CHECK(fx.has("UpdateRoms/UpdateRoms.exe"));
    CHECK(next.out.said("config.ini kept as it was"));
    CHECK(next.out.said("Updated."));
}

//******************
// UIREV-51: the default theme on an update
//******************
TEST_CASE("a fresh install lands on the default theme") {
    Fixture fx;
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("stick/Autobleem/bin/autobleem/config.ini").find("Theme=ab2.0.0") != string::npos);
    CHECK(fx.has("Themes/ab2.0.0/theme.json"));
}

TEST_CASE("an update that brings the default theme to a stick without it switches the theme once") {
    Fixture fx;
    // an older version: its launcher, its config on another theme, no default theme folder
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/autobleem-gui", "ELF v1.9.0");
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/config.ini", "theme=aergb\nlanguage=Polish\n");
    fx.tmp.writeFile("stick/VERSION", "v1.9.0\n");
    fx.tmp.writeFile("stick/Themes/ab2/theme.json", "{}");
    CHECK_FALSE(fx.has("Themes/ab2.0.0"));
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    const string cfg = fx.tmp.readFile("stick/Autobleem/bin/autobleem/config.ini");
    CHECK(cfg.find("Theme=ab2.0.0") != string::npos);
    CHECK(cfg.find("Language=Polish") != string::npos); // the other settings stay
    CHECK(fx.out.said("theme set to ab2.0.0"));
    CHECK(fx.has("Themes/ab2.0.0/theme.json"));

    // the next update finds the folder: the user's choice (here back to aergb) is kept
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/config.ini", "theme=aergb\n");
    Fixture next;
    next.options = fx.options;
    next.options.packageFile = makePackage(next.tmp, "v2.0.1");
    REQUIRE_MESSAGE(InstallerJob::run(next.options, next.site, next.out, []() { return false; }, error), error);
    CHECK(fx.tmp.readFile("stick/Autobleem/bin/autobleem/config.ini").find("Theme=aergb") != string::npos);
    CHECK_FALSE(next.out.said("theme set to"));
}

TEST_CASE("an update whose stick already has the default theme folder keeps the user's theme") {
    Fixture fx;
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/autobleem-gui", "ELF v2.0.0");
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/config.ini", "theme=aergb\n");
    fx.tmp.writeFile("stick/Themes/ab2.0.0/theme.json", "{mine}");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("stick/Autobleem/bin/autobleem/config.ini").find("Theme=aergb") != string::npos);
    CHECK_FALSE(fx.out.said("theme set to"));
}

TEST_CASE("a package without the default theme folder does not switch the theme") {
    Fixture fx;
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/autobleem-gui", "ELF v1.9.0");
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/config.ini", "theme=aergb\n");
    TarBuilder b;
    b.dir("./Autobleem").dir("./Autobleem/bin").dir("./Autobleem/bin/autobleem");
    b.file("./Autobleem/bin/autobleem/autobleem-gui", "ELF v2", 0755);
    b.dir("./Themes").dir("./Themes/ab2").file("./Themes/ab2/theme.json", "{}");
    b.file("./VERSION", "v2\n");
    fx.options.packageFile = fx.tmp.at("pkg/other.tar.gz");
    REQUIRE(b.writeTarGz(fx.options.packageFile));
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("stick/Autobleem/bin/autobleem/config.ini").find("Theme=aergb") != string::npos);
}

TEST_CASE("RetroArch, its cores, libraries, apps and bundles, then the BIOS files and the samples") {
    Fixture fx;
    fx.options.retroarch = true;
    fx.options.bios = true;
    fx.options.samples = true;
    StickInfo before = InstallerJob::inspect(fx.options);
    vector<string> phases = InstallerJob::phasesFor(fx.options, before);
    CHECK(phases.size() == 13);
    CHECK(phases[5] == "RetroArch");
    CHECK(phases[10] == "BIOS files");
    CHECK(phases[11] == "Sample games");

    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.out.phases == phases);
    // RetroArch/bin: the binary, the theme where the cfg keys look, the cfg with the directories
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF retroarch");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/VERSION") == "v1.22.2-4\n");
    CHECK(fx.has("RetroArch/bin/Retroarch themes/ab2-1280x720.png"));
    // the theme over libretro's assets bundle: ours wins, the stock file is kept once as .prab2, the bundle's
    // own files are still there (the theme went in after it, not before)
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png") == "ab2 setting");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png.prab2") == "stock setting");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/custom/png/phase2-new.png") == "phase 2");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/custom/font.ttf") == "red hat");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/ozone/regular.ttf") == "ozone font");
    CHECK_FALSE(fx.has("RetroArch/bin/assets/xmb/custom/png/bg.png.prab2")); // nothing stock to keep
    CHECK(fx.has("RetroArch/bin/assets/assets-file.txt"));
    CHECK(fx.out.said("the AutoBleem 2 theme: 5 files"));
    string cfg = fx.tmp.readFile("stick/RetroArch/bin/retroarch.cfg");
    CHECK(cfg.find("system_directory = \"/media/RetroArch/bios\"") != string::npos);
    CHECK(cfg.find("rgui_browser_directory = \"/media/RetroArch/roms/\"") != string::npos);
    CHECK(cfg.find("libretro_directory = \":/cores\"") != string::npos);
    // the build's keys first, then the theme's own, after its files landed
    CHECK(cfg.find("xmb_theme = \"7\"") == string::npos);
    CHECK(cfg.find("xmb_theme = \"6\"") != string::npos);
    CHECK(cfg.find("quit_on_close_content = \"2\"") != string::npos);
    CHECK(cfg.find("# the keys") == string::npos);
    CHECK(cfg.find("# the theme") == string::npos);
    CHECK(fx.has("RetroArch/bin/Retroarch themes/Autobleem2.png")); // the old files still land
    CHECK(fx.has("RetroArch/bin/playlists"));
    CHECK(fx.has("RetroArch/roms"));
    // the packs
    CHECK(fx.has("RetroArch/bin/cores/snes9x_libretro.so"));
    CHECK(fx.has("RetroArch/bin/info/snes9x_libretro.info"));
    CHECK(fx.has("Autobleem/lib/apps/libSDL2_image-2.0.so.0.0.1"));
    CHECK_FALSE(fx.has("Autobleem/lib/apps/libSDL2_image-2.0.so.0")); // a symlink: not on FAT
    CHECK(fx.has("Autobleem/lib/modules/xpad.ko"));
    CHECK(fx.has("Apps/doom/run.sh"));
    CHECK_FALSE(fx.has("apps.json"));
    // the bundles
    CHECK(fx.has("RetroArch/bin/assets/assets-file.txt"));
    CHECK(fx.has("RetroArch/bin/database/rdb/database-rdb-file.txt"));
    CHECK(fx.has("RetroArch/bin/shaders/shaders_glsl-file.txt"));
    // the BIOS files: one fetched, the missing one reported, the run goes on
    CHECK(fx.tmp.readFile("stick/RetroArch/bios/scph5501.bin") == "bios!");
    CHECK_FALSE(fx.has("RetroArch/bios/sub dir/gone.bin"));
    CHECK(fx.out.said("could not fetch sub dir/gone.bin"));
    CHECK(fx.out.said("1 fetched, 0 already there, 1 failed"));
    // the samples, the console's way round
    CHECK(fx.has("Games/Tetrade/Tetrade.cue"));
    CHECK(fx.has("SAMPLES.md"));
    CHECK(fx.has("RetroArch/roms/Nintendo - Nintendo Entertainment System/Nova.nes"));
    CHECK(fx.has("RetroArch/bin/thumbnails/Nintendo - Nintendo Entertainment System/Named_Boxarts/Nova.png"));
    CHECK_FALSE(fx.has("RetroArch/thumbnails"));
    CHECK(fx.tmp.readFile("stick/System/samples.txt") == "samples-20260920.tar.gz\n");

    // again: RetroArch is there at that version, the BIOS file is kept, the samples are not put back
    Fixture again;
    again.options = fx.options;
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch.cfg", cfg + "video_smooth = \"true\"\n");
    DirEntry::removeDirAndContents(fx.root + "/Games/Tetrade");
    REQUIRE_MESSAGE(InstallerJob::run(again.options, again.site, again.out, []() { return false; }, error), error);
    CHECK(again.out.said("RetroArch v1.22.2-4 is on the stick already"));
    CHECK(again.out.said("keeping the existing retroarch.cfg"));
    CHECK(again.out.said("0 fetched, 1 already there, 1 failed"));
    CHECK(again.out.said("not again"));
    CHECK_FALSE(fx.has("Games/Tetrade"));
    CHECK(again.site.count("retroarch-psc-v1.22.2-4.zip") == 0);
}

TEST_CASE("the BIOS files without RetroArch on the stick are not a phase; samples without RetroArch skip its part") {
    Fixture fx;
    fx.options.bios = true;
    fx.options.samples = true;
    StickInfo info = InstallerJob::inspect(fx.options);
    vector<string> phases = InstallerJob::phasesFor(fx.options, info);
    CHECK(find(phases.begin(), phases.end(), "BIOS files") == phases.end());
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.has("Games/Tetrade/Tetrade.cue"));
    CHECK_FALSE(fx.has("RetroArch/roms"));
    CHECK(fx.out.said("need RetroArch"));
}

TEST_CASE("a stop request, a bad package and a missing drive are reported") {
    Fixture fx;
    string error;
    CHECK_FALSE(InstallerJob::run(fx.options, fx.site, fx.out, []() { return true; }, error));
    CHECK(error == "Stopped");
    CHECK_FALSE(fx.has("Autobleem/bin/autobleem/autobleem-gui"));

    fx.tmp.writeFile("pkg/bad.tar.gz", "not a tarball");
    fx.options.packageFile = fx.tmp.at("pkg/bad.tar.gz");
    CHECK_FALSE(fx.run(error));
    CHECK_FALSE(error.empty());

    Fixture other;
    other.options.root = other.tmp.at("nope");
    CHECK_FALSE(InstallerJob::run(other.options, other.site, other.out, []() { return false; }, error));
    CHECK(error.find("No such drive") != string::npos);

    // the site down: the covers phase fails with the reason
    Fixture down;
    down.site.files.clear();
    CHECK_FALSE(down.run(error));
    CHECK(error.find("coversJ.db") != string::npos);
}

TEST_CASE("an AutoBleem 1.0 / NG stick is brought to the new layout before the update, nothing of the user's lost") {
    Fixture fx;
    // the old stick: RetroBoot's tree at the root, roms/ and themes/ beside it, an old launcher
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/autobleem-gui", "ELF 1.0");
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/config.ini", "theme=aergb\n");
    fx.tmp.writeFile("stick/Games/Crash/Crash.cue", "cue");
    fx.tmp.writeFile("stick/Games/!MemCards/Crash/card1.mcd", "mcd");
    fx.tmp.writeFile("stick/themes/mine/theme.ini", "Background=bg.png\n");
    fx.tmp.writeFile("stick/retroarch/retroarch", "ELF 1.9.0");
    fx.tmp.writeFile(
        "stick/retroarch/retroarch.cfg",
        "libretro_directory = \":/cores\"\nsystem_directory = \":/system\"\nrgui_browser_directory = \"/media/roms/\"\n"
        "xmb_theme = \"8\"\ncontent_favorites_directory = \"default\"\n");
    fx.tmp.writeFile("stick/retroarch/cores/snes9x_libretro.so", "core");
    fx.tmp.writeFile("stick/retroarch/system/scph5501.bin", "bios");
    fx.tmp.writeFile("stick/retroarch/saves/Crash.srm", "srm");
    fx.tmp.writeFile(
        "stick/retroarch/playlists/Nintendo - Super Nintendo Entertainment System.lpl",
        "{\"items\": [{\"path\": \"/media/roms/Nintendo - Super Nintendo Entertainment System/Zelda.sfc\", "
        "\"core_path\": \"/media/retroarch/cores/snes9x_libretro.so\"}]}");
    fx.tmp.writeFile("stick/retroarch/playlists/Applications.lpl", "{}");
    fx.tmp.writeFile("stick/retroarch/playlists/Sony - PlayStation.lpl", "{}");
    fx.tmp.writeFile("stick/retroarch/playlists/AutoBleem.lpl", "{}");
    fx.tmp.writeFile("stick/retroarch/retroboot/assets/lib/libSDL2_image-2.0.so.0.0.1", "so");
    fx.tmp.writeFile("stick/retroarch/retroboot/modules/xpad.ko", "ko");
    fx.tmp.writeFile("stick/retroarch/retroboot/bin/launch_rfa.sh", "rb");
    fx.tmp.writeFile("stick/retroarch/apps/eduke32/eduke32", "ELF");
    fx.tmp.writeFile("stick/retroarch/apps/eduke32/launcheduke32.sh",
                     "export LD_LIBRARY_PATH=\"${RB_LIBRARY_PATH}\"\ncd /media/retroarch/apps/eduke32\n./eduke32 > "
                     "/media/retroarch/logs/duke3d.log\n");
    fx.tmp.writeFile("stick/Apps/eduke32/app.ini", "Title=Duke\n");
    fx.tmp.writeFile(
        "stick/Apps/eduke32/run.sh",
        "#! /bin/sh\n# Retroboot App Launcher\n\nif [ -z \"${RB_LIBRARY_PATH}\" ]; then\n\tsource "
        "/media/retroarch/retroboot/bin/loadconfig.sh\nfi\n"
        "if [ ! -d \"${RB_LIBRARY_PATH}\" ]; then\n\tsh /media/retroarch/retroboot/bin/init_libs.sh\nfi\nsh "
        "\"/media/retroarch/apps/eduke32/launcheduke32.sh\"\n");
    fx.tmp.writeFile("stick/Apps/retroboot/run.sh", "sh /media/retroarch/retroboot/bin/launch_rfa.sh\n");
    fx.tmp.writeFile("stick/Apps/retroboot/app.ini", "Title=RetroBoot\n");
    fx.tmp.writeFile("stick/roms/Nintendo - Super Nintendo Entertainment System/Zelda.sfc", "rom");

    StickInfo info = InstallerJob::inspect(fx.options);
    CHECK(info.installed);
    CHECK(info.legacyLayout);
    CHECK(info.hasRetroArch);
    vector<string> phases = InstallerJob::phasesFor(fx.options, info);
    CHECK(phases[1] == "Preparing the update");
    CHECK(phases[2] == "Bringing the old layout up to date");
    CHECK(phases[4] == "UpdateRoms");

    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    // moved
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF 1.9.0");
    CHECK(fx.has("RetroArch/bin/cores/snes9x_libretro.so"));
    CHECK(fx.has("RetroArch/bin/saves/Crash.srm"));
    CHECK(fx.tmp.readFile("stick/RetroArch/bios/scph5501.bin") == "bios");
    CHECK_FALSE(fx.has("RetroArch/bin/system"));
    CHECK(fx.has("RetroArch/roms/Nintendo - Super Nintendo Entertainment System/Zelda.sfc"));
    CHECK_FALSE(fx.has("roms"));
    CHECK(fx.has("Themes/mine/theme.ini"));
    CHECK(fx.has("Games/Crash/Crash.cue"));
    CHECK(fx.has("Games/!MemCards/Crash/card1.mcd"));
    // rewritten
    string cfg = fx.tmp.readFile("stick/RetroArch/bin/retroarch.cfg");
    CHECK(cfg.find("system_directory = \"/media/RetroArch/bios\"") != string::npos);
    CHECK(cfg.find("rgui_browser_directory = \"/media/RetroArch/roms/\"") != string::npos);
    CHECK(cfg.find("xmb_theme = \"8\"") != string::npos); // RetroBoot's RetroArch stays, so does its theme
    string lpl = fx.tmp.readFile("stick/RetroArch/bin/playlists/Nintendo - Super Nintendo Entertainment System.lpl");
    CHECK(lpl.find("/media/RetroArch/roms/Nintendo - Super Nintendo Entertainment System/Zelda.sfc") != string::npos);
    CHECK(lpl.find("/media/RetroArch/bin/cores/snes9x_libretro.so") != string::npos);
    CHECK_FALSE(fx.has("RetroArch/bin/playlists/Applications.lpl"));
    CHECK_FALSE(fx.has("RetroArch/bin/playlists/Sony - PlayStation.lpl"));
    CHECK_FALSE(fx.has("RetroArch/bin/playlists/AutoBleem.lpl"));
    // the libraries, the apps
    CHECK(fx.has("Autobleem/lib/apps/libSDL2_image-2.0.so.0.0.1"));
    CHECK(fx.has("Autobleem/lib/modules/xpad.ko"));
    CHECK(fx.has("Apps/eduke32/eduke32"));
    string run = fx.tmp.readFile("stick/Apps/eduke32/run.sh");
    CHECK(run == "#! /bin/sh\n# Retroboot App Launcher\n. /media/Autobleem/rc/app_env.sh\n\nsh "
                 "\"/media/Apps/eduke32/launcheduke32.sh\"\n");
    string launch = fx.tmp.readFile("stick/Apps/eduke32/launcheduke32.sh");
    CHECK(
        launch ==
        "export LD_LIBRARY_PATH=\"/tmp/applib\"\ncd /media/Apps/eduke32\n./eduke32 > /media/System/Logs/duke3d.log\n");
    CHECK_FALSE(fx.has("Apps/retroboot"));
    // RetroBoot's tree is the user's to delete: not asked, it stays whole
    CHECK(fx.has("RetroArch/bin/retroboot/bin/launch_rfa.sh"));
    // and the update itself went through, the old settings kept
    CHECK(fx.tmp.readFile("stick/Autobleem/bin/autobleem/autobleem-gui") == "ELF v2.0.0-pre0-abc1234");
    {
        const string cfg = fx.tmp.readFile("stick/Autobleem/bin/autobleem/config.ini");
        // a 1.0 stick has no default theme folder: the conversion switches the theme to it (UIREV-51)
        CHECK(cfg.find("Theme=ab2.0.0") != string::npos);
        CHECK(cfg.find("Emulator=pcsx-abnxt") != string::npos);
    }
    CHECK(fx.out.said("theme set to ab2.0.0"));
    CHECK(fx.out.said("retroarch -> RetroArch/bin"));

    // a second run finds the new layout and does not migrate again
    Fixture again;
    again.options = fx.options;
    CHECK_FALSE(InstallerJob::inspect(again.options).legacyLayout);
}

// a RetroBoot stick with an app in retroarch/apps/ that Apps/ also has (moved by the migration) and, with
// `strayApp`, one that Apps/ does not have
static void retroBootStick(Fixture &fx, bool strayApp) {
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/autobleem-gui", "ELF 1.0");
    fx.tmp.writeFile("stick/retroarch/retroarch", "ELF 1.9.0");
    fx.tmp.writeFile("stick/retroarch/retroarch.cfg", "xmb_theme = \"8\"\n");
    fx.tmp.writeFile("stick/retroarch/retroboot/assets/lib/libSDL2_image-2.0.so.0.0.1", "so");
    fx.tmp.writeFile("stick/retroarch/retroboot/modules/xpad.ko", "ko");
    fx.tmp.writeFile("stick/retroarch/retroboot/emulationstation/.emulationstation/es_settings.cfg", "es");
    fx.tmp.writeFile("stick/retroarch/apps/eduke32/eduke32", "ELF");
    fx.tmp.writeFile("stick/Apps/eduke32/app.ini", "Title=Duke\n");
    fx.tmp.writeFile("stick/Apps/eduke32/run.sh", "sh \"/media/retroarch/apps/eduke32/launch.sh\"\n");
    if (strayApp)
        fx.tmp.writeFile("stick/retroarch/apps/doom/doom.wad", "wad");
}

TEST_CASE("RetroBoot's tree and apps folder stay when the user did not ask for their removal") {
    Fixture fx;
    retroBootStick(fx, false);
    REQUIRE_FALSE(fx.options.removeRetroBoot); // the default
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.has("RetroArch/bin/retroboot/emulationstation/.emulationstation/es_settings.cfg"));
    CHECK(fx.has("RetroArch/bin/retroboot/modules/xpad.ko"));
    CHECK(fx.has("Autobleem/lib/modules/xpad.ko")); // copied, the original untouched
    CHECK_FALSE(fx.out.said("removed RetroArch/bin/retroboot"));
}

TEST_CASE("RetroBoot's tree is removed when the user asked, after its libraries are in Autobleem/lib") {
    Fixture fx;
    retroBootStick(fx, false);
    fx.options.removeRetroBoot = true;
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.has("Autobleem/lib/apps/libSDL2_image-2.0.so.0.0.1"));
    CHECK(fx.has("Autobleem/lib/modules/xpad.ko"));
    CHECK_FALSE(fx.has("RetroArch/bin/retroboot"));
    CHECK_FALSE(fx.has("RetroArch/bin/apps"));
    CHECK(fx.has("Apps/eduke32/eduke32"));
    CHECK(fx.has("RetroArch/bin/retroarch"));
    CHECK(fx.out.said("removed RetroArch/bin/retroboot"));
}

TEST_CASE("nothing of RetroBoot is removed while a library of it is missing from Autobleem/lib") {
    Fixture fx;
    retroBootStick(fx, false);
    InstallerJob::inspect(fx.options);
    // the migration has run; a module is lost from Autobleem/lib behind its back
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    REQUIRE(fx.has("RetroArch/bin/retroboot/modules/xpad.ko"));
    REQUIRE(DirEntry::removeFile(fx.root + "/Autobleem/lib/modules/xpad.ko"));
    vector<string> said;
    CHECK_FALSE(LegacyLayout::removeRetroBoot(fx.root, [&](const string &l) { said.push_back(l); }));
    CHECK(fx.has("RetroArch/bin/retroboot/modules/xpad.ko"));
    CHECK(fx.has("RetroArch/bin/retroboot/emulationstation/.emulationstation/es_settings.cfg"));
    REQUIRE(said.size() == 1);
    CHECK(said[0].find("xpad.ko") != string::npos);
    // a script still starting something in retroboot/ blocks it too
    fx.tmp.writeFile("stick/Autobleem/lib/modules/xpad.ko", "ko");
    fx.tmp.writeFile("stick/Apps/old/run.sh", "sh /media/RetroArch/bin/retroboot/bin/start.sh\n");
    said.clear();
    CHECK_FALSE(LegacyLayout::removeRetroBoot(fx.root, [&](const string &l) { said.push_back(l); }));
    CHECK(fx.has("RetroArch/bin/retroboot"));
    // and with both in order it goes
    DirEntry::removeFile(fx.root + "/Apps/old/run.sh");
    CHECK(LegacyLayout::removeRetroBoot(fx.root, [&](const string &l) { said.push_back(l); }));
    CHECK_FALSE(fx.has("RetroArch/bin/retroboot"));
}

TEST_CASE("an app in RetroArch/bin/apps that Apps/ does not have stays with its files") {
    Fixture fx;
    retroBootStick(fx, true);
    fx.options.removeRetroBoot = true;
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK_FALSE(fx.has("RetroArch/bin/retroboot"));
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/apps/doom/doom.wad") == "wad");
    CHECK_FALSE(fx.has("RetroArch/bin/apps/eduke32")); // moved to Apps/ by the migration
    CHECK(fx.has("Apps/eduke32/eduke32"));
    CHECK(fx.out.said("RetroArch/bin/apps kept"));
}

TEST_CASE("a new RetroArch build over a RetroBoot-era retroarch.cfg sets its own keys, keeps the rest") {
    Fixture fx;
    fx.options.retroarch = true;
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "ELF 1.9.0");
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch.cfg",
                     "video_smooth = \"true\"\nxmb_theme = \"8\"\nmenu_driver = \"xmb\"\n");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    string cfg = fx.tmp.readFile("stick/RetroArch/bin/retroarch.cfg");
    // the build's two keys, the save-state keys (their own file, no theme files to wait for), then the theme's two
    // (xmb_theme replaced again, xmb_font appended)
    CHECK(cfg == "video_smooth = \"true\"\nxmb_theme = \"6\"\nmenu_driver = \"xmb\"\nquit_on_close_content = \"2\"\n"
                 "savestate_auto_save = \"true\"\nsavestate_thumbnail_enable = \"true\"\n"
                 "xmb_font = \":/assets/xmb/custom/font.ttf\"\n");
    CHECK(fx.out.said("2 keys of this RetroArch build set in it"));
    CHECK(fx.out.said("2 save-state keys set"));
    CHECK(fx.out.said("2 theme keys set"));
}

TEST_CASE("old ES-style ROM folders become the RetroArch database names the scan reads") {
    TempDir tmp("installer");
    const string roms = tmp.makeSubDir("roms");
    tmp.writeFile("roms/nes/Nova.nes", "nes");
    tmp.writeFile("roms/megadrive/Sonic.md", "md");
    // famicom and the database's own folder both there: merged, the file already in place wins
    tmp.writeFile("roms/famicom/Zelda.nes", "famicom zelda");
    tmp.writeFile("roms/Nintendo - Nintendo Entertainment System/Zelda.nes", "kept zelda");
    // a case-only rename (daphne -> Daphne)
    tmp.writeFile("roms/daphne/dl.zip", "laser");
    // no confident database: left alone
    tmp.writeFile("roms/psx/Game.cue", "cue");
    vector<string> said;
    const int n = LegacyLayout::convertRomFolders(roms, [&](const string &l) { said.push_back(l); });
    CHECK(n == 4);
    CHECK(tmp.readFile("roms/Nintendo - Nintendo Entertainment System/Nova.nes") == "nes");
    CHECK(tmp.readFile("roms/Nintendo - Nintendo Entertainment System/Zelda.nes") == "kept zelda");
    CHECK(tmp.readFile("roms/Sega - Mega Drive - Genesis/Sonic.md") == "md");
    CHECK(tmp.readFile("roms/Daphne/dl.zip") == "laser");
    CHECK(tmp.readFile("roms/psx/Game.cue") == "cue");
    CHECK_FALSE(DirEntry::exists(roms + "/nes"));
    CHECK_FALSE(DirEntry::exists(roms + "/famicom"));
    CHECK_FALSE(DirEntry::exists(roms + "/megadrive"));
    // a second pass has nothing to do
    CHECK(LegacyLayout::convertRomFolders(roms, [](const string &) {}) == 0);
}

TEST_CASE("with RetroArch every listed system gets its roms folder - an old folder renamed, nothing else touched") {
    Fixture fx;
    fx.options.retroarch = true;
    // a stick an older installer left: RetroArch in the new layout, a ROM in an ES-style folder
    fx.tmp.writeFile("stick/RetroArch/roms/nes/Nova.nes", "nes");
    fx.tmp.writeFile("stick/RetroArch/roms/Sega - Mega Drive - Genesis/Sonic.md", "mine");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("stick/RetroArch/roms/Nintendo - Nintendo Entertainment System/Nova.nes") == "nes");
    CHECK_FALSE(fx.has("RetroArch/roms/nes"));
    CHECK(fx.tmp.readFile("stick/RetroArch/roms/Sega - Mega Drive - Genesis/Sonic.md") == "mine");
    CHECK(fx.out.said("roms/: 0 system folder(s) made, 2 already there"));

    // a fresh stick: both folders made
    Fixture fresh;
    fresh.options.retroarch = true;
    REQUIRE_MESSAGE(fresh.run(error), error);
    CHECK(DirEntry::isDirectory(fresh.root + "/RetroArch/roms/Nintendo - Nintendo Entertainment System"));
    CHECK(DirEntry::isDirectory(fresh.root + "/RetroArch/roms/Sega - Mega Drive - Genesis"));
    CHECK(fresh.out.said("roms/: 2 system folder(s) made, 0 already there"));

    // without RetroArch, chosen or on the stick, no roms folders at all
    Fixture none;
    REQUIRE_MESSAGE(none.run(error), error);
    CHECK_FALSE(none.has("RetroArch/roms"));
}

TEST_CASE("UpdateRoms: the installer's own copy first; a broken site copy leaves the stick's; the run is logged") {
    // an UpdateRoms/ folder beside the installer (the bundle's) - used, the site not asked
    {
        Fixture fx;
        fx.tmp.writeFile("pkg/UpdateRoms/UpdateRoms.exe", "MZ bundled");
        fx.tmp.writeFile("pkg/UpdateRoms/README.txt", "readme");
        string error;
        REQUIRE_MESSAGE(fx.run(error), error);
        CHECK(fx.tmp.readFile("stick/UpdateRoms/UpdateRoms.exe") == "MZ bundled");
        CHECK(fx.site.count("UpdateRoms-") == 0);
        CHECK(fx.out.said("UpdateRoms from this installer's folder"));
    }
    // the site's zip holds no UpdateRoms.exe: the stick keeps the one it had
    {
        Fixture fx;
        fx.tmp.writeFile("stick/UpdateRoms/UpdateRoms.exe", "MZ old");
        ableem::ZipWriter zip;
        REQUIRE(zip.open(fx.tmp.at("site/UpdateRoms-v2.0.0-pre0-abc1234.zip")));
        zip.addBytes("UpdateRoms/README.txt", "no program here");
        REQUIRE(zip.close());
        const string ur =
            json("UpdateRoms-v2.0.0-pre0-abc1234.zip", fx.tmp.at("site/UpdateRoms-v2.0.0-pre0-abc1234.zip"));
        const string fs = json("autobleem-psc-v2.0.0-pre0-abc1234.tar.gz", fx.options.packageFile);
        fx.tmp.writeFile("site/unstable.json", "{\"version\": \"v2.0.0-pre0-abc1234\", \"prerelease\": true, "
                                               "\"files\": {\"psc-fs\": " +
                                                   fs + ", \"updateroms\": " + ur + "}}");
        string error;
        REQUIRE_MESSAGE(fx.run(error), error);
        CHECK(fx.tmp.readFile("stick/UpdateRoms/UpdateRoms.exe") == "MZ old");
        CHECK(fx.out.said("the stick keeps what it had"));
        // the run's record on the stick
        const string log = fx.tmp.readFile("stick/System/Logs/installer.log");
        CHECK(log.find("AutoBleemInstaller: ") == 0);
        CHECK(log.find("the stick keeps what it had") != string::npos);
    }
}

TEST_CASE("a channel: the stick package and UpdateRoms come from that channel's release on the site") {
    Fixture fx;
    const string pkg = fx.options.packageFile;
    fx.site.files[string(Site) + "/autobleem-psc-v2.0.0-pre0-abc1234.tar.gz"] = pkg;
    fx.options.packageFile.clear();

    SUBCASE("testing: the pre-release's package, downloaded and checked, and its own UpdateRoms") {
        fx.options.channel = "testing";
        ChannelRelease rel;
        string error;
        REQUIRE_MESSAGE(InstallerJob::channelRelease(Site, "testing", fx.site, fx.tmp.path(), rel, error), error);
        CHECK(rel.version == "v2.0.0-pre0-abc1234");
        CHECK(rel.package.name == "autobleem-psc-v2.0.0-pre0-abc1234.tar.gz");
        CHECK(rel.updateRoms.name == "UpdateRoms-v2.0.0-pre0-abc1234.zip");

        REQUIRE_MESSAGE(fx.run(error), error);
        CHECK(fx.out.phases.front() == "Getting the package");
        CHECK(fx.out.said("the testing channel: AutoBleem v2.0.0-pre0-abc1234"));
        CHECK(fx.tmp.readFile("stick/VERSION") == "v2.0.0-pre0-abc1234\n");
        CHECK(fx.tmp.readFile("stick/UpdateRoms/UpdateRoms.exe") == "MZ updateroms");
        CHECK(fx.site.count("autobleem-psc-v2.0.0-pre0-abc1234.tar.gz") == 1);
    }
    SUBCASE("nightly with no development build on the site stands on testing's release") {
        fx.options.channel = "nightly";
        string error;
        REQUIRE_MESSAGE(fx.run(error), error);
        CHECK(fx.site.count("nightly/latest.json") == 1);
        CHECK(fx.tmp.readFile("stick/VERSION") == "v2.0.0-pre0-abc1234\n");
    }
    SUBCASE("a channel from channels.json is read through the lists it names, the run included") {
        fx.site.files[string(Site) + "/preview/latest.json"] = fx.site.files[string(Site) + "/releases/unstable.json"];
        fx.site.files.erase(string(Site) + "/releases/unstable.json");
        ChannelRelease rel;
        string error;
        CHECK_FALSE(InstallerJob::channelRelease(Site, "preview", fx.site, fx.tmp.path(), rel, error));
        REQUIRE_MESSAGE(InstallerJob::channelRelease(Site, "preview", {"preview/latest.json", "releases/latest.json"},
                                                     fx.site, fx.tmp.path(), rel, error),
                        error);
        CHECK(rel.channel == "preview");
        CHECK(rel.version == "v2.0.0-pre0-abc1234");
        fx.options.channel = "preview";
        fx.options.channelIndexes = {"preview/latest.json"};
        REQUIRE_MESSAGE(fx.run(error), error);
        CHECK(fx.out.said("the preview channel: AutoBleem v2.0.0-pre0-abc1234"));
    }
    SUBCASE("release: a list without a stick package is no install") {
        fx.options.channel = "release"; // the fixture's latest.json carries only an old "psc" zip
        string error;
        CHECK_FALSE(fx.run(error));
        CHECK(error.find("no PlayStation Classic package") != string::npos);
        CHECK_FALSE(fx.has("Autobleem"));
    }
    SUBCASE("a package that is not what the catalog says is not unpacked") {
        fx.options.channel = "testing";
        fx.tmp.writeFile("site/other.tar.gz", "not the package");
        fx.site.files[string(Site) + "/autobleem-psc-v2.0.0-pre0-abc1234.tar.gz"] = fx.tmp.at("site/other.tar.gz");
        string error;
        CHECK_FALSE(fx.run(error));
        CHECK_FALSE(fx.has("Autobleem"));
    }
    CHECK(InstallerJob::channelLists("nightly") ==
          vector<string>{"nightly/latest.json", "releases/unstable.json", "releases/latest.json"});
    CHECK(InstallerJob::channelLists("testing") == vector<string>{"releases/unstable.json", "releases/latest.json"});
    CHECK(InstallerJob::channelLists("release") == vector<string>{"releases/latest.json"});
}

TEST_CASE("the theme over an updated stick: only the build's cfg keys change, the stock backup stays the stock file") {
    Fixture fx;
    fx.options.retroarch = true;
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "ELF 1.9.0");
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch.cfg", "video_smooth = \"true\"\nxmb_theme = \"8\"\n");
    // assets already on the stick (the bundle is skipped), with a stock file and an earlier backup of it
    fx.tmp.writeFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png", "our old copy");
    fx.tmp.writeFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png.prab2", "the stock file");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.out.said("assets: already there"));
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png") == "ab2 setting");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png.prab2") == "the stock file");
    CHECK(fx.has("RetroArch/bin/assets/xmb/custom/png/phase2-new.png"));
    string cfg = fx.tmp.readFile("stick/RetroArch/bin/retroarch.cfg");
    CHECK(cfg.find("video_smooth = \"true\"") != string::npos);
    CHECK(cfg.find("xmb_theme = \"6\"") != string::npos);
}

TEST_CASE("the theme's cfg keys wait with the theme: no assets bundle, no keys that point at missing icons") {
    Fixture fx;
    fx.options.retroarch = true;
    fx.site.files.erase(string(Site) + "/buildbot/assets.zip");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.out.said("the AutoBleem 2 theme waits"));
    string cfg = fx.tmp.readFile("stick/RetroArch/bin/retroarch.cfg");
    CHECK(cfg.find("xmb_theme = \"7\"") != string::npos); // the build's own, as an old installer leaves it
    CHECK(cfg.find("xmb_theme = \"6\"") == string::npos);
    CHECK(cfg.find("xmb_font") == string::npos);
}

TEST_CASE("the console's own RetroArch update lays the downloaded zip over the RetroArch that is on the stick") {
    Fixture fx;
    fx.options.packageFile.clear(); // no AutoBleem package in this run
    fx.options.retroarchZip = fx.tmp.at("site/retroarch-psc-v1.22.2-4.zip");
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "ELF old");
    fx.tmp.writeFile("stick/RetroArch/bin/VERSION", "v1.22.2-1\n");
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch.cfg", "video_smooth = \"true\"\nxmb_theme = \"8\"\n");
    fx.tmp.writeFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png", "stock setting");
    fx.tmp.writeFile("stick/RetroArch/bin/assets/mine.txt", "mine");
    fx.tmp.writeFile("stick/RetroArch/bin/cores/snes9x_libretro.so", "core");
    fx.tmp.writeFile("stick/RetroArch/bin/saves/Crash.srm", "srm");
    fx.tmp.writeFile("stick/Autobleem/bin/autobleem/autobleem-gui", "ELF launcher");

    StickInfo before = InstallerJob::inspect(fx.options);
    CHECK(InstallerJob::phasesFor(fx.options, before) == vector<string>{"RetroArch"});
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.out.phases == vector<string>{"RetroArch"});
    // the binary, the docs, the wallpaper
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF retroarch");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/fonts/OFL.txt") == "ofl");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/Retroarch themes/ab2-1280x720.png") == "wallpaper");
    // the theme over the assets: ours wins, the stock file kept once, the user's own file and the bundle stay
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png") == "ab2 setting");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png.prab2") == "stock setting");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/custom/font.ttf") == "red hat");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/mine.txt") == "mine");
    CHECK(fx.out.said("the AutoBleem 2 theme: 5 files"));
    // retroarch.cfg: the user's keys stay, the build's two, the save-state two and the theme's two are set, in that
    // order
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch.cfg") ==
          "video_smooth = \"true\"\nxmb_theme = \"6\"\nquit_on_close_content = \"2\"\n"
          "savestate_auto_save = \"true\"\nsavestate_thumbnail_enable = \"true\"\n"
          "xmb_font = \":/assets/xmb/custom/font.ttf\"\n");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/VERSION") == "v1.22.2-4\n");
    // nothing else of the stick is touched: no cores, libraries, apps, covers, no new AutoBleem
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/cores/snes9x_libretro.so") == "core");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/saves/Crash.srm") == "srm");
    CHECK(fx.tmp.readFile("stick/Autobleem/bin/autobleem/autobleem-gui") == "ELF launcher");
    CHECK_FALSE(fx.has("Autobleem/lib/modules/xpad.ko"));
    CHECK_FALSE(fx.has("RetroArch/bin/retroarch.new"));
    CHECK(fx.site.fetched.empty()); // the zip was downloaded by the launcher already

    // a second run keeps the first stock file, not our copy
    string again;
    REQUIRE_MESSAGE(fx.run(again), again);
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/assets/xmb/monochrome/png/setting.png.prab2") == "stock setting");
}

#ifndef _WIN32
static bool isExecutable(const string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IXUSR) != 0;
}

TEST_CASE("the unpacked package keeps the executable bit of its scripts and binaries") {
    Fixture fx;
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    for (const char *rel :
         {"Autobleem/bin/autobleem/autobleem-gui", "Autobleem/bin/emu/pcsx-ab", "Autobleem/rc/launch.sh",
          "Autobleem/start.sh", "Apps/abflashkit/abflashkit", "Extensions/pscbios/bin/psc/pscbios.so"})
        CHECK_MESSAGE(isExecutable(fx.root + "/" + rel), rel);
    // a plain file stays plain
    CHECK_FALSE(isExecutable(fx.root + "/Autobleem/bin/autobleem/config.ini"));
}

TEST_CASE("the console's RetroArch update keeps the executable bit of the retroarch binary") {
    Fixture fx;
    fx.options.packageFile.clear();
    const string zip = fx.tmp.at("site/retroarch-modes.zip");
    REQUIRE(ZipBuilder()
                .file("retroarch", "ELF retroarch", 0755)
                .file("VERSION", "v1.22.2-4\n")
                .file("theme/OFL.txt", "ofl")
                .write(zip));
    fx.options.retroarchZip = zip;
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "ELF old");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF retroarch");
    CHECK(isExecutable(fx.root + "/RetroArch/bin/retroarch"));
    CHECK_FALSE(isExecutable(fx.root + "/RetroArch/bin/fonts/OFL.txt"));
}
#endif

TEST_CASE("the console's RetroArch update waits for the assets bundle and stamps the version last") {
    Fixture fx;
    fx.options.packageFile.clear();
    fx.options.retroarchZip = fx.tmp.at("site/retroarch-psc-v1.22.2-4.zip");
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "ELF old");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.out.said("the AutoBleem 2 theme waits"));
    CHECK_FALSE(fx.has("RetroArch/bin/assets"));
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF retroarch");
    // no cfg yet: written, with the build's keys
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch.cfg").find("quit_on_close_content = \"2\"") != string::npos);
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/VERSION") == "v1.22.2-4\n");
}

TEST_CASE("the console's RetroArch update stamps a version the launcher reads as installed, from a key=value zip") {
    // RetroArch zips built up to v1.22.2-6 carry key=value lines only: the stick got that file as its stamp and
    // Software Update offered the same build again for ever
    Fixture fx;
    fx.options.packageFile.clear();
    fx.options.retroarchZip = fx.tmp.at("site/retroarch-psc-v1.22.2-6.zip");
    {
        ableem::ZipWriter zip;
        REQUIRE(zip.open(fx.options.retroarchZip));
        zip.addBytes("retroarch", "ELF retroarch");
        zip.addBytes("VERSION", "retroarch_version=v1.22.2\npsc_build=6\nbuild_date=2026-10-04T05:27:09Z\n"
                                "toolchain=autobleem-build-gcc6-glibc2.24\n");
        REQUIRE(zip.close());
    }
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "ELF old");
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(retroarch_version::installed(fx.tmp.at("stick")) == "v1.22.2-6");
    CHECK(InstallerJob::inspect(fx.options).retroarchVersion == "v1.22.2-6");
    // the first line is the version itself, the zip's own lines stay below it
    const string stamp = fx.tmp.readFile("stick/RetroArch/bin/VERSION");
    CHECK(stamp.compare(0, 10, "v1.22.2-6\n") == 0);
    CHECK(stamp.find("psc_build=6") != string::npos);

    // a zip with the tag as its first line is stamped as it is
    {
        ableem::ZipWriter zip;
        REQUIRE(zip.open(fx.options.retroarchZip));
        zip.addBytes("retroarch", "ELF retroarch");
        zip.addBytes("VERSION", "v1.22.2-7\nretroarch_version=v1.22.2\npsc_build=7\n");
        REQUIRE(zip.close());
    }
    string again;
    REQUIRE_MESSAGE(fx.run(again), again);
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/VERSION") == "v1.22.2-7\nretroarch_version=v1.22.2\npsc_build=7\n");
    CHECK(retroarch_version::installed(fx.tmp.at("stick")) == "v1.22.2-7");
}

TEST_CASE("the console's RetroArch update that fails leaves the old RetroArch as it was") {
    // no RetroArch on the stick: nothing is made
    {
        Fixture fx;
        fx.options.packageFile.clear();
        fx.options.retroarchZip = fx.tmp.at("site/retroarch-psc-v1.22.2-4.zip");
        string error;
        CHECK_FALSE(fx.run(error));
        CHECK(error.find("not on the stick") != string::npos);
        CHECK_FALSE(fx.has("RetroArch"));
    }
    // a zip that is not one, and one that is not there
    for (bool missing : {false, true}) {
        Fixture fx;
        fx.options.packageFile.clear();
        fx.tmp.writeFile("site/broken.zip", "this is not a zip");
        fx.options.retroarchZip = fx.tmp.at(missing ? "site/none.zip" : "site/broken.zip");
        fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "ELF old");
        fx.tmp.writeFile("stick/RetroArch/bin/VERSION", "v1.22.2-1\n");
        fx.tmp.writeFile("stick/RetroArch/bin/retroarch.cfg", "video_smooth = \"true\"\n");
        string error;
        CHECK_FALSE(fx.run(error));
        CHECK_FALSE(error.empty());
        CHECK(fx.out.said("RetroArch was not updated"));
        CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF old");
        CHECK(fx.tmp.readFile("stick/RetroArch/bin/VERSION") == "v1.22.2-1\n");
        CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch.cfg") == "video_smooth = \"true\"\n");
    }
}

TEST_CASE("the zip-only mode makes its scratch folder and its parents when they are not there yet") {
    Fixture fx;
    fx.options.packageFile.clear();
    fx.options.retroarchZip = fx.tmp.at("site/retroarch-psc-v1.22.2-4.zip");
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "ELF old");
    fx.options.scratchDir = fx.tmp.at("not/yet/there/scratch");
    REQUIRE_FALSE(DirEntry::exists(fx.options.scratchDir));
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF retroarch");
}

TEST_CASE("a scratch path that is a file (abupdate ran as /tmp/abupdate) fails at once, saying so") {
    Fixture fx;
    fx.options.packageFile.clear();
    fx.options.retroarchZip = fx.tmp.at("site/retroarch-psc-v1.22.2-4.zip");
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "ELF old");
    fx.tmp.writeFile("abupdate", "the program");
    fx.options.scratchDir = fx.tmp.at("abupdate");
    string error;
    CHECK_FALSE(fx.run(error));
    CHECK(error.find("scratch folder") != string::npos);
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF old");
}

//******************
// RetroArch alone (retroarchInstall / retroarchRemove / RetroArchRunner)
//******************
namespace {

RetroArchOptions raOptions(Fixture &fx, uint64_t freeBytes = 1ull << 40) {
    RetroArchOptions o;
    o.root = fx.root;
    o.repoUrl = Site;
    o.buildbotUrl = string(Site) + "/buildbot";
    o.scratchDir = fx.tmp.at("scratch");
    o.diskSpace = [freeBytes](const string &, uint64_t &free, uint64_t &total) {
        free = total = freeBytes;
        return true;
    };
    return o;
}

const InstallerJob::ShouldStop NeverStop = []() { return false; };

// what is on the stick the moment the install-binary phase starts
class BinaryWatcher : public Recorder {
public:
    explicit BinaryWatcher(const string &root) : root_(root) {}
    void onPhase(int index, int total, const string &title) override {
        Recorder::onPhase(index, total, title);
        if (title == "install-binary") {
            DirEntry::readFile(root_ + "/RetroArch/bin/retroarch", binary);
            for (const char *rel : {"cores/snes9x_libretro.so", "retroarch.cfg", "assets/assets-file.txt",
                                    "Retroarch themes/ab2-1280x720.png"})
                everythingElse = everythingElse && DirEntry::exists(root_ + "/RetroArch/bin/" + rel);
            versionStamped = DirEntry::exists(root_ + "/RetroArch/bin/VERSION");
        }
    }
    string binary = "(not seen)";
    bool everythingElse = true;
    bool versionStamped = true;

private:
    string root_;
};

// the download command over the fake site's files: "fetch [--continue] <url> <part>". Serves a file from the
// part's size on when asked to continue; `cut` (part of a url) serves half of what is left, sets `stop` and drops.
struct ResumeSite {
    FakeSite &site;
    string cut;
    atomic<bool> stop{false};
    map<string, size_t> served;
    vector<string> lines;
    mutex lock;
    int operator()(const string &line) {
        lock_guard<mutex> guard(lock);
        lines.push_back(line);
        istringstream in(line);
        string word, url, out;
        bool resume = false;
        while (in >> word) {
            if (word == "--continue")
                resume = true;
            else if (word.rfind("http", 0) == 0)
                url = word;
            else if (word != "fetch")
                out = word;
        }
        auto it = site.files.find(url);
        if (it == site.files.end())
            return 22;
        string all, have;
        DirEntry::readFile(it->second, all);
        if (resume)
            DirEntry::readFile(out, have);
        string rest = all.substr(have.size());
        bool drop = !cut.empty() && url.find(cut) != string::npos;
        if (drop)
            rest.resize(rest.size() / 2);
        ofstream(out, resume ? ios::binary | ios::app : ios::binary | ios::trunc) << rest;
        served[url] += rest.size();
        if (drop)
            stop = true;
        return drop ? 18 : 0;
    }
};

string progressText(Fixture &fx, const string &name) {
    return fx.tmp.readFile(name);
}

} // namespace

TEST_CASE("RetroArch alone: every key in order, and the binary is laid last") {
    Fixture fx;
    // an update: the old binary is still there while everything else lands
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch", "old retroarch");
    BinaryWatcher out(fx.root);
    RetroArchResult r = InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, out, NeverStop);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(out.phases == vector<string>{"check-space", "download-retroarch", "download-cores", "download-libs",
                                       "download-apps", "download-assets", "unpack", "merge-config",
                                       "install-binary"});
    CHECK(out.binary == "old retroarch"); // not touched until the last phase
    CHECK(out.everythingElse);            // cores, cfg, assets and the theme were all in place by then
    CHECK_FALSE(out.versionStamped);      // the stamp follows the binary
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF retroarch");
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/VERSION") == "v1.22.2-4\n");
    CHECK_FALSE(fx.has("RetroArch/bin/retroarch.new"));
    CHECK_FALSE(fx.has("RetroArch/bios/scph5501.bin")); // no --bios
    CHECK(fx.site.count("psc/bios") == 0);
    CHECK(fx.has("Autobleem/lib/modules/xpad.ko"));
    CHECK(fx.has("Apps/doom/run.sh"));
    CHECK_FALSE(DirEntry::exists(fx.tmp.at("scratch"))); // the downloads go with a successful run

    // the same with --bios: one more key, before unpack
    Fixture withBios;
    Recorder rec;
    r = InstallerJob::retroarchInstall(raOptions(withBios), true, withBios.site, rec, NeverStop);
    REQUIRE_MESSAGE(r.ok, r.error);
    REQUIRE(rec.phases.size() == 10);
    CHECK(rec.phases[6] == "download-bios");
    CHECK(rec.phases[9] == "install-binary");
    CHECK(withBios.tmp.readFile("stick/RetroArch/bios/scph5501.bin") == "bios!");
}

TEST_CASE("RetroArch alone after a remove re-merges retroarch.cfg and keeps the user's keys") {
    Fixture fx;
    fx.tmp.writeFile("stick/RetroArch/bin/retroarch.cfg", "video_smooth = \"true\"\nxmb_theme = \"3\"\n");
    Recorder out;
    RetroArchResult r = InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, out, NeverStop);
    REQUIRE_MESSAGE(r.ok, r.error);
    const string cfg = fx.tmp.readFile("stick/RetroArch/bin/retroarch.cfg");
    CHECK(cfg.find("video_smooth = \"true\"") != string::npos);
    CHECK(cfg.find("xmb_theme = \"6\"") != string::npos);
    CHECK(cfg.find("quit_on_close_content = \"2\"") != string::npos);
}

TEST_CASE("RetroArch remove deletes RetroArch and keeps what is the user's") {
    Fixture fx;
    Recorder out;
    REQUIRE(InstallerJob::retroarchInstall(raOptions(fx), true, fx.site, out, NeverStop).ok);
    for (const char *keep : {"RetroArch/roms/nes/game.nes", "RetroArch/bin/playlists/a.lpl",
                             "RetroArch/bin/thumbnails/x.png", "RetroArch/bin/saves/s.srm",
                             "RetroArch/bin/savestates/s.state", "RetroArch/bin/screenshots/p.png",
                             "RetroArch/bin/config/c.cfg", "RetroArch/bin/retroboot/r.txt", "Games/g/g.cue",
                             "Autobleem/lib/own.so", "Apps/own/run.sh"})
        fx.tmp.writeFile(string("stick/") + keep, "mine");
    fx.tmp.writeFile("stick/RetroArch/bin/cores.removing/old.so", "left by a cut-off run");
    REQUIRE(fx.has("RetroArch/bin/retroarch"));

    Recorder rm;
    RetroArchResult r = InstallerJob::retroarchRemove(raOptions(fx), false, rm, NeverStop);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(rm.phases == vector<string>{"remove-files"});
    for (const char *gone :
         {"retroarch", "VERSION", "cores", "cores.removing", "info", "assets", "database", "cheats", "overlays",
          "shaders", "downloads", "records", "Retroarch themes", "fonts/selawik-light.ttf", "fonts/OFL.txt"})
        CHECK_MESSAGE(!fx.has(string("RetroArch/bin/") + gone), gone);
    for (const char *kept :
         {"RetroArch/roms/nes/game.nes", "RetroArch/bin/playlists/a.lpl", "RetroArch/bin/thumbnails/x.png",
          "RetroArch/bin/saves/s.srm", "RetroArch/bin/savestates/s.state", "RetroArch/bin/screenshots/p.png",
          "RetroArch/bin/config/c.cfg", "RetroArch/bin/retroboot/r.txt", "RetroArch/bin/autoconfig/autoconfig-file.txt",
          "RetroArch/bin/retroarch.cfg", "Games/g/g.cue", "Autobleem/lib/own.so", "Autobleem/lib/modules/xpad.ko",
          "Apps/own/run.sh", "Apps/doom/run.sh", "RetroArch/bios/scph5501.bin"})
        CHECK_MESSAGE(fx.has(kept), kept);

    // --bios takes the BIOS files as well
    r = InstallerJob::retroarchRemove(raOptions(fx), true, rm, NeverStop);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK_FALSE(fx.has("RetroArch/bios/scph5501.bin"));
    CHECK(fx.has("RetroArch/roms/nes/game.nes"));
    CHECK(fx.has("RetroArch/bin/retroarch.cfg"));
}

TEST_CASE("RetroArch remove leaves the system's own RetroArch alone: apt-provided") {
    Fixture fx;
    fx.tmp.writeFile("usr/bin/retroarch", "apt");
    RetroArchOptions o = raOptions(fx);
    o.aptBinary = fx.tmp.at("usr/bin/retroarch");
    ProgressFile progress(fx.tmp.at("progress"), 0);
    CHECK(RetroArchRunner::run("remove", o, false, fx.site, progress, NeverStop) == 1);
    CHECK(progressText(fx, "progress") == "end|error|apt-provided");
    CHECK(fx.tmp.readFile("usr/bin/retroarch") == "apt");
}

TEST_CASE("RetroArch install: no room ends end|error|no-space before anything big is downloaded") {
    Fixture fx;
    ProgressFile progress(fx.tmp.at("progress"), 0);
    CHECK(RetroArchRunner::run("install", raOptions(fx, 1000), false, fx.site, progress, NeverStop) == 1);
    CHECK(progressText(fx, "progress") == "end|error|no-space");
    CHECK(fx.site.count(".tar.gz") == 0);
    CHECK(fx.site.count(".zip") == 0);
    CHECK_FALSE(fx.has("RetroArch/bin/retroarch"));
}

TEST_CASE("RetroArch install: a download that does not match its sha256 ends end|error|checksum") {
    Fixture fx;
    fx.tmp.writeFile("site/cores-psc-20260920.tar.gz", "tampered");
    ProgressFile progress(fx.tmp.at("progress"), 0);
    CHECK(RetroArchRunner::run("install", raOptions(fx), false, fx.site, progress, NeverStop) == 1);
    CHECK(progressText(fx, "progress") == "end|error|checksum");
    CHECK_FALSE(fx.has("RetroArch/bin/retroarch")); // the binary waits for everything else
    CHECK_FALSE(DirEntry::exists(fx.tmp.at("scratch/cores-psc-20260920.tar.gz.part"))); // a wrong part is no use
}

TEST_CASE("RetroArch install: a lost download and a missing network end with their own codes") {
    {
        Fixture fx;
        fx.site.files.erase(string(Site) + "/libs-psc-20260920.tar.gz");
        ProgressFile progress(fx.tmp.at("progress"), 0);
        CHECK(RetroArchRunner::run("install", raOptions(fx), false, fx.site, progress, NeverStop) == 1);
        CHECK(progressText(fx, "progress") == "end|error|download");
    }
    {
        struct NoNetwork : Downloader {
            bool fetch(const string &, const string &, const Progress &, string &error) override {
                error = string(ErrNoNetwork) + ": cannot be reached";
                return false;
            }
        } none;
        Fixture fx;
        ProgressFile progress(fx.tmp.at("progress"), 0);
        CHECK(RetroArchRunner::run("install", raOptions(fx), false, none, progress, NeverStop) == 1);
        CHECK(progressText(fx, "progress") == "end|error|no-network");
    }
    {
        Fixture fx;
        ProgressFile progress(fx.tmp.at("progress"), 0);
        CHECK(RetroArchRunner::run("install", raOptions(fx), false, fx.site, progress, []() { return true; }) == 3);
        CHECK(progressText(fx, "progress") == "end|stopped");
        CHECK(RetroArchRunner::run("upgrade", raOptions(fx), false, fx.site, progress, NeverStop) == 1);
        CHECK(progressText(fx, "progress") == "end|error|other");
    }
}

TEST_CASE("RetroArch install: a stop keeps the .part, the second install continues it and refetches nothing finished") {
    Fixture fx;
    ResumeSite net{fx.site};
    net.cut = "cores-psc";
    CommandDownloader dl("fetch %c %u %o", [&net](const string &line) { return net(line); });
    dl.setRetry(0, 0, 5);
    dl.setNetwork([]() { return true; }, [](unsigned) {});
    const string coresUrl = string(Site) + "/cores-psc-20260920.tar.gz";
    const string raUrl = string(Site) + "/retroarch-psc-v1.22.2-4.zip";
    const string coresFile = fx.tmp.at("site/cores-psc-20260920.tar.gz");

    ProgressFile progress(fx.tmp.at("progress"), 0);
    InstallerJob::ShouldStop stop = [&net]() { return net.stop.load(); };
    CHECK(RetroArchRunner::run("install", raOptions(fx), false, dl, progress, stop) == 3);
    CHECK(progressText(fx, "progress") == "end|stopped");
    const long long whole = DirEntry::fileSize(coresFile);
    const long long half = DirEntry::fileSize(fx.tmp.at("scratch/cores-psc-20260920.tar.gz.part"));
    CHECK(half > 0);
    CHECK(half < whole);
    CHECK(DirEntry::exists(fx.tmp.at("scratch/retroarch-psc-v1.22.2-4.zip"))); // the finished one stays
    CHECK_FALSE(fx.has("RetroArch/bin/retroarch"));

    // the second install: no cut, nothing stopped
    net.stop = false;
    net.cut.clear();
    net.served.clear();
    net.lines.clear();
    ProgressFile again(fx.tmp.at("progress"), 0);
    CHECK(RetroArchRunner::run("install", raOptions(fx), false, dl, again, stop) == 0);
    CHECK(progressText(fx, "progress") == "end|ok");
    CHECK(net.served.count(raUrl) == 0);                               // the finished zip was not fetched again
    CHECK(net.served[coresUrl] == static_cast<size_t>(whole - half)); // only the rest of the cut one
    bool continued = false;
    for (const string &l : net.lines)
        continued = continued || (l.find("--continue") != string::npos && l.find("cores-psc") != string::npos);
    CHECK(continued);
    CHECK(fx.tmp.readFile("stick/RetroArch/bin/retroarch") == "ELF retroarch");
    CHECK(fx.has("RetroArch/bin/cores/snes9x_libretro.so"));
}

//******************
// The progress contract: every phase reports real movement (unit, item, a total above 0)
//******************
namespace {

struct Report {
    string phase, unit, item;
    int itemIndex = 0, itemCount = 0;
    uint64_t done = 0, total = 0;
};

// every onProgress of a run, tagged with the phase and the item it came in
class ProgressRecorder : public Recorder {
public:
    vector<Report> reports;
    void onPhase(int i, int n, const string &title) override {
        Recorder::onPhase(i, n, title);
        cur = Report();
        cur.phase = title;
    }
    void onItem(const string &name, int index, int count, const string &unit) override {
        cur.item = name;
        cur.itemIndex = index;
        cur.itemCount = count;
        cur.unit = unit;
    }
    void onProgress(uint64_t done, uint64_t total) override {
        cur.done = done;
        cur.total = total;
        reports.push_back(cur);
    }
    // the reports of a phase that a strict progress file would have written (a total above 0)
    vector<Report> of(const string &phase) const {
        vector<Report> r;
        for (const Report &x : reports)
            if (x.phase == phase && x.total > 0)
                r.push_back(x);
        return r;
    }

private:
    Report cur;
};

size_t fileCountOf(const string &zip) {
    vector<string> names;
    ableem::ZipArchive::list(zip, names);
    size_t n = 0;
    for (const string &name : names)
        if (!name.empty() && name.back() != '/')
            n++;
    return n;
}

} // namespace

TEST_CASE("RetroArch install: every phase reports a unit, an item and a total above 0") {
    Fixture fx;
    ProgressRecorder rec;
    RetroArchResult r = InstallerJob::retroarchInstall(raOptions(fx), true, fx.site, rec, NeverStop);
    REQUIRE_MESSAGE(r.ok, r.error);
    for (const string &phase : rec.phases) {
        const vector<Report> got = rec.of(phase);
        INFO(phase);
        REQUIRE_FALSE(got.empty());
        for (const Report &x : got) {
            CHECK((x.unit == "bytes" || x.unit == "files" || x.unit == "items"));
            CHECK(x.done <= x.total);
        }
        CHECK(got.back().done == got.back().total); // each phase ends full
    }
}

TEST_CASE("RetroArch install: downloads are in bytes with the package's name") {
    Fixture fx;
    ProgressRecorder rec;
    REQUIRE(InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, rec, NeverStop).ok);
    const vector<Report> cores = rec.of("download-cores");
    REQUIRE_FALSE(cores.empty());
    CHECK(cores.back().unit == "bytes");
    CHECK(cores.back().item == "cores-psc-20260920.tar.gz");
    CHECK(cores.back().itemIndex == 1);
    CHECK(cores.back().itemCount == 1);
    CHECK(cores.back().total == static_cast<uint64_t>(DirEntry::fileSize(fx.tmp.at("site/cores-psc-20260920.tar.gz"))));
    CHECK(rec.of("download-retroarch").back().item == "retroarch-psc-v1.22.2-4.zip");
    // check-space counts the four catalogs
    CHECK(rec.of("check-space").back().total == 4);
}

TEST_CASE("RetroArch install: download-assets is one bar in bytes over the zips when the server tells the sizes") {
    Fixture fx;
    ProgressRecorder rec;
    REQUIRE(InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, rec, NeverStop).ok);
    const vector<Report> got = rec.of("download-assets");
    REQUIRE_FALSE(got.empty());
    uint64_t sum = 0;
    for (const char *b : {"assets", "autoconfig", "database-rdb", "database-cursors", "cheats", "overlays",
                          "shaders_glsl"})
        sum += static_cast<uint64_t>(DirEntry::fileSize(fx.tmp.at(string("site/") + b + ".zip")));
    for (const Report &x : got) {
        CHECK(x.unit == "bytes");
        CHECK(x.total == sum);
        CHECK(x.itemCount == 7);
    }
    CHECK(got.back().item == "shaders_glsl.zip");
    CHECK(got.back().itemIndex == 7);
    CHECK(got.back().done == sum);
}

TEST_CASE("RetroArch install: download-assets counts the zips when the server does not tell a size") {
    Fixture fx;
    fx.site.noHead = true;
    ProgressRecorder rec;
    REQUIRE(InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, rec, NeverStop).ok);
    const vector<Report> got = rec.of("download-assets");
    REQUIRE_FALSE(got.empty());
    for (const Report &x : got) {
        CHECK(x.unit == "items");
        CHECK(x.total == 7);
    }
    CHECK(got.back().done == 7);
}

TEST_CASE("RetroArch install: unpack counts a tar.gz in bytes read and a zip in files, never 1 of 1") {
    Fixture fx;
    ProgressRecorder rec;
    REQUIRE(InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, rec, NeverStop).ok);
    const vector<Report> got = rec.of("unpack");
    REQUIRE_FALSE(got.empty());
    bool zipSeen = false, tarSeen = false;
    for (const Report &x : got) {
        CHECK(x.itemCount == 11); // the RetroArch zip, three packs, seven bundles
        if (x.item == "retroarch-psc-v1.22.2-4.zip") {
            zipSeen = true;
            CHECK(x.unit == "files");
            CHECK(x.total == fileCountOf(fx.tmp.at("site/retroarch-psc-v1.22.2-4.zip")));
            CHECK(x.total > 1);
        }
        if (x.item == "cores-psc-20260920.tar.gz") {
            tarSeen = true;
            CHECK(x.unit == "bytes");
            CHECK(x.total == static_cast<uint64_t>(DirEntry::fileSize(fx.tmp.at("site/cores-psc-20260920.tar.gz"))));
        }
    }
    CHECK(zipSeen);
    CHECK(tarSeen);
    // the zip's bar moved file by file: some report in the middle
    bool middle = false;
    for (const Report &x : got)
        middle = middle || (x.item == "retroarch-psc-v1.22.2-4.zip" && x.done > 0 && x.done < x.total);
    CHECK(middle);
}

TEST_CASE("RetroArch install: merge-config and install-binary count their steps as items") {
    Fixture fx;
    ProgressRecorder rec;
    REQUIRE(InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, rec, NeverStop).ok);
    for (const char *phase : {"merge-config", "install-binary"}) {
        const vector<Report> got = rec.of(phase);
        REQUIRE_FALSE(got.empty());
        for (const Report &x : got) {
            CHECK(x.unit == "items");
            CHECK(x.total == 2);
        }
    }
    CHECK(rec.of("merge-config").front().item == "retroarch.cfg");
}

TEST_CASE("RetroArch install: the BIOS files are counted as files with their names") {
    Fixture fx;
    ProgressRecorder rec;
    REQUIRE(InstallerJob::retroarchInstall(raOptions(fx), true, fx.site, rec, NeverStop).ok);
    const vector<Report> got = rec.of("download-bios");
    REQUIRE_FALSE(got.empty());
    CHECK(got.front().unit == "files");
    CHECK(got.front().total == 2);
    CHECK(got.front().item == "scph5501.bin");
}

TEST_CASE("RetroArch remove reports items, and has no stop: a stop flag does not cut it short") {
    Fixture fx;
    Recorder out;
    REQUIRE(InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, out, NeverStop).ok);
    ProgressRecorder rm;
    const InstallerJob::ShouldStop always = []() { return true; };
    RetroArchResult r = InstallerJob::retroarchRemove(raOptions(fx), false, rm, always);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK_FALSE(r.stopped);
    CHECK_FALSE(fx.has("RetroArch/bin/retroarch"));
    CHECK_FALSE(fx.has("RetroArch/bin/cores"));
    const vector<Report> got = rm.of("remove-files");
    REQUIRE_FALSE(got.empty());
    for (const Report &x : got)
        CHECK(x.unit == "items");
    CHECK(got.back().done == got.back().total);
}

TEST_CASE("RetroArch install finishes a Remove that was cut off: the .removing folders go") {
    Fixture fx;
    fx.tmp.writeFile("stick/RetroArch/bin/cores.removing/old.so", "left by a cut-off remove");
    fx.tmp.writeFile("stick/RetroArch/bin/cheats.removing/a/b.cht", "left");
    fx.tmp.writeFile("stick/RetroArch/bios.removing/scph.bin", "left");
    Recorder out;
    RetroArchResult r = InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, out, NeverStop);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK_FALSE(fx.has("RetroArch/bin/cores.removing"));
    CHECK_FALSE(fx.has("RetroArch/bin/cheats.removing"));
    CHECK_FALSE(fx.has("RetroArch/bios.removing"));
    CHECK(fx.has("RetroArch/bin/cores/snes9x_libretro.so")); // the new cores are in
}

TEST_CASE("RetroArch remove after a cut-off remove: a folder renamed .removing is finished, a new one replaces it") {
    Fixture fx;
    Recorder out;
    REQUIRE(InstallerJob::retroarchInstall(raOptions(fx), false, fx.site, out, NeverStop).ok);
    // the power went while "cores" was being removed: it is renamed, its old copy half there, and a second
    // folder was renamed already
    fx.tmp.writeFile("stick/RetroArch/bin/cores.removing/half.so", "half");
    fx.tmp.writeFile("stick/RetroArch/bin/info.removing/x.info", "x");
    Recorder rm;
    RetroArchResult r = InstallerJob::retroarchRemove(raOptions(fx), false, rm, NeverStop);
    REQUIRE_MESSAGE(r.ok, r.error);
    for (const char *gone : {"cores", "cores.removing", "info", "info.removing", "cheats", "assets"})
        CHECK_MESSAGE(!fx.has(string("RetroArch/bin/") + gone), gone);
}

TEST_CASE("ProgressFile (strict): no line while the total is 0, the 8-field line from the first real total") {
    TempDir tmp("progress");
    ProgressFile progress(tmp.at("p"), 0);
    progress.setStrict(true);
    progress.onPhase(2, 9, "download-cores");
    progress.onProgress(0, 0);
    CHECK_FALSE(DirEntry::exists(tmp.at("p")));
    progress.onItem("cores-psc-20261003.tar.gz", 1, 1, "bytes");
    progress.onProgress(0, 4096);
    CHECK(tmp.readFile("p") == "phase 2/9|download-cores|0|4096|bytes|cores-psc-20261003.tar.gz|1|1");
    CHECK(ProgressFile::phaseLine(3, 9, "download-libs", 1024, 4096) == "phase 3/9|download-libs|1024|4096");
    // a '|' in a name cannot break the line
    progress.onItem("a|b", 1, 2, "files");
    progress.onProgress(1, 5);
    CHECK(tmp.readFile("p") == "phase 2/9|download-cores|1|5|files|a_b|1|2");
}

TEST_CASE("ProgressFile (not strict): a job without items still writes the old 4-field line") {
    TempDir tmp("progress");
    ProgressFile progress(tmp.at("p"), 0);
    progress.onPhase(1, 3, "Unpacking AutoBleem");
    CHECK(tmp.readFile("p") == "phase 1/3|Unpacking AutoBleem|0|0");
}

TEST_CASE("CommandDownloader::contentLength: the %c slot takes --head, a template without it cannot ask") {
    TempDir tmp("head");
    string seen;
    CommandDownloader dl("fetch %c -o \"%o\" \"%u\"", [&](const string &line) {
        seen = line;
        ofstream(tmp.at("size.txt")) << "6295495\n";
        return 0;
    });
    uint64_t size = 0;
    CHECK(dl.contentLength("http://site/assets.zip", tmp.at("size.txt"), size));
    CHECK(size == 6295495);
    CHECK(seen.find("--head") != string::npos);
    CommandDownloader plain("curl -o \"%o\" \"%u\"", [](const string &) { return 0; });
    CHECK_FALSE(plain.contentLength("http://site/assets.zip", tmp.at("size.txt"), size));
}

#ifndef _WIN32
// adversarial: removing RetroBoot's tree must never reach outside it
TEST_CASE("removeRetroBoot does not delete what a symlink inside RetroBoot's tree points to") {
    Fixture fx;
    retroBootStick(fx, false);
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    fx.tmp.writeFile("stick/RetroArch/roms/snes/game.sfc", "rom");
    REQUIRE(symlink((fx.root + "/RetroArch/roms").c_str(), (fx.root + "/RetroArch/bin/retroboot/emulationstation/roms").c_str()) == 0);
    LegacyLayout::removeRetroBoot(fx.root, [](const string &) {});
    CHECK_FALSE(fx.has("RetroArch/bin/retroboot"));
    CHECK(fx.has("RetroArch/roms/snes/game.sfc"));
}

TEST_CASE("removeRetroBoot does not delete through a symlinked retroboot folder") {
    Fixture fx;
    retroBootStick(fx, false);
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    fx.tmp.writeFile("elsewhere/keep/me.txt", "mine");
    DirEntry::removeDirAndContents(fx.root + "/RetroArch/bin/retroboot");
    fx.tmp.makeSubDir("elsewhere/lib");
    REQUIRE(symlink(fx.tmp.at("elsewhere").c_str(), (fx.root + "/RetroArch/bin/retroboot").c_str()) == 0);
    LegacyLayout::removeRetroBoot(fx.root, [](const string &) {});
    CHECK(fx.tmp.readFile("elsewhere/keep/me.txt") == "mine");
}
#endif

TEST_CASE("an apps folder holding only a hidden file is not removed") {
    Fixture fx;
    retroBootStick(fx, false);
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    fx.tmp.writeFile("stick/RetroArch/bin/apps/mine/.config", "precious");
    LegacyLayout::removeRetroBoot(fx.root, [](const string &) {});
    CHECK(fx.has("RetroArch/bin/apps/mine/.config"));
}

#ifndef _WIN32
TEST_CASE("a removal that fails half way does not say removed") {
    if (geteuid() == 0)
        return;
    Fixture fx;
    retroBootStick(fx, false);
    string error;
    REQUIRE_MESSAGE(fx.run(error), error);
    const string locked = fx.root + "/RetroArch/bin/retroboot/emulationstation/.emulationstation";
    chmod(locked.c_str(), 0555);
    vector<string> said;
    LegacyLayout::removeRetroBoot(fx.root, [&](const string &l) { said.push_back(l); });
    chmod(locked.c_str(), 0755);
    bool claimed = false;
    for (const string &l : said)
        if (l.find("removed RetroArch/bin/retroboot") != string::npos)
            claimed = true;
    CHECK((!claimed || !fx.has("RetroArch/bin/retroboot")));
}
#endif
