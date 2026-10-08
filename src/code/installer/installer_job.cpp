#include "installer/installer_job.h"
#include "installer/install_job_base.h"
#include "installer/legacy_layout.h"
#include "installer/local_bundle.h"
#include "core/services/default_theme.h"
#include "core/services/extension_catalog.h"
#include "core/services/processor_catalog.h"
#include "core/services/retroarch_version.h"
#include "core/services/system.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/log.h>
#include <ableem/engine/sha256.h>
#include <ableem/engine/strings.h>
#include <ableem/engine/tar_archive.h>
#include <ableem/engine/update_catalog.h>
#include <ableem/engine/zip_archive.h>

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace std;
using ableem::DirEntry;
using ableem::PackCatalog;
using ableem::PscRetroArchCatalog;
using ableem::ReleaseCatalog;
using ableem::Sha256;
using ableem::TarArchive;
using ableem::TarEntry;
using ableem::UpdateFile;
using ableem::ZipArchive;

namespace {

const char *const VersionFile = "VERSION";
const char *const LauncherBinary = "Autobleem/bin/autobleem/autobleem-gui";
const char *const ConfigIni = "Autobleem/bin/autobleem/config.ini";
const char *const SamplesMarker = "System/samples.txt";

struct CoverDb {
    const char *name;
    const char *file;
    bool InstallOptions::*selected;
};
const CoverDb Covers[] = {
    {"Japan", "coversJ.db", &InstallOptions::coversJapan},
    {"USA", "coversU.db", &InstallOptions::coversUsa},
    {"PAL", "coversP.db", &InstallOptions::coversPal},
};

// libretro's bundles and where each unpacks under RetroArch/bin (the Pi installer's list, minus info -
// the cores pack carries the info files)
struct Bundle {
    const char *name;
    const char *dest;
};
const Bundle Bundles[] = {
    {"assets", "assets"},
    {"autoconfig", "autoconfig"},
    {"database-rdb", "database/rdb"},
    {"database-cursors", "database/cursors"},
    {"cheats", "cheats"},
    {"overlays", "overlays"},
    {"shaders_glsl", "shaders"},
};

// "PlayStation only" runs the BIOS step and nothing else
bool biosOnly(const InstallOptions &opt) { return opt.bios && opt.ps1BiosOnly; }

// the stick carries the package's version already: prepare, unpack and UpdateRoms have nothing to do
bool skipsUnpack(const InstallOptions &opt, const StickInfo &info) {
    return !opt.force && info.installed && !info.legacyLayout && !info.packageVersion.empty() &&
           info.installedVersion == info.packageVersion;
}

//******************
// Run
//******************
// one install: the options, the stick, and the helpers every phase uses
class Run : public InstallJobBase {
public:
    Run(const InstallOptions &options, const StickInfo &info, Downloader &downloader, InstallListener &listener,
        const InstallerJob::ShouldStop &shouldStop)
        : InstallJobBase(downloader, listener, shouldStop, options.repoUrl,
                         options.scratchDir.empty() ? options.root + "/System/Install" : options.scratchDir),
          opt(options), info(info), root(options.root) {}

    bool go(string &error) {
        phases = InstallerJob::phasesFor(opt, info);
        if (!DirEntry::createDirs(scratch) || !DirEntry::isDirectory(scratch)) {
            error = "cannot make the scratch folder " + scratch;
            say("  " + error);
            return false;
        }
        // the run's own record on the stick, so a report from a tester can be read afterwards
        if (DirEntry::isDirectory(root) && DirEntry::createDirs(at("System/Logs"))) {
            logPath = at("System/Logs/installer.log");
            ofstream(logPath, ios::binary | ios::trunc)
                << "AutoBleemInstaller: " << opt.packageFile << " (" << info.packageVersion << ") onto " << root
                << (info.installed ? ", an update of " + info.installedVersion : string(", a fresh install")) << "\n";
        }
        if (!opt.retroarchZip.empty()) {
            const bool zipped = retroarchZip(error);
            if (!zipped)
                say("  RetroArch was not updated: " + error);
            return zipped;
        }
        bool ok = true;
        if (!biosOnly(opt)) {
            ok = package(error);
            // the stick carries this very version: nothing of AutoBleem itself is unpacked again
            if (ok && skipsUnpack(opt, info)) {
                say("  the stick already has " + info.installedVersion +
                    ", the same version as the package - not unpacking it again (force reinstalls)");
                phases = InstallerJob::phasesFor(opt, info);
                ok = covers(error);
            } else {
                ok = ok && prepare(error) && legacy(error) && unpack(error) && updateRoms(error) && covers(error);
            }
            if (ok && opt.retroarch)
                ok = retroarch(error);
        }
        if (ok && opt.bios && (biosOnly(opt) || opt.retroarch || info.hasRetroArch))
            ok = bios(error);
        if (ok && opt.samples)
            ok = samples(error);
        if (ok)
            finish();
        if (DirEntry::isDirectory(scratch) && opt.scratchDir.empty())
            DirEntry::removeDirAndContents(scratch);
        return ok;
    }

private:
    string at(const string &rel) const { return root + "/" + rel; }

    //******************
    // 1. the package
    //******************
    bool package(string &error) {
        phase("Getting the package");
        if (opt.packageFile.empty() && !opt.channel.empty() && !fromChannel(error))
            return false;
        if (opt.packageFile.empty() || !DirEntry::exists(opt.packageFile)) {
            error = "No package: pick a channel to download it from";
            return false;
        }
        vector<TarEntry> entries;
        if (!TarArchive::list(opt.packageFile, entries, error))
            return false;
        bool launcher = false;
        for (const TarEntry &e : entries) {
            if (e.name == LauncherBinary)
                launcher = true;
            if (e.isDir && e.name.rfind("Themes/", 0) == 0 && e.name.find('/', 7) == string::npos)
                shippedThemes.push_back(e.name.substr(7));
            // the extensions the package ships (PSC-Bios, the Store): an update replaces those folders whole
            const size_t slash = e.name.rfind("Extensions/", 0) == 0 ? e.name.find('/', 11) : string::npos;
            if (slash != string::npos && slash > 11) {
                const string name = e.name.substr(11, slash - 11);
                if (find(shippedExtensions.begin(), shippedExtensions.end(), name) == shippedExtensions.end())
                    shippedExtensions.push_back(name);
            }
        }
        if (!launcher) {
            error = opt.packageFile + " is not an AutoBleem package (no " + string(LauncherBinary) + ")";
            return false;
        }
        say("  " + opt.packageFile + ": " + info.packageVersion + ", " + to_string(entries.size()) + " entries");
        return true;
    }

    // the channel's release: its stick package downloaded (sha256-checked) into the scratch folder, its
    // version read from the package, its UpdateRoms remembered for the UpdateRoms phase
    bool fromChannel(string &error) {
        ChannelRelease rel;
        if (!InstallerJob::channelRelease(repoUrl, opt.channel,
                                          opt.channelIndexes.empty() ? InstallerJob::channelLists(opt.channel)
                                                                     : opt.channelIndexes,
                                          dl, scratch, rel, error))
            return false;
        say("  the " + opt.channel + " channel: AutoBleem " + rel.version);
        const string file = scratch + "/" + rel.package.name;
        if (!downloadVerified(rel.package, file, error))
            return false;
        string data;
        if (!TarArchive::readEntry(file, VersionFile, data, error)) {
            error = rel.package.name + " has no VERSION - " + error;
            return false;
        }
        opt.packageFile = file;
        info.packageVersion = firstLine(data);
        channelUpdateRoms = rel.updateRoms;
        return true;
    }

    //******************
    // 2. the stick
    //******************
    // an update: what the package ships goes, everything of the user's stays
    bool prepare(string &error) {
        phase(info.installed ? "Preparing the update" : "Preparing the stick");
        if (!DirEntry::isDirectory(root)) {
            error = "No such drive: " + root;
            return false;
        }
        if (stopped(error))
            return false;
        // the default theme's folder before the install touches the Themes, for the one-time switch (unpack)
        hadDefaultTheme = DirEntry::isDirectory(at(string("Themes/") + DefaultTheme::Name));
        if (info.installed) {
            say("  AutoBleem " + (info.installedVersion.empty() ? string("(unknown version)") : info.installedVersion) +
                " is on the stick - updating to " + info.packageVersion);
            savedConfig = readText(at(ConfigIni));
            // Apps/pscbios is where PSC-Bios was an App until 2026-09-24; it is an extension the package ships
            // since (Extensions/pscbios) - an update removes both, and the package brings the extension
            for (const char *dir : {"Autobleem/bin/autobleem", "Autobleem/bin/emu", "Autobleem/bin/emunxt",
                                    "Autobleem/rc", "Apps/pscbios", "Apps/abflashkit", "Extensions/pscbios", "Docs"}) {
                if (DirEntry::isDirectory(at(dir)))
                    DirEntry::removeDirAndContents(at(dir));
            }
            // what the package ships of Extensions/ goes first, so nothing of the old version is left in it; an
            // extension the user put there themselves (one the package has not) stays
            for (const string &extension : shippedExtensions)
                if (DirEntry::isDirectory(at("Extensions/" + extension)))
                    DirEntry::removeDirAndContents(at("Extensions/" + extension));
            for (const string &theme : shippedThemes)
                if (DirEntry::isDirectory(at("Themes/" + theme)))
                    DirEntry::removeDirAndContents(at("Themes/" + theme));
            DirEntry::removeFile(at("Autobleem/lib/libs.tar.gz"));
            DirEntry::removeFile(at("Autobleem/start.sh"));
            DirEntry::removeFile(at(VersionFile));
        } else {
            say("  A fresh install of " + info.packageVersion + " onto " + root);
        }
        return true;
    }

    //******************
    // 2b. an old stick's layout
    //******************
    bool legacy(string &error) {
        if (!info.legacyLayout)
            return true;
        phase("Bringing the old layout up to date");
        if (stopped(error))
            return false;
        say("  an AutoBleem 1.0 / NG stick: RetroArch, the ROMs and the themes move to where the launcher looks now -");
        say("  games, save states, memory cards, ROMs and RetroArch's saves stay");
        if (!LegacyLayout::migrate(root, [this](const string &line) { say(line); }, error))
            return false;
        say("  the playlists are rebuilt by the launcher's first scan (or UpdateRoms, for box art from the PC)");
        return true;
    }

    //******************
    // 3. AutoBleem itself
    //******************
    bool unpack(string &error) {
        phase("Unpacking AutoBleem");
        if (stopped(error))
            return false;
        if (!untar(opt.packageFile, root, error))
            return false;
        if (!savedConfig.empty()) {
            writeText(at(ConfigIni), savedConfig);
            say("  config.ini kept as it was");
        }
        // the package's default theme on a stick that did not have it (an update from an older version, an
        // AutoBleem 1.0 conversion, a fresh install): the theme setting switches to it once; a stick that had the
        // folder keeps the user's choice
        const bool shipsDefaultTheme =
            find(shippedThemes.begin(), shippedThemes.end(), DefaultTheme::Name) != shippedThemes.end();
        if (DefaultTheme::switchesTo(shipsDefaultTheme, hadDefaultTheme) &&
            setIniValue(at(ConfigIni), "theme", DefaultTheme::Name))
            say(string("  theme set to ") + DefaultTheme::Name);
        // the PS1 emulator every install lands on (the owner's rule, 2026-09-21): pcsx-abnxt, whatever the
        // stick's config.ini said before
        if (setIniValue(at(ConfigIni), "emulator", "pcsx-abnxt"))
            say("  PS1 emulator set to pcsx-abnxt");
        for (const char *dir : {"Games", "Games/!SaveStates", "Games/!MemCards", "System", "System/Databases",
                                "System/Logs", "Apps", "Themes"})
            DirEntry::createDirs(at(dir));
        // the scanner processors' folder, with a README saying what goes there (docs/scanner-processors-plan.md)
        ProcessorCatalog::ensureFolder(at("System/Processors"));
        // and the extensions' (docs/extensions-plan.md), installed by hand
        ExtensionCatalog::ensureFolder(at("Extensions"));
        if (opt.retroarch || info.hasRetroArch)
            romFolders();
        say("  done");
        return true;
    }

    // RetroArch/roms: an AutoBleem 1.0 / RetroBoot stick's ES-style folders (nes, snes, ...) renamed to the
    // RetroArch database names the launcher's scan reads, then one folder per system made where missing
    // (the package's platform/roms_systems.cfg, the list the Pi and Windows installers use) - on an update
    // too, which fixes a stick an older installer left that way and touches nothing already there
    void romFolders() {
        const string roms = at("RetroArch/roms");
        DirEntry::createDirs(roms);
        const int converted = LegacyLayout::convertRomFolders(roms, [this](const string &l) { say(l); });
        if (converted > 0)
            say("  " + to_string(converted) + " old ROM folder(s) renamed for the launcher's scan");
        createRomFolders(at("Autobleem/bin/autobleem/platform/roms_systems.cfg"), roms);
    }

    //******************
    // 3b. UpdateRoms
    //******************
    // the PC-side ROM scanner, into <stick>/UpdateRoms/ - the console has no network, so the ROMs' box art
    // and names come from a PC run of it. The copy that came with this installer (an UpdateRoms/ folder next
    // to it, from the same release as the package) first; else the site's, from the release this package
    // belongs to. The stick's copy is replaced only once a new one is complete. Not having it is no reason
    // to stop.
    bool updateRoms(string &error) {
        phase("UpdateRoms");
        if (stopped(error))
            return false;
        const string bundled = opt.packageFile.substr(0, opt.packageFile.find_last_of("/\\") + 1) + "UpdateRoms";
        if (DirEntry::exists(bundled + "/UpdateRoms.exe")) {
            if (placeUpdateRoms(bundled))
                say("  UpdateRoms from this installer's folder");
            else
                say("  could not copy UpdateRoms from this installer's folder - going on without it");
            return true;
        }
        const UpdateFile *file = channelUpdateRoms.name.empty() ? nullptr : &channelUpdateRoms;
        ReleaseCatalog unstable, stable;
        string text, why;
        if (!file && fetchCatalog("releases/unstable.json", text, why))
            unstable.parse(text);
        if (!file && fetchCatalog("releases/latest.json", text, why))
            stable.parse(text);
        // the release whose console package this is, else the pre-release, else the stable one
        const string mine = "autobleem-psc-" + info.packageVersion + ".tar.gz";
        for (const ReleaseCatalog *r : {&unstable, &stable}) {
            const UpdateFile *fs = r->fileFor("psc-fs");
            if (!file && fs && fs->name == mine && r->fileFor("updateroms")) {
                file = r->fileFor("updateroms");
                break;
            }
        }
        if (!file)
            file = unstable.fileFor("updateroms") ? unstable.fileFor("updateroms") : stable.fileFor("updateroms");
        if (!file) {
            say("  no UpdateRoms package on the site - skipped (copy UpdateRoms/ onto the stick by hand for box art)");
            return true;
        }
        const string zip = scratch + "/" + file->name;
        if (!downloadVerified(*file, zip, error)) {
            say("  could not fetch " + file->name + ": " + error + " - going on without it");
            error.clear();
            return true;
        }
        const string unpacked = scratch + "/updateroms";
        DirEntry::removeDirAndContents(unpacked);
        if (!ZipArchive::extract(zip, unpacked) || !placeUpdateRoms(unpacked + "/UpdateRoms"))
            say("  could not unpack " + file->name + " - the stick keeps what it had");
        else
            say("  " + file->name);
        DirEntry::removeDirAndContents(unpacked);
        DirEntry::removeFile(zip);
        return true;
    }

    // <src>'s files (UpdateRoms.exe, README.txt) become the stick's UpdateRoms/, replacing what was there -
    // only when src really holds the program, so a broken source never leaves the stick without one
    bool placeUpdateRoms(const string &src) {
        if (!DirEntry::exists(src + "/UpdateRoms.exe"))
            return false;
        const string dest = at("UpdateRoms");
        const string staged = at("UpdateRoms.new");
        DirEntry::removeDirAndContents(staged);
        DirEntry::createDirs(staged);
        for (const DirEntry &e : DirEntry::diru_FilesOnly(src))
            if (!DirEntry::copyFile(src + "/" + e.name, staged + "/" + e.name)) {
                DirEntry::removeDirAndContents(staged);
                return false;
            }
        if (DirEntry::isDirectory(dest))
            DirEntry::removeDirAndContents(dest);
        return DirEntry::renameFile(staged, dest);
    }

    //******************
    // 4. the cover databases
    //******************
    bool covers(string &error) {
        bool any = false;
        for (const CoverDb &c : Covers)
            any = any || opt.*c.selected;
        if (!any)
            return true;
        phase("Cover databases");
        DirEntry::createDirs(at("Autobleem/bin/db"));
        for (const CoverDb &c : Covers) {
            if (!(opt.*c.selected))
                continue;
            if (stopped(error))
                return false;
            string sidecar;
            UpdateFile file;
            file.name = c.file;
            file.url = opt.repoUrl + "/db/" + c.file;
            if (dl.fetchText(file.url + ".sha256", scratch + "/sidecar.txt", sidecar, error))
                file.sha256 = sidecarHash(sidecar);
            else
                say("  (no checksum published for " + string(c.file) + " - " + error + ")");
            say("  " + string(c.name));
            if (!downloadVerified(file, at(string("Autobleem/bin/db/") + c.file), error))
                return false;
        }
        return true;
    }

    //******************
    // 5. RetroArch
    //******************
    bool retroarch(string &error) {
        const string bin = at("RetroArch/bin");
        // RetroArch itself, and the theme it comes with
        phase("RetroArch");
        if (stopped(error))
            return false;
        string text;
        PscRetroArchCatalog ra;
        if (!fetchCatalog("psc/retroarch/latest.json", text, error) || !ra.parse(text)) {
            if (error.empty())
                error = "psc/retroarch/latest.json is not what was expected";
            return false;
        }
        if (info.hasRetroArch && info.retroarchVersion == ra.version) {
            say("  RetroArch " + ra.version + " is on the stick already");
        } else {
            say("  RetroArch " + ra.version);
            string zip;
            if (!obtain(ra.zip, zip, error))
                return false;
            if (!installZip(zip, ra.zip.name, error))
                return false;
            discard(zip);
        }
        if (!writeRetroArchCfg(error))
            return false;

        // the cores, with their info files
        phase("RetroArch cores");
        if (!pack("psc/cores/latest.json", "cores", bin, error, TarArchive::Filter()))
            return false;
        // the libraries the apps need, and the xpad module
        phase("Runtime libraries");
        if (!pack("psc/libs/latest.json", "libs", at("Autobleem/lib"), error,
                  [](const TarEntry &e) { return e.name != "libs.json"; }))
            return false;
        // the apps
        phase("Apps");
        if (!pack("psc/apps/latest.json", "apps", root, error, [](const TarEntry &e) { return e.name != "apps.json"; }))
            return false;
        // libretro's bundles
        phase("RetroArch assets");
        bool assetsReady = true;
        for (const Bundle &b : Bundles) {
            if (stopped(error))
                return false;
            const string dest = bin + "/" + b.dest;
            if (DirEntry::isDirectory(dest) && !DirEntry::diru(dest).empty()) {
                say("  " + string(b.name) + ": already there");
                continue;
            }
            say("  " + string(b.name));
            UpdateFile file; // libretro publishes no checksum: a bundle's own manifest carries one
            file.name = string(b.name) + ".zip";
            file.url = opt.buildbotUrl + "/" + file.name;
            string zip;
            if (!obtain(file, zip, error)) {
                say("  could not download " + file.name + ": " + error + " - going on without it");
                error.clear();
                if (string(b.name) == "assets")
                    assetsReady = false;
                continue;
            }
            if (!ZipArchive::extract(zip, dest)) {
                say("  could not unpack " + file.name + " - going on without it");
                if (string(b.name) == "assets")
                    assetsReady = false;
            }
            error.clear();
            discard(zip);
        }
        return applyTheme(assetsReady, error) && stampVersion(error);
    }

    // a RetroArch zip (the site's retroarch-psc-<v>.zip) unpacked and laid over RetroArch/bin: the binary, the
    // docs and the theme's loose files; the theme's assets tree, its cfg keys and the VERSION wait for
    // applyTheme and stampVersion. Every file goes in under a temporary name and is renamed over the old one, so
    // a write that fails half way leaves the old file; the zip is unpacked before anything on the stick is
    // touched. The console's own update (retroarchZip) and the PC installer share this.
    bool installZip(const string &zip, const string &zipName, string &error) {
        const string bin = at("RetroArch/bin");
        const string unpacked = scratch + "/retroarch";
        DirEntry::removeDirAndContents(unpacked);
        if (!ZipArchive::extract(zip, unpacked)) {
            if (error.empty())
                error = "cannot unpack " + zipName;
            DirEntry::removeDirAndContents(unpacked);
            return false;
        }
        for (const char *dir : {"", "Retroarch themes", "fonts", "playlists", "saves", "savestates", "screenshots",
                                "config", "logs", "thumbnails", "downloads", "records", "cores", "info"})
            DirEntry::createDirs(bin + (*dir ? string("/") + dir : ""));
        DirEntry::createDirs(at("RetroArch/bios"));
        DirEntry::createDirs(at("RetroArch/roms"));
        struct Place {
            const char *from;
            const char *to;
        };
        for (const Place &p :
             {Place{"retroarch", "retroarch"}, Place{"theme/Autobleem2.png", "Retroarch themes/Autobleem2.png"},
              Place{"theme/selawik-light.ttf", "fonts/selawik-light.ttf"}, Place{"theme/OFL.txt", "fonts/OFL.txt"},
              Place{"theme/ab2-1280x720.png", "Retroarch themes/ab2-1280x720.png"}}) {
            if (!DirEntry::exists(unpacked + "/" + p.from))
                continue;
            const string dst = bin + "/" + p.to, tmpName = dst + ".new";
            DirEntry::removeFile(tmpName);
            if (!DirEntry::copyFile(unpacked + "/" + p.from, tmpName) || !DirEntry::replaceFile(tmpName, dst)) {
                DirEntry::removeFile(tmpName);
                error = "cannot write RetroArch/bin/" + string(p.to);
                DirEntry::removeDirAndContents(unpacked);
                return false;
            }
        }
        newVersion = readText(unpacked + "/VERSION");
        themeCfg = readText(unpacked + "/theme/retroarch-psc.cfg");
        ab2Cfg = readText(unpacked + "/theme/ab2-theme.cfg");     // applied once the theme files have landed
        statesCfg = readText(unpacked + "/theme/ab2-states.cfg"); // the save-state keys: no files to wait for
        // the theme's assets tree waits for the bundles: a folder of ours under assets/ would make a fresh
        // install skip libretro's own assets bundle as "already there"
        themeAssets = scratch + "/ra-theme-assets";
        DirEntry::removeDirAndContents(themeAssets);
        if (DirEntry::isDirectory(unpacked + "/theme/assets"))
            DirEntry::renameFile(unpacked + "/theme/assets", themeAssets);
        DirEntry::removeDirAndContents(unpacked);
        newBinary = true;
        return true;
    }

    // the AutoBleem 2 look over RetroArch's assets (a new RetroArch build brings it), then the theme's cfg keys
    bool applyTheme(bool assetsReady, string &error) {
        if (themeAssets.empty())
            return true;
        if (assetsReady) {
            int count = 0;
            if (!applyThemeTree(themeAssets, at("RetroArch/bin/assets"), count, error))
                return false;
            say("  the AutoBleem 2 theme: " + to_string(count) + " files");
            if (!ab2Cfg.empty() && !setCfgKeys(ab2Cfg, error))
                return false;
        } else {
            // putting our folders in would make the next run take the assets bundle for done
            say("  the AutoBleem 2 theme waits: RetroArch's assets are not on the stick yet");
        }
        DirEntry::removeDirAndContents(themeAssets);
        return true;
    }

    // RetroArch/bin/VERSION, last: a run that failed before it is offered again, not taken for done
    bool stampVersion(string &error) {
        if (newVersion.empty())
            return true;
        // the stamp's first line is the version as the site lists it ("v1.22.2-6"): a zip built before it had that
        // line carries only key=value lines (retroarch_version=, psc_build=), which are kept below it
        string stamp = newVersion;
        const string version = retroarch_version::parse(newVersion);
        if (!version.empty() && firstLine(newVersion) != version)
            stamp = version + "\n" + newVersion;
        const string file = at("RetroArch/bin/VERSION");
        if (!writeText(file, stamp)) {
            error = "cannot write RetroArch/bin/VERSION";
            return false;
        }
        return true;
    }

    // the console's own update: the RetroArch zip the launcher downloaded, over the RetroArch that is on the
    // stick. The same result as retroarch() on such a stick - the binary and its docs, the theme over
    // RetroArch/bin/assets (stock files kept once as .prab2), the wallpaper, retroarch.cfg's merge, the theme's
    // cfg keys after its files, the VERSION stamp - without the cores, libraries, apps and bundles.
    bool retroarchZip(string &error) {
        phase("RetroArch");
        if (stopped(error))
            return false;
        if (!info.hasRetroArch || !DirEntry::isDirectory(at("RetroArch/bin"))) {
            error = "RetroArch is not on the stick - the PC installer puts it there";
            return false;
        }
        if (opt.retroarchZip.empty() || !DirEntry::exists(opt.retroarchZip)) {
            error = "No RetroArch zip: " + opt.retroarchZip;
            return false;
        }
        say("  " + opt.retroarchZip);
        if (!installZip(opt.retroarchZip, opt.retroarchZip, error) || !writeRetroArchCfg(error))
            return false;
        const string assets = at("RetroArch/bin/assets");
        const bool assetsReady = DirEntry::isDirectory(assets) && !DirEntry::diru(assets).empty();
        return applyTheme(assetsReady, error) && stampVersion(error);
    }

    // `from` copied over `to`, folder by folder. A stock file of RetroArch's that this overwrites is kept once
    // as <name>.prab2 (a later run sees the .prab2 and keeps the first one - the stock file, not our copy)
    bool applyThemeTree(const string &from, const string &to, int &count, string &error) {
        DirEntry::createDirs(to);
        for (const DirEntry &e : DirEntry::diru(from)) {
            const string src = from + "/" + e.name, dst = to + "/" + e.name;
            if (e.isDir) {
                if (!applyThemeTree(src, dst, count, error))
                    return false;
                continue;
            }
            const string backup = dst + ".prab2";
            if (DirEntry::exists(dst) && !DirEntry::exists(backup))
                DirEntry::copyFile(dst, backup);
            DirEntry::removeFile(dst);
            if (!DirEntry::copyFile(src, dst)) {
                error = "cannot write " + dst;
                return false;
            }
            count++;
        }
        return true;
    }

    // a dated pack from psc/<kind>/latest.json, unpacked under `dest`
    bool pack(const string &catalog, const string &what, const string &dest, string &error,
              const TarArchive::Filter &filter) {
        if (stopped(error))
            return false;
        string text;
        PackCatalog cat;
        if (!fetchCatalog(catalog, text, error) || !cat.parse(text)) {
            if (error.empty())
                error = catalog + " is not what was expected";
            return false;
        }
        say("  " + cat.file.name + (cat.count ? " (" + to_string(cat.count) + " " + what + ")" : ""));
        string tarball;
        if (!obtain(cat.file, tarball, error))
            return false;
        DirEntry::createDirs(dest);
        bool ok = untar(tarball, dest, error, filter);
        discard(tarball);
        return ok;
    }

    // each `key = value` line of `keys` set in `text`: the key's line, wherever it is, replaced; appended when
    // there is none. Every other line stays. Returns the number of keys set.
    static int mergeKeys(string &text, const string &keys) {
        int set = 0;
        istringstream in(keys);
        string line;
        while (getline(in, line)) {
            string t = trimmed(line);
            if (t.empty() || t[0] == '#')
                continue;
            size_t eq = t.find('=');
            if (eq == string::npos)
                continue;
            const string key = trimmed(t.substr(0, eq));
            size_t pos = 0;
            bool found = false;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == string::npos)
                    end = text.size();
                string existing = text.substr(pos, end - pos);
                string k = trimmed(existing.substr(0, existing.find('=')));
                if (existing.find('=') != string::npos && k == key) {
                    text.replace(pos, end - pos, t);
                    found = true;
                    break;
                }
                pos = end + 1;
            }
            if (!found) {
                if (!text.empty() && text.back() != '\n')
                    text += "\n";
                text += t + "\n";
            }
            set++;
        }
        return set;
    }

    // the theme's own keys (ab2-theme.cfg) into retroarch.cfg: only after the theme's files are on the stick,
    // so a menu never points at icons that are not there
    bool setCfgKeys(const string &keys, string &error) {
        const string cfg = at("RetroArch/bin/retroarch.cfg");
        string text = readText(cfg);
        const int set = mergeKeys(text, keys);
        if (!writeText(cfg, text)) {
            error = "cannot write " + cfg;
            return false;
        }
        say("  retroarch.cfg: " + to_string(set) + " theme keys set");
        return true;
    }

    // retroarch.cfg, only when there is none: RetroArch keeps it up to date itself and the launcher edits
    // a few keys around each launch - both must keep what the user has set since
    bool writeRetroArchCfg(string &error) {
        const string cfg = at("RetroArch/bin/retroarch.cfg");
        if (DirEntry::exists(cfg)) {
            // an existing cfg is the user's - but a new RetroArch build brings keys the old one gets wrong
            // (a RetroBoot-era cfg on 1.22.2: the XMB theme enum, quit_on_close_content, the front buttons):
            // the build's own keys go over it, everything else stays
            if (newBinary && (!themeCfg.empty() || !statesCfg.empty())) {
                string text = readText(cfg);
                int set = mergeKeys(text, themeCfg);
                const int stateKeys = mergeKeys(text, statesCfg);
                if (!writeText(cfg, text)) {
                    error = "cannot write " + cfg;
                    return false;
                }
                say("  retroarch.cfg kept, " + to_string(set) + " keys of this RetroArch build set in it");
                if (stateKeys > 0)
                    say("  retroarch.cfg: " + to_string(stateKeys) + " save-state keys set");
            } else {
                say("  keeping the existing retroarch.cfg");
            }
            return true;
        }
        string text =
            "# Written by AutoBleem's installer. RetroArch keeps this file up to date itself; AutoBleem edits\n"
            "# a few display keys around each launch. Every directory lives under RetroArch/bin (\":/\" is\n"
            "# this file's folder), the BIOS files under RetroArch/bios, the games under RetroArch/roms.\n"
            "libretro_directory = \":/cores\"\n"
            "libretro_info_path = \":/info\"\n"
            "system_directory = \"/media/RetroArch/bios\"\n"
            "rgui_browser_directory = \"/media/RetroArch/roms/\"\n"
            "core_assets_directory = \":/downloads\"\n"
            "savefile_directory = \":/saves\"\n"
            "savestate_directory = \":/savestates\"\n"
            "playlist_directory = \":/playlists\"\n"
            "content_database_path = \":/database/rdb\"\n"
            "cursor_directory = \":/database/cursors\"\n"
            "cheat_database_path = \":/cheats\"\n"
            "assets_directory = \":/assets\"\n"
            "joypad_autoconfig_dir = \":/autoconfig\"\n"
            "overlay_directory = \":/overlays\"\n"
            "video_shader_dir = \":/shaders\"\n"
            "thumbnails_directory = \":/thumbnails\"\n"
            "screenshot_directory = \":/screenshots\"\n"
            "recording_output_directory = \":/records\"\n"
            "recording_config_directory = \":/records\"\n"
            "rgui_config_directory = \":/config\"\n"
            "core_options_path = \":/config/retroarch-core-options.cfg\"\n"
            "global_core_options = \"true\"\n"
            "log_dir = \":/logs\"\n"
            "cache_directory = \"/tmp/ra_cache\"\n"
            "video_fullscreen = \"true\"\n"
            "input_autodetect_enable = \"true\"\n"
            "menu_show_core_updater = \"false\"\n";
        // the build's own keys (the XMB theme, the front buttons, quit_on_close_content...), comments out
        for (const string *keys : {&themeCfg, &statesCfg}) {
            istringstream in(*keys);
            string line;
            while (getline(in, line)) {
                string t = trimmed(line);
                if (!t.empty() && t[0] != '#')
                    text += t + "\n";
            }
        }
        if (!writeText(cfg, text)) {
            error = "cannot write " + cfg;
            return false;
        }
        say("  retroarch.cfg written");
        return true;
    }

    //******************
    // 6. the BIOS files
    //******************
    bool bios(string &error) {
        phase("BIOS files");
        if (stopped(error))
            return false;
        if (opt.ps1BiosOnly)
            say("  PlayStation only: the PlayStation BIOS files of the pack, not the rest");
        const string biosDir = at("RetroArch/bios");
        if (!fetchBiosPack("psc/bios/latest.json", biosDir, error,
                           opt.ps1BiosOnly ? BiosFilter(isPs1PackFile) : BiosFilter()))
            return false;
        // the PlayStation emulator reads System/Bios/romw.bin and romJP.bin; an existing one is kept
        installPs1Bios(biosDir, at("System/Bios"));
        return true;
    }

    //******************
    // 7. the sample games
    //******************
    bool samples(string &error) {
        phase("Sample games");
        if (stopped(error))
            return false;
        if (DirEntry::exists(at(SamplesMarker))) {
            say("  the samples were put on this stick before (System/samples.txt) - not again");
            return true;
        }
        string text;
        PackCatalog cat;
        if (!fetchCatalog("samples/latest.json", text, error) || !cat.parse(text)) {
            if (error.empty())
                error = "samples/latest.json is not what was expected";
            return false;
        }
        string tarball;
        if (!obtain(cat.file, tarball, error))
            return false;
        // Games/ and SAMPLES.md as they are; the RetroArch part only with RetroArch on the stick, and laid
        // out the console's way: the pack's RetroArch/roms is RetroArch/roms, its RetroArch/thumbnails is
        // RetroArch/bin/thumbnails
        bool ok = untar(tarball, root, error, [](const TarEntry &e) { return e.name.rfind("RetroArch/", 0) != 0; });
        const bool withRetroArch = opt.retroarch || info.hasRetroArch;
        if (ok && withRetroArch)
            ok = untar(tarball, at("RetroArch/roms"), error, TarArchive::Filter(), "RetroArch/roms/") &&
                 untar(tarball, at("RetroArch/bin/thumbnails"), error, TarArchive::Filter(), "RetroArch/thumbnails/");
        discard(tarball);
        if (!ok)
            return false;
        writeText(at(SamplesMarker), cat.file.name + "\n");
        say(string("  done") + (withRetroArch ? "" : " (the PlayStation game; the other systems' need RetroArch)"));
        return true;
    }

    // The console has no network, so the ROMs' names and box art come from a PC run of UpdateRoms: started
    // here, after everything is on the stick, before the first boot. Optional - it needs the PC's network and
    // may be missing or fail, and the stick boots without it (the launcher's own scan then does the work).
    void scanGames() {
        const string exe = at("UpdateRoms/UpdateRoms.exe");
        if (!DirEntry::exists(exe)) {
            say("  UpdateRoms is not on the stick - the launcher scans the games on its first start");
            return;
        }
        InstallOptions::Runner runner = opt.programRunner;
#ifdef _WIN32
        if (!runner)
            runner = [](const string &line) { return System::runShellCommand(line); };
#endif
        if (!runner) {
            say("  UpdateRoms runs on Windows only - run it from the stick's UpdateRoms folder on a PC");
            return;
        }
        say("  running UpdateRoms on the stick (box art needs this PC's network)");
        say("  Running UpdateRoms - this can take several minutes");
        const int status = runner("\"" + exe + "\" \"" + root + "\" --quiet");
        if (status == 0)
            say("  UpdateRoms scanned the games");
        else
            say("  UpdateRoms ended with " + to_string(status) + " - going on, the launcher scans on its first start");
    }

    void finish() {
        phase("Finishing");
        scanGames();
        out.onProgress(1, 1);
        say(info.installed ? "Updated. Put the stick into the console's second controller port and boot it."
                           : "Installed. Put the stick into the console's second controller port and boot it.");
    }

    InstallOptions opt;                   // a copy: a channel's package lands in opt.packageFile
    StickInfo info;                       // and its version in info.packageVersion
    ableem::UpdateFile channelUpdateRoms; // UpdateRoms of the channel's release, when it has one
    string root;
    vector<string> shippedThemes;
    vector<string> shippedExtensions; // the package's Extensions/<name>/ folders
    string savedConfig;
    string themeCfg;
    string ab2Cfg;
    string statesCfg;   // theme/ab2-states.cfg: savestate_auto_save, the thumbnail, the folders and their sorting
    string newVersion;  // the RetroArch zip's VERSION, written last
    string themeAssets; // the RetroArch zip's theme/assets tree, moved aside until the bundles are in
    bool hadDefaultTheme = false; // Themes/<DefaultTheme::Name> was on the stick before this run
    bool newBinary = false;
};

} // namespace

//*******************************
// Downloader::fetchResumable / localFile
//*******************************
bool Downloader::fetchResumable(const string &url, const string &destFile, const Progress &progress, string &error) {
    return fetch(url, destFile, progress, error); // cannot continue a part: the file starts over
}

bool Downloader::localFile(const string &, string &, const Progress &, string &error) {
    error.clear();
    return false;
}

//*******************************
// Downloader::fetchText
//*******************************
bool Downloader::fetchText(const string &url, const string &scratchFile, string &text, string &error) {
    DirEntry::createDirs(scratchFile.substr(0, scratchFile.find_last_of('/')));
    if (!fetch(url, scratchFile, Progress(), error))
        return false;
    text = readText(scratchFile);
    DirEntry::removeFile(scratchFile);
    return true;
}

//*******************************
// InstallerJob::normalizeRoot
//*******************************
string InstallerJob::normalizeRoot(const string &input) {
    string root = input;
    replace(root.begin(), root.end(), '\\', '/');
    while (root.size() > 1 && root.back() == '/' && !(root.size() == 3 && root[1] == ':'))
        root.pop_back();
    if (root.size() == 2 && root[1] == ':')
        root += "/";
    return root;
}

//*******************************
// InstallerJob::packageNextTo
//*******************************
string InstallerJob::packageNextTo(const string &programPath) {
    string dir = programPath;
    replace(dir.begin(), dir.end(), '\\', '/');
    size_t slash = dir.find_last_of('/');
    dir = slash == string::npos ? "." : dir.substr(0, slash);
    string best;
    for (const ableem::DirEntry &e : DirEntry::diru_FilesOnly(dir)) {
        if (e.name.rfind("autobleem-psc-", 0) == 0 && e.name.size() > 7 &&
            e.name.compare(e.name.size() - 7, 7, ".tar.gz") == 0 && e.name > best)
            best = e.name;
    }
    return best.empty() ? "" : dir + "/" + best;
}

//*******************************
// InstallerJob::bundleNextTo / bundlePackage
//*******************************
string InstallerJob::bundleNextTo(const string &programPath) {
    string dir = programPath;
    replace(dir.begin(), dir.end(), '\\', '/');
    size_t slash = dir.find_last_of('/');
    dir = slash == string::npos ? "." : dir.substr(0, slash);
    return DirEntry::exists(dir + "/payload/bundle.json") ? dir + "/payload" : "";
}

string InstallerJob::bundlePackage(const string &bundleDir) {
    ableem::BundleCatalog catalog;
    if (!catalog.load(normalizeRoot(bundleDir) + "/bundle.json") || catalog.package.empty())
        return "";
    return normalizeRoot(bundleDir) + "/" + catalog.package;
}

//*******************************
// InstallerJob::channelLists
//*******************************
vector<string> InstallerJob::channelLists(const string &channel) {
    if (channel == "nightly")
        return {"nightly/latest.json", "releases/unstable.json", "releases/latest.json"};
    if (channel == "testing")
        return {"releases/unstable.json", "releases/latest.json"};
    return {"releases/latest.json"};
}

//*******************************
// InstallerJob::channelRelease
//*******************************
bool InstallerJob::channelRelease(const string &repoUrl, const string &channel, Downloader &downloader,
                                  const string &scratchDir, ChannelRelease &out, string &error) {
    return channelRelease(repoUrl, channel, channelLists(channel), downloader, scratchDir, out, error);
}

bool InstallerJob::channelRelease(const string &repoUrl, const string &channel, const vector<string> &lists,
                                  Downloader &downloader, const string &scratchDir, ChannelRelease &out,
                                  string &error) {
    out = ChannelRelease();
    out.channel = channel;
    string lastError;
    for (const string &list : lists) {
        string text;
        ReleaseCatalog release;
        if (!downloader.fetchText(repoUrl + "/" + list, scratchDir + "/channel.json", text, lastError) ||
            !release.parse(text))
            continue;
        const UpdateFile *pkg = release.fileFor("psc-fs");
        if (!pkg)
            continue; // this list has no stick package (a PC-only build) - the next one stands in
        out.version = release.version;
        out.package = *pkg;
        if (const UpdateFile *ur = release.fileFor("updateroms"))
            out.updateRoms = *ur;
        return true;
    }
    error = "The " + channel + " channel has no PlayStation Classic package" +
            (lastError.empty() ? string() : " (" + lastError + ")");
    return false;
}

//*******************************
// InstallerJob::inspect
//*******************************
StickInfo InstallerJob::inspect(const InstallOptions &options) {
    StickInfo info;
    const string root = normalizeRoot(options.root);
    info.isStick = !root.empty() && DirEntry::isDirectory(root);
    if (info.isStick) {
        info.installed = DirEntry::exists(root + "/" + LauncherBinary);
        info.installedVersion = firstLine(readText(root + "/" + VersionFile));
        info.legacyLayout = LegacyLayout::detect(root);
        info.hasRetroArch = DirEntry::exists(root + "/RetroArch/bin/retroarch") ||
                            (info.legacyLayout && DirEntry::exists(root + "/retroarch/retroarch"));
        info.retroarchVersion = retroarch_version::installed(root);
        for (size_t i = 0; i < 3; i++)
            info.hasCovers[i] = DirEntry::exists(root + "/Autobleem/bin/db/" + Covers[i].file);
    }
    if (!options.packageFile.empty()) {
        string data, error;
        if (TarArchive::readEntry(options.packageFile, VersionFile, data, error))
            info.packageVersion = firstLine(data);
        else
            info.error = error;
    } else if (options.channel.empty()) {
        info.error = "no package and no channel";
    }
    return info;
}

//*******************************
// InstallerJob::phasesFor
//*******************************
vector<string> InstallerJob::phasesFor(const InstallOptions &options, const StickInfo &info) {
    if (!options.retroarchZip.empty())
        return {"RetroArch"};
    if (biosOnly(options))
        return {"BIOS files", "Finishing"};
    if (skipsUnpack(options, info)) {
        vector<string> kept{"Getting the package"};
        if (options.coversJapan || options.coversUsa || options.coversPal)
            kept.push_back("Cover databases");
        if (options.retroarch)
            for (const char *p : {"RetroArch", "RetroArch cores", "Runtime libraries", "Apps", "RetroArch assets"})
                kept.push_back(p);
        if (options.bios && (options.retroarch || info.hasRetroArch))
            kept.push_back("BIOS files");
        if (options.samples)
            kept.push_back("Sample games");
        kept.push_back("Finishing");
        return kept;
    }
    vector<string> phases{"Getting the package", info.installed ? "Preparing the update" : "Preparing the stick"};
    if (info.legacyLayout)
        phases.push_back("Bringing the old layout up to date");
    phases.push_back("Unpacking AutoBleem");
    phases.push_back("UpdateRoms");
    if (options.coversJapan || options.coversUsa || options.coversPal)
        phases.push_back("Cover databases");
    if (options.retroarch)
        for (const char *p : {"RetroArch", "RetroArch cores", "Runtime libraries", "Apps", "RetroArch assets"})
            phases.push_back(p);
    if (options.bios && (options.retroarch || info.hasRetroArch))
        phases.push_back("BIOS files");
    if (options.samples)
        phases.push_back("Sample games");
    phases.push_back("Finishing");
    return phases;
}

//*******************************
// InstallerJob::run
//*******************************
bool InstallerJob::run(const InstallOptions &input, Downloader &downloader, InstallListener &listener,
                       const ShouldStop &shouldStop, string &error) {
    InstallOptions options = input;
    options.root = normalizeRoot(options.root);
    // a bundled run: the packs come from the folder (LocalBundle, checked once), the rest from `downloader`
    LocalBundle bundle(normalizeRoot(options.bundleDir), &downloader);
    Downloader *source = &downloader;
    if (!options.bundleDir.empty()) {
        options.bundleDir = normalizeRoot(options.bundleDir);
        if (!bundle.load(error))
            return false;
        source = &bundle;
        if (options.packageFile.empty() && options.channel.empty() && !bundle.catalog().package.empty()) {
            // the package is read before the run starts (its VERSION): checked against the manifest first
            string checked;
            if (!bundle.checkedPath(bundle.catalog().package, checked, Downloader::Progress(), error))
                return false;
            options.packageFile = checked;
        }
    }
    StickInfo info = inspect(options);
    if (!info.isStick) {
        error = "No such drive: " + options.root;
        return false;
    }
    if (!options.retroarchZip.empty()) { // the console's RetroArch update: no package, no channel
        Run zipRun(options, info, *source, listener, shouldStop);
        return zipRun.go(error);
    }
    if (!biosOnly(options) && info.packageVersion.empty() && options.channel.empty()) { // a channel's package is read in the run
        error = info.error.empty() ? "The package has no VERSION" : info.error;
        return false;
    }
    Run run(options, info, *source, listener, shouldStop);
    return run.go(error);
}
