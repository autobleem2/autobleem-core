#include "installer/installer_job.h"
#include "installer/install_job_base.h"
#include "installer/legacy_layout.h"
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

#ifndef _WIN32
#include <sys/stat.h>
#endif

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
// room kept for one libretro bundle not on the stick yet (assets is the big one, ~50 MB packed)
const uint64_t BundleAllowance = 100ull * 1024 * 1024;

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

// the folders of RetroArch/bin a Remove takes (each renamed <name>.removing, then deleted); a Remove that was cut
// off leaves the renamed ones, and the next Install or Remove finishes them
const char *const RemovedDirs[] = {
    "cores",
    "info",
    "assets",
    "database",
    "cheats",
    "overlays",
    "shaders",
    "downloads",
    "records",
    "Retroarch themes",
};

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
        bool ok =
            package(error) && prepare(error) && legacy(error) && unpack(error) && updateRoms(error) && covers(error);
        if (ok && opt.retroarch)
            ok = retroarch(error);
        if (ok && opt.bios && (opt.retroarch || info.hasRetroArch))
            ok = bios(error);
        if (ok && opt.samples)
            ok = samples(error);
        if (ok && opt.removeRetroBoot) {
            // the user's choice, last: everything the stick needs is in place by now; a refusal is a line, not a failure
            say("RetroBoot's leftovers were asked to be removed");
            LegacyLayout::removeRetroBoot(root, [this](const string &line) { say(line); });
        }
        if (ok)
            finish();
        if (DirEntry::isDirectory(scratch) && opt.scratchDir.empty())
            DirEntry::removeDirAndContents(scratch);
        return ok;
    }

    //******************
    // RetroArch alone
    //******************
    // InstallerJob::retroarchInstall: the phases are titled with the progress file's keys
    bool raInstall(const RetroArchOptions &ro, bool withBios, string &error) {
        keepDownloads = deferBinary = true;
        phases.clear();
        for (const char *key : {"check-space", "download-retroarch", "download-cores"})
            phases.push_back(key);
        for (const char *key : {"download-libs", "download-apps", "download-assets"})
            phases.push_back(key);
        if (withBios)
            phases.push_back("download-bios");
        for (const char *key : {"unpack", "merge-config", "install-binary"})
            phases.push_back(key);
        if (!DirEntry::createDirs(scratch) || !DirEntry::isDirectory(scratch)) {
            error = "cannot make the scratch folder " + scratch;
            failKind = FailKind::Other;
            return false;
        }
        bool ok = raCheckSpace(ro, error);
        if (ok) {
            phase("download-retroarch");
            ok = !stopped(error) && downloadVerified(raCatalog.zip, scratch + "/" + raCatalog.zip.name, error);
        }
        const pair<const char *, const PackCatalog *> packs[] = {{"download-cores", &coresCatalog},
                                                                  {"download-libs", &libsCatalog},
                                                                  {"download-apps", &appsCatalog}};
        for (const auto &p : packs) {
            if (!ok)
                break;
            phase(p.first);
            ok = !stopped(error) && downloadVerified(p.second->file, scratch + "/" + p.second->file.name, error);
        }
        if (ok)
            ok = raDownloadBundles(error);
        if (ok && withBios) {
            phase("download-bios");
            ok = !stopped(error) && fetchBiosPack("psc/bios/latest.json", at("RetroArch/bios"), error);
            if (!ok && !stopped(error))
                failKind = FailKind::Download;
        }
        if (ok)
            ok = raUnpack(error);
        if (ok) {
            phase("merge-config");
            out.onItem("retroarch.cfg", 1, 2, "items");
            out.onProgress(0, 2);
            ok = !stopped(error) && writeRetroArchCfg(error);
            if (ok) {
                out.onItem("theme", 2, 2, "items");
                out.onProgress(1, 2);
                ok = applyTheme(assetsReady, error);
            }
            if (ok)
                out.onProgress(2, 2);
        }
        if (ok) {
            phase("install-binary");
            out.onItem("retroarch", 1, 2, "items");
            out.onProgress(0, 2);
            ok = !stopped(error) && installBinary(error);
            if (ok) {
                out.onItem("version", 2, 2, "items");
                out.onProgress(1, 2);
                ok = stampVersion(error);
            }
            if (ok)
                out.onProgress(2, 2);
        }
        if (ok) { // done: the downloads are of no use any more
            DirEntry::removeDirAndContents(scratch);
        }
        if (!ok && failKind == FailKind::None && error != "Stopped")
            failKind = FailKind::Other;
        return ok;
    }

    FailKind failure() const { return failKind; }

private:
    string at(const string &rel) const { return root + "/" + rel; }

    // the four catalogs, and the room the run needs: what has to be downloaded and unpacked, with a bundle
    // allowance. Nothing is downloaded when the filesystem has less.
    bool raCheckSpace(const RetroArchOptions &ro, string &error) {
        phase("check-space");
        // the four catalogs are the items of this phase
        out.onItem("retroarch", 1, 4, "items");
        out.onProgress(0, 4);
        string text;
        if (!fetchCatalog("psc/retroarch/latest.json", text, error) || !raCatalog.parse(text))
            return catalogLost("psc/retroarch/latest.json", error);
        uint64_t need = 0;
        const pair<const char *, PackCatalog *> packs[] = {
            {"psc/cores/latest.json", &coresCatalog}, {"psc/libs/latest.json", &libsCatalog},
            {"psc/apps/latest.json", &appsCatalog}};
        int caught = 1;
        for (const auto &p : packs) {
            if (stopped(error))
                return false;
            out.onItem(string(p.first).substr(4, string(p.first).find('/', 4) - 4), caught + 1, 4, "items");
            out.onProgress(caught, 4);
            if (!fetchCatalog(p.first, text, error) || !p.second->parse(text))
                return catalogLost(p.first, error);
            need += p.second->file.size * 2; // the download and what it unpacks to
            caught++;
        }
        out.onProgress(4, 4);
        need += raCatalog.zip.size * 2;
        for (const Bundle &b : Bundles)
            if (!DirEntry::exists(scratch + "/" + b.name + ".zip") && !bundleThere(b))
                need += BundleAllowance;
        // what is in the scratch folder already (a resume) is not to be fetched again
        for (const UpdateFile *f : {&raCatalog.zip, &coresCatalog.file, &libsCatalog.file, &appsCatalog.file})
            if (DirEntry::exists(scratch + "/" + f->name))
                need -= min<uint64_t>(need, f->size);
        // a Remove that was cut off (power loss) left <name>.removing folders: this install finishes it
        vector<string> leftovers{at("RetroArch/bios.removing")};
        for (const char *d : RemovedDirs)
            leftovers.push_back(at(string("RetroArch/bin/") + d + ".removing"));
        for (const string &gone : leftovers)
            if (DirEntry::isDirectory(gone))
                DirEntry::removeDirAndContents(gone);
        uint64_t free = 0, total = 0;
        const bool known = ro.diskSpace ? ro.diskSpace(root, free, total) : System::diskSpace(root, free, total);
        const string room = known ? humanSize(free) + " free" : string("free space unknown");
        say("  needs about " + humanSize(need) + ", " + room);
        if (known && free < need) {
            error = "Not enough room on " + root + ": " + humanSize(need) + " needed, " + humanSize(free) + " free";
            failKind = FailKind::NoSpace;
            return false;
        }
        return true;
    }

    // a catalog that could not be fetched (error says why: no network, a 404) or parsed (error empty)
    bool catalogLost(const string &name, string &error) {
        if (stopped(error))
            return false;
        if (error.empty()) {
            error = name + " is not what was expected";
            failKind = FailKind::Other;
        } else {
            classifyFailure(error);
        }
        return false;
    }

    bool bundleThere(const Bundle &b) const {
        const string dest = at(string("RetroArch/bin/") + b.dest);
        return DirEntry::isDirectory(dest) && !DirEntry::diru(dest).empty();
    }

    // libretro's bundles into the scratch folder (the ones the stick has already are not fetched); a lost one
    // is reported and gone past, as the full install does
    bool raDownloadBundles(string &error) {
        phase("download-assets");
        assetsReady = true;
        assetsDone = 0;
        // the zips still to fetch, and their sizes (HEAD) so one bar in bytes can run over all of them; when
        // the server does not tell every size the phase counts the zips instead
        vector<const Bundle *> todo;
        for (const Bundle &b : Bundles)
            if (!DirEntry::exists(scratch + "/" + b.name + ".zip") && !bundleThere(b))
                todo.push_back(&b);
        const int count = static_cast<int>(todo.size());
        if (todo.empty()) {
            out.onItem("", 0, 0, "items");
            out.onProgress(1, 1);
            return true;
        }
        uint64_t sum = 0;
        bool sized = true;
        DirEntry::createDirs(scratch);
        for (const Bundle *b : todo) {
            if (stopped(error))
                return false;
            uint64_t size = 0;
            if (dl.contentLength(opt.buildbotUrl + "/" + b->name + ".zip", scratch + "/size.txt", size))
                sum += size;
            else
                sized = false;
        }
        const char *unit = sized ? "bytes" : "items";
        for (int i = 0; i < count; i++) {
            const Bundle &b = *todo[i];
            if (stopped(error))
                return false;
            const string zip = scratch + "/" + b.name + ".zip";
            out.onItem(string(b.name) + ".zip", i + 1, count, unit);
            progressBase = progressTotal = 0;
            if (sized) {
                progressTotal = sum;
                progressBase = assetsDone;
            }
            out.onProgress(sized ? assetsDone : static_cast<uint64_t>(i), sized ? sum : static_cast<uint64_t>(count));
            say("  " + string(b.name));
            countOnly = !sized;
            const bool fetched = download(opt.buildbotUrl + "/" + b.name + ".zip", zip, error);
            countOnly = false;
            progressTotal = 0;
            if (fetched) {
                assetsDone += sized ? static_cast<uint64_t>(max<long long>(0, DirEntry::fileSize(zip))) : 1;
                continue;
            }
            if (stopped(error))
                return false;
            say("  could not download " + string(b.name) + ".zip: " + error + " - going on without it");
            error.clear();
            failKind = FailKind::None;
            if (string(b.name) == "assets")
                assetsReady = false;
            assetsDone += sized ? 0 : 1;
        }
        out.onProgress(sized ? sum : static_cast<uint64_t>(count), sized ? sum : static_cast<uint64_t>(count));
        return true;
    }

    // everything but the binary: the zip's loose files, the packs, the bundles
    bool raUnpack(string &error) {
        phase("unpack");
        if (stopped(error))
            return false;
        const string bin = at("RetroArch/bin");
        int archives = 4; // the RetroArch zip, the three packs, and the bundles that were fetched
        for (const Bundle &b : Bundles)
            if (DirEntry::exists(scratch + "/" + b.name + ".zip"))
                archives++;
        int archive = 1;
        if (!installZip(scratch + "/" + raCatalog.zip.name, raCatalog.zip.name, error, archive++, archives))
            return false;
        if (!DirEntry::exists(scratch + "/retroarch/retroarch")) {
            error = raCatalog.zip.name + " has no retroarch binary";
            return false;
        }
        const pair<const PackCatalog *, pair<string, TarArchive::Filter>> packs[] = {
            {&coresCatalog, {bin, TarArchive::Filter()}},
            {&libsCatalog, {at("Autobleem/lib"), [](const TarEntry &e) { return e.name != "libs.json"; }}},
            {&appsCatalog, {root, [](const TarEntry &e) { return e.name != "apps.json"; }}}};
        for (const auto &p : packs) {
            if (stopped(error))
                return false;
            DirEntry::createDirs(p.second.first);
            out.onItem(p.first->file.name, archive++, archives, "bytes");
            out.onProgress(0, max<uint64_t>(1, static_cast<uint64_t>(max<long long>(
                                                   0, DirEntry::fileSize(scratch + "/" + p.first->file.name)))));
            if (!untar(scratch + "/" + p.first->file.name, p.second.first, error, p.second.second))
                return false;
        }
        for (const Bundle &b : Bundles) {
            if (stopped(error))
                return false;
            const string zip = scratch + "/" + b.name + ".zip";
            if (!DirEntry::exists(zip))
                continue;
            out.onItem(string(b.name) + ".zip", archive++, archives, "files");
            if (!ZipArchive::extractCounting(zip, bin + "/" + b.dest, [this](uint64_t done, uint64_t total) {
                    out.onProgress(done, total ? total : 1);
                })) {
                say("  could not unpack " + string(b.name) + ".zip - going on without it");
                DirEntry::removeFile(zip); // a broken zip would be unpacked again and again
                if (string(b.name) == "assets")
                    assetsReady = false;
            }
        }
        return true;
    }

    // the binary, the very last thing: copied beside the old one as retroarch.new, then renamed over it, so an
    // interrupted run leaves the old RetroArch or the new one, never half of one
    bool installBinary(string &error) {
        const string bin = at("RetroArch/bin/retroarch"), staged = bin + ".new";
        DirEntry::removeFile(staged);
        if (!DirEntry::copyFile(scratch + "/retroarch/retroarch", staged)) {
            error = "cannot write " + staged;
            return false;
        }
#ifndef _WIN32
        chmod(staged.c_str(), 0755);
#endif
        if (!DirEntry::replaceFile(staged, bin)) {
            DirEntry::removeFile(staged);
            error = "cannot write " + bin;
            return false;
        }
        return true;
    }

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
            const string zip = scratch + "/" + ra.zip.name;
            if (!downloadVerified(ra.zip, zip, error))
                return false;
            if (!installZip(zip, ra.zip.name, error))
                return false;
            DirEntry::removeFile(zip);
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
            const string zip = scratch + "/" + b.name + ".zip";
            if (!download(opt.buildbotUrl + "/" + b.name + ".zip", zip, error)) {
                say("  could not download " + string(b.name) + ".zip: " + error + " - going on without it");
                error.clear();
                if (string(b.name) == "assets")
                    assetsReady = false;
                continue;
            }
            if (!ZipArchive::extract(zip, dest)) {
                say("  could not unpack " + string(b.name) + ".zip - going on without it");
                if (string(b.name) == "assets")
                    assetsReady = false;
            }
            error.clear();
            DirEntry::removeFile(zip);
        }
        return applyTheme(assetsReady, error) && stampVersion(error);
    }

    // a RetroArch zip (the site's retroarch-psc-<v>.zip) unpacked and laid over RetroArch/bin: the binary, the
    // docs and the theme's loose files; the theme's assets tree, its cfg keys and the VERSION wait for
    // applyTheme and stampVersion. Every file goes in under a temporary name and is renamed over the old one, so
    // a write that fails half way leaves the old file; the zip is unpacked before anything on the stick is
    // touched. The console's own update (retroarchZip) and the PC installer share this.
    // the zip's files counted on the phase's bar (item `itemIndex` of `itemCount` of the phase)
    bool installZip(const string &zip, const string &zipName, string &error, int itemIndex = 1, int itemCount = 1) {
        const string bin = at("RetroArch/bin");
        const string unpacked = scratch + "/retroarch";
        DirEntry::removeDirAndContents(unpacked);
        out.onItem(zipName, itemIndex, itemCount, "files");
        if (!ZipArchive::extractCounting(zip, unpacked, [this](uint64_t done, uint64_t total) {
                out.onProgress(done, total ? total : 1);
            })) {
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
            if (deferBinary && string(p.from) == "retroarch")
                continue; // laid last, by installBinary
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
        if (!deferBinary) // else the binary is still in it
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
        const string tarball = scratch + "/" + cat.file.name;
        if (!downloadVerified(cat.file, tarball, error))
            return false;
        DirEntry::createDirs(dest);
        bool ok = untar(tarball, dest, error, filter);
        if (!keepDownloads)
            DirEntry::removeFile(tarball);
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
        return fetchBiosPack("psc/bios/latest.json", at("RetroArch/bios"), error);
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
        const string tarball = scratch + "/" + cat.file.name;
        if (!downloadVerified(cat.file, tarball, error))
            return false;
        // Games/ and SAMPLES.md as they are; the RetroArch part only with RetroArch on the stick, and laid
        // out the console's way: the pack's RetroArch/roms is RetroArch/roms, its RetroArch/thumbnails is
        // RetroArch/bin/thumbnails
        bool ok = untar(tarball, root, error, [](const TarEntry &e) { return e.name.rfind("RetroArch/", 0) != 0; });
        const bool withRetroArch = opt.retroarch || info.hasRetroArch;
        if (ok && withRetroArch)
            ok = untar(tarball, at("RetroArch/roms"), error, TarArchive::Filter(), "RetroArch/roms/") &&
                 untar(tarball, at("RetroArch/bin/thumbnails"), error, TarArchive::Filter(), "RetroArch/thumbnails/");
        DirEntry::removeFile(tarball);
        if (!ok)
            return false;
        writeText(at(SamplesMarker), cat.file.name + "\n");
        say(string("  done") + (withRetroArch ? "" : " (the PlayStation game; the other systems' need RetroArch)"));
        return true;
    }

    void finish() {
        phase("Finishing");
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
    // the RetroArch-only run: downloads stay in the scratch folder until it succeeds (the resume), and the binary
    // waits in the unpacked zip for installBinary
    bool keepDownloads = false;
    bool deferBinary = false;
    bool assetsReady = true;
    uint64_t assetsDone = 0; // download-assets: the bytes (or zips) finished so far
    ableem::PscRetroArchCatalog raCatalog;
    ableem::PackCatalog coresCatalog, libsCatalog, appsCatalog;
};

} // namespace

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
    StickInfo info = inspect(options);
    if (!info.isStick) {
        error = "No such drive: " + options.root;
        return false;
    }
    if (!options.retroarchZip.empty()) { // the console's RetroArch update: no package, no channel
        Run zipRun(options, info, downloader, listener, shouldStop);
        return zipRun.go(error);
    }
    if (info.packageVersion.empty() && options.channel.empty()) { // a channel's package is read in the run
        error = info.error.empty() ? "The package has no VERSION" : info.error;
        return false;
    }
    Run run(options, info, downloader, listener, shouldStop);
    return run.go(error);
}

//*******************************
// InstallerJob::retroarchInstall
//*******************************
RetroArchResult InstallerJob::retroarchInstall(const RetroArchOptions &ro, bool withBios, Downloader &downloader,
                                               InstallListener &listener, const ShouldStop &shouldStop) {
    RetroArchResult result;
    InstallOptions options;
    options.root = normalizeRoot(ro.root);
    options.repoUrl = ro.repoUrl;
    options.buildbotUrl = ro.buildbotUrl;
    options.scratchDir = ro.scratchDir.empty() ? options.root + "/System/Install/retroarch" : ro.scratchDir;
    const StickInfo info = inspect(options);
    if (!info.isStick) {
        result.error = "No such drive: " + options.root;
        result.kind = FailKind::Other;
        return result;
    }
    Run run(options, info, downloader, listener, shouldStop);
    result.ok = run.raInstall(ro, withBios, result.error);
    result.stopped = !result.ok && result.error == "Stopped";
    result.kind = result.ok || result.stopped ? FailKind::None : run.failure();
    return result;
}

//*******************************
// InstallerJob::retroarchRemove
//*******************************
RetroArchResult InstallerJob::retroarchRemove(const RetroArchOptions &ro, bool withBios, InstallListener &listener,
                                              const ShouldStop &shouldStop) {
    RetroArchResult result;
    const string root = normalizeRoot(ro.root), ra = root + "/RetroArch", bin = ra + "/bin";
    if (!DirEntry::isDirectory(root)) {
        result.error = "No such drive: " + root;
        result.kind = FailKind::Other;
        return result;
    }
    // the binary goes first, with its stamp and the theme's loose files
    const vector<string> files{bin + "/retroarch", bin + "/retroarch.new", bin + "/VERSION",
                               bin + "/fonts/selawik-light.ttf", bin + "/fonts/OFL.txt"};
    // then these folders, each renamed <name>.removing and deleted. autoconfig, config, playlists, saves,
    // savestates, screenshots, thumbnails, roms, retroarch.cfg and the rest of the user's stay.
    vector<string> dirs;
    for (const char *d : RemovedDirs)
        dirs.push_back(bin + "/" + d);
    if (withBios)
        dirs.push_back(ra + "/bios");

    listener.onPhase(1, 1, "remove-files");
    if (!DirEntry::exists(bin + "/retroarch") && !ro.aptBinary.empty() && DirEntry::exists(ro.aptBinary)) {
        listener.onLine("RetroArch here is the system's (" + ro.aptBinary + "): not removed");
        result.error = ro.aptBinary + " belongs to the package manager";
        result.kind = FailKind::AptProvided;
        return result;
    }
    const uint64_t total = files.size() + dirs.size() * 2;
    uint64_t done = 0;
    // a Remove has no Stop: it runs to the end (the launcher offers none), so a SIGTERM does not cut it short
    (void)shouldStop;
    const auto nameOf = [](const string &path) { return path.substr(path.find_last_of('/') + 1); };
    listener.onItem("", 0, 0, "items");
    listener.onProgress(0, total);
    for (const string &f : files) {
        listener.onItem(nameOf(f), static_cast<int>(done + 1), static_cast<int>(total), "items");
        DirEntry::removeFile(f);
        listener.onProgress(++done, total);
    }
    // a run that was cut off left <name>.removing folders: they go with this one
    for (const string &d : dirs) {
        listener.onItem(nameOf(d), static_cast<int>(done + 1), static_cast<int>(total), "items");
        const string gone = d + ".removing";
        if (DirEntry::isDirectory(gone))
            DirEntry::removeDirAndContents(gone);
        if (DirEntry::isDirectory(d) && !DirEntry::renameFile(d, gone)) {
            result.error = "cannot remove " + d;
            result.kind = FailKind::Other;
            return result;
        }
        listener.onProgress(++done, total);
    }
    for (const string &d : dirs) {
        listener.onItem(nameOf(d), static_cast<int>(done + 1), static_cast<int>(total), "items");
        const string gone = d + ".removing";
        if (DirEntry::isDirectory(gone))
            DirEntry::removeDirAndContents(gone);
        listener.onLine("removed " + d);
        listener.onProgress(++done, total);
    }
    if (withBios)
        DirEntry::createDirs(ra + "/bios"); // the folder itself stays for the next install
    result.ok = true;
    return result;
}
