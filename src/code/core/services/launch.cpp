//
// LaunchService: what App::launchGame and the three EmuInterceptors used to do between them.
//
#include "launch.h"
#include "app_manifest.h"
#include "environment.h"
#include "../main.h"
#include "system.h"
#include "pcsx_config.h"

#include <ableem/engine/config_file_editor.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unistd.h>
#include <ableem/engine/log.h>

using namespace std;

namespace {

const char *const RaNeonCore = "NEON";
const char *const RaPeopsCore = "PEOPS";
const char *const PcsxNeonGpu = "builtin_gpu";

} // namespace

//*******************************
// LaunchService::pcsxLauncherScript
//*******************************
string LaunchService::pcsxLauncherScript() {
    return Env::getPathToRCDir() + sep + "launch.sh";
}

//*******************************
// LaunchService::retroArchLauncherScript
//*******************************
string LaunchService::retroArchLauncherScript() {
    return Env::getPathToRCDir() + sep + "launch_rb.sh";
}

//*******************************
// LaunchService::pcsxBinaryIn / pcsxExecutable / retroArchExecutable
//*******************************
string LaunchService::pcsxBinaryIn(const string &dir) {
    if (dir.empty()) {
        return "";
    }
#ifdef _WIN32
    return dir + sep + "pcsx-ab.exe";
#else
    return dir + sep + "pcsx-ab";
#endif
}

string LaunchService::pcsxExecutable() const {
    // the chosen one first, the other when its folder has no binary - the same order launch.sh keeps
    const bool nxt = config_.inifile.values.at("emulator") == "pcsx-abnxt";
    const string first = pcsxBinaryIn(nxt ? Env::pcsxNxtDir() : Env::pcsxDir());
    const string second = pcsxBinaryIn(nxt ? Env::pcsxDir() : Env::pcsxNxtDir());
    if (!first.empty() && DirEntry::exists(first)) {
        return first;
    }
    if (!second.empty() && DirEntry::exists(second)) {
        PLOG_INFO << "no " << first << " - falling back to " << second;
        return second;
    }
    return "";
}

string LaunchService::retroArchExecutable() {
    for (const string &path : Env::retroArchBinaries()) {
        if (DirEntry::exists(path)) {
            return path;
        }
    }
    return "";
}

//*******************************
// LaunchService::planPcsx
//*******************************
// script mode - args as rc/launch.sh reads them: ssFolder, cdfile, lang, region, gameFolder, resume,
// aspect, filter, pad, emulator (config.ini's "emulator": pcsx-ab or pcsx-abnxt - which Autobleem/bin
// folder the script runs), language (config.ini's "language" by name - English, Polski, ...: pcsx-abnxt
// draws its own screens, the disc picker, in it from its lang/<Name>.txt; the script passes it as
// -language to nxt alone, the classic pcsx-ab would take it for a file to run). direct mode - pcsx-ab's
// own options, the way the scripts invoke it, plus where its dot dir (the save-state folder: pcsx.cfg,
// memcards, sstates) and the BIOS are.
LaunchPlan LaunchService::planPcsx(const PsGame &game, const string &discImage, const string &lang, int resumePoint,
                                   const string &aspect, const string &filter) const {
    LaunchPlan plan;
    if (!Env::directLaunch()) {
        plan.exe = pcsxLauncherScript();
        plan.args = {game.ssFolder,
                     discImage,
                     lang,
                     "2", // region: need to find out if console is jap to switch to 2 - later on
                     game.folder,
                     resumePoint != -1 ? "1" : "0",
                     aspect,
                     filter,
                     "NA", // pad mapping per-game was never wired up; this was always the fallback
                     config_.inifile.values.at("emulator"),
                     config_.inifile.values.at("language")};
        return plan;
    }
    plan.exe = pcsxExecutable();
    if (plan.exe.empty()) {
        // no PS1 emulator of our own on this machine: RetroArch's pcsx_rearmed core plays the game (it
        // cannot read our save-state slots, so a resume starts from the beginning) - launch.sh's fallback
        PLOG_INFO << "no pcsx-ab in " << Env::pcsxDir() << " or " << Env::pcsxNxtDir()
                  << " - falling back to RetroArch's PS1 core";
        return planRetroArch(discImage, RaNeonCore);
    }
    const string emuDir = plan.exe.substr(0, plan.exe.find_last_of('/'));
    const bool nxt = !Env::pcsxNxtDir().empty() && emuDir == Env::pcsxNxtDir();
    if (nxt) {
        // pcsx-abnxt: the profile and the BIOS named outright, full screen by its own option
        plan.cwd = emuDir;
        plan.args = {"-dotdir", game.ssFolder, "-biosdir", Env::getPathToPs1BiosDir()};
    } else {
        // pcsx-ab: the run directory launch.sh builds, made with directory links by launchPcsx()
        plan.cwd = pcsxRunDir();
    }
    const string filterArg = nxt ? filter : pcsxAbFilter(atoi(filter.c_str()));
    for (const char *a :
         {"-filter", filterArg.c_str(), "-ratio", aspect.c_str(), "-lang", lang.c_str(), "-region", "4", "-enter", "1"}) {
        plan.args.push_back(a);
    }
    if (plan.cwd == emuDir) {
        // pcsx-abnxt's own screens in the launcher's language (its lang/<Name>.txt next to the binary)
        plan.args.push_back("-language");
        plan.args.push_back(config_.inifile.values.at("language"));
    }
    if (resumePoint != -1) {
        plan.args.push_back("-load");
        plan.args.push_back(to_string(resumePoint));
    }
    if (plan.cwd == emuDir) {
        plan.args.push_back("-fullscreen");
    }
    plan.args.push_back("-cdfile");
    plan.args.push_back(discImage);
    return plan;
}

//*******************************
// LaunchService::pcsxRunDir
//*******************************
string LaunchService::pcsxRunDir() {
    return Env::getPathToSystemDir() + sep + "runpcsx";
}

//*******************************
// LaunchService::pcsxAbFilter / filterModeFor
//*******************************
string LaunchService::pcsxAbFilter(int mode) {
    return mode == 1 ? "0" : "1";
}

int LaunchService::filterModeFor(const PsGame &game) {
    // the game's own config's filter when it has one: pcsx-abnxt would keep that one over -filter anyway,
    // the classic pcsx-ab only knows -filter
    int mode = atoi(PcsxConfig::value(game, "plat_target.hwfilter").c_str());
    return mode < 0 || mode > 2 ? 0 : mode;
}

//*******************************
// LaunchService::planRetroArch
//*******************************
// script mode - args as rc/launch_rb.sh reads them: file, core. direct mode - retroarch --config <its cfg>
// -L <core> --fullscreen <file>, with the platform's PS1 core for one of ours.
LaunchPlan LaunchService::planRetroArch(const string &file, const string &core) {
    LaunchPlan plan;
    if (!Env::directLaunch()) {
        plan.exe = retroArchLauncherScript();
        plan.args = {file, core};
        return plan;
    }
    plan.exe = retroArchExecutable();
    plan.cwd = Env::getPathToRetroarchDir();
    const string corePath = (core == RaNeonCore || core == RaPeopsCore) ? Env::getPathToRetroarchCoreFile() : core;
    plan.args = {"--config", raConfigFile(), "-L", corePath, "--fullscreen", file};
    return plan;
}

//*******************************
// LaunchService::planRetroArchMenu / launchRetroArchMenu
//*******************************
LaunchPlan LaunchService::planRetroArchMenu() {
    LaunchPlan plan;
    plan.exe = retroArchExecutable();
    plan.cwd = Env::getPathToRetroarchDir();
    plan.args = {"--config", raConfigFile(), "--fullscreen"};
    return plan;
}

void LaunchService::launchRetroArchMenu() {
    LaunchPlan plan = planRetroArchMenu();
    if (plan.exe.empty()) {
        PLOG_WARNING << "no RetroArch binary to run";
        return;
    }
    runner_.run(plan);
}

//*******************************
// LaunchService::planApp
//*******************************
// A multi-platform App (docs/app-format-plan.md): its app.ini resolved for this machine by AppManifest.
// Through a script (the console, the appliances): the App's own Startup= script when it has one, else the
// generic rc/app_run.sh - either sources rc/app_env.sh and execs $AB_APP_EXEC, which is what the ini names
// for this platform. Direct (Windows, no sh): the resolved program itself, with its Args=. Both get the
// AB_APP_* variables and the ini's Env=. An App of the old kind (Startup= only) is run as it always was.
LaunchPlan LaunchService::planApp(const PsGame &game) {
    LaunchPlan plan;
    AppManifest m = AppManifest::load(game.base, "app.ini", Env::appPlatformKeys());
    if (!m.runnable() || m.legacyStartup) {
        plan.exe = game.base + sep + game.startup;
        if (Env::directLaunch()) {
            plan.cwd = game.base;
        }
        return plan;
    }

    plan.env = appEnvironment(m);
    plan.cwd = game.base;
    if (Env::directLaunch()) {
        plan.exe = m.program;
        plan.args = AppManifest::splitArgs(m.args);
        if (!m.libDir.empty()) {
            const char *path = getenv("PATH");
            plan.env.emplace_back("PATH", m.libDir + (path != nullptr && *path ? string(";") + path : ""));
        }
        return plan;
    }
    string own = m.value("startup");
    plan.exe = own.empty() ? appRunScript() : game.base + sep + own;
    return plan;
}

//*******************************
// LaunchService::appRunScript / appEnvironment
//*******************************
string LaunchService::appRunScript() {
    return Env::getPathToRCDir() + sep + "app_run.sh";
}

vector<pair<string, string>> LaunchService::appEnvironment(const AppManifest &m) {
    string keys;
    for (const string &k : Env::appPlatformKeys())
        keys += (keys.empty() ? "" : " ") + k;
    vector<pair<string, string>> env{{"AB_ROOT", Env::getPathToUSBRoot()},
                                     {"AB_APP_DIR", m.folder},
                                     {"AB_APP_EXEC", m.program},
                                     {"AB_APP_ARGS", m.args},
                                     {"AB_APP_LIB", m.libDir},
                                     {"AB_APP_KEY", m.key},
                                     {"AB_PLATFORM", Env::buildTargetKey()},
                                     {"AB_PLATFORM_KEYS", keys},
                                     {"AB_APP_VIRTUAL_PAD", m.usesVirtualPad() ? "1" : "0"}};
    for (const auto &kv : m.env)
        env.push_back(kv);
    return env;
}

//*******************************
// LaunchService::raSavesDir / raConfigFile / raCoreOptionsFile
//*******************************
string LaunchService::raSavesDir() {
    return Env::getPathToRetroarchDir() + sep + "saves";
}

string LaunchService::raConfigFile() {
    return Env::getPathToRetroarchDir() + sep + "retroarch.cfg";
}

string LaunchService::raCoreOptionsFile() {
    return Env::getPathToRetroarchDir() + sep + "config" + sep + "retroarch-core-options.cfg";
}

//*******************************
// LaunchService::selectionScriptFile
//*******************************
string LaunchService::selectionScriptFile() {
    return Env::getPathToRCDir() + sep + "autobleem_cfg.sh";
}

//*******************************
// LaunchService::writeSelectionScript
//*******************************
void LaunchService::writeSelectionScript() {
    if (Env::directLaunch()) {
        return; // no rc script runs after the launcher on a desktop - and no rc directory to write into
    }
    ofstream os;
    string path = selectionScriptFile();
    os.open(path);
    if (!DirEntry::checkWritable(os, path))
        return; // the rc scripts then keep the previous selection
    os << "#!/bin/sh" << endl << endl;
    os << "AB_SELECTION=" << session_.menuOption << endl;
    os << "AB_THEME=" << config_.inifile.values["theme"] << endl;
    os << "AB_PCSX=" << config_.inifile.values["pcsx"] << endl;

    os.flush();
    os.close();
}

//*******************************
// LaunchService::pathFor
//*******************************
LaunchService::Path LaunchService::pathFor(const PsGame &game, EmuMode mode) {
    if (game.foreign) {
        return game.app ? Path::App : Path::RetroArch;
    }
    return mode == EmuMode::Pcsx ? Path::Pcsx : Path::RetroArch;
}

//*******************************
// LaunchService::launch
//*******************************
void LaunchService::launch(PsGamePtr &game, EmuMode mode, int resumePoint) {
    writeSelectionScript();

    switch (pathFor(*game, mode)) {
    case Path::Pcsx:
        memcards_.swapInForLaunch(*game);
        resumePoints_.prepareForLaunch(*game, resumePoint);
        PcsxConfig::migrateLegacy(*game); // what an older build left becomes the game's own config
        launchPcsx(*game, resumePoint);
        PcsxConfig::migrateLegacy(*game); // ...and what an older emulator left just now
        memcards_.swapOutAfterLaunch(*game);
        break;

    case Path::RetroArch:
        if (!game->foreign) {
            PcsxConfig::migrateLegacy(*game); // the RetroArch set-up reads the game's PCSX values
        }
        raMemcardIn(*game);
        launchRetroArch(*game);
        raMemcardOut(*game);
        break;

    case Path::App:
        launchApp(*game);
        break;
    }
}

//*******************************
// LaunchService::discImageFor
//*******************************
string LaunchService::discImageFor(const PsGame &game) {
    string gameFile = game.folder + sep + game.base;
    if (!(DirEntry::matchExtension(game.base, ".pbp") || DirEntry::matchExtension(game.base, ".chd"))) {
        gameFile += ".cue";
    }
    return gameFile;
}

//*******************************
// LaunchService::raBaseNameFor
//*******************************
string LaunchService::raBaseNameFor(const PsGame &game) {
    if (DirEntry::isPBPFile(game.base)) {
        return game.base.substr(0, game.base.length() - 4);
    }
    return game.base;
}

//*******************************
// LaunchService::copyCfgAsLf
//*******************************
// Copy a cfg file forcing LF line endings: the emulator reads its config in text mode and a CRLF file (or
// a stray carriage return on a value) breaks it. Falls back to a plain copy if the file cannot be read.
void LaunchService::copyCfgAsLf(const string &src, const string &dst) {
    ifstream in(src, ios::in | ios::binary);
    if (!in.is_open()) {
        DirEntry::copy(src, dst);
        return;
    }
    string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();
    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
    ofstream out(dst, ios::out | ios::trunc | ios::binary);
    if (!out.is_open()) {
        DirEntry::copy(src, dst);
        return;
    }
    out << content;
    out.close();
}

//*******************************
// LaunchService::launchPcsx
//*******************************
void LaunchService::launchPcsx(PsGame &game, int resumePoint) {
    PLOG_INFO << "calling LaunchService::launchPcsx()";

    library_.updateDatePlayed(game, time(nullptr));

    string lastCDpoint = game.ssFolder + sep + "lastcdimg.txt";
    string lastCDpointX = game.ssFolder + sep + "lastcdimg." + to_string(resumePoint) + ".txt";
    string gameFile = "";

    string aspect = "0";
    if (config_.inifile.values["aspect"] == "true") {
        aspect = "1";
    }

    // the game's own filter (the game editor's Filter row), as pcsx-abnxt numbers it
    string filter = to_string(filterModeFor(game));

    trim(game.ssFolder);
    game.ssFolder = DirEntry::removeSeparatorFromEndOfPath(game.ssFolder);

    remove(lastCDpoint.c_str());

    if (DirEntry::exists(lastCDpointX)) {
        // resuming: the state's own record of which disc was in the drive is what PCSX must be given
        DirEntry::copy(lastCDpointX, lastCDpoint);
        ifstream is(lastCDpointX.c_str());
        if (is.is_open()) {
            std::string line;
            std::getline(is, line);

            // last line is our filename
            gameFile = line;
            is.close();
        }
    } else {
        gameFile = discImageFor(game);
    }

    // hack to get language from lang file
    string langStr = _("|@lang|");
    if (langStr == "|@lang|") {
        langStr = "2";
    }

    if (Env::directLaunch() && !game.internal) {
        // the per-game pcsx.cfg belongs next to the save states, where pcsx-ab reads it - the scripts do
        // this copy themselves. Forced to LF, never a plain byte copy: pcsx-ab/pcsx-abnxt read the cfg in
        // text mode and reject a CRLF file (fread != ftell), and a trailing carriage return would spoil
        // "Bios = SET_BY_PCSX" - so a game cfg that is CRLF (an older install, or edited in Notepad) is
        // normalised on the way in.
        string cfg = game.folder + sep + PCSX_CFG;
        if (DirEntry::exists(cfg)) {
            DirEntry::createDirs(game.ssFolder);
            copyCfgAsLf(cfg, game.ssFolder + sep + PCSX_CFG);
        }
    }

    LaunchPlan plan = planPcsx(game, gameFile, langStr, resumePoint, aspect, filter);
    // the old pcsx-ab started directly: its run directory as launch.sh lays it out - .pcsx the save-state
    // folder, bios the PS1 BIOS, plugins the emulator's - as directory links, cleared again after
    const bool runDir = plan.cwd == pcsxRunDir();
    if (runDir) {
        const string dir = pcsxRunDir(), emuDir = plan.exe.substr(0, plan.exe.find_last_of('/'));
        DirEntry::createDirs(dir);
        DirEntry::createDirs(Env::getPathToPs1BiosDir());
        DirEntry::createDirs(game.ssFolder); // a link needs its target to exist (the emulator would make it)
        bool ok = System::makeDirectoryLink(dir + sep + ".pcsx", game.ssFolder) &&
                  System::makeDirectoryLink(dir + sep + "bios", Env::getPathToPs1BiosDir());
        if (ok && DirEntry::isDirectory(emuDir + sep + "plugins")) {
            ok = System::makeDirectoryLink(dir + sep + "plugins", emuDir + sep + "plugins");
        }
        if (!ok) {
            // no links on this file system (a FAT stick): the emulator would run with an empty profile
            PLOG_WARNING << "cannot lay out " << dir << " - RetroArch's PS1 core plays the game instead";
            plan = planRetroArch(gameFile, RaNeonCore);
        }
    }
    runner_.run(plan);
    if (runDir) {
        for (const char *l : {".pcsx", "bios", "plugins"}) {
            System::removeDirectoryLink(pcsxRunDir() + sep + l);
        }
    }
    usleep(3 * 1000);
}

//*******************************
// LaunchService::launchRetroArch
//*******************************
void LaunchService::launchRetroArch(PsGame &game) {
    PLOG_INFO << "calling LaunchService::launchRetroArch()";

    // one of our own games: a playlist entry's gameId is only its index in the playlist, and would name
    // some unrelated row in regional.db
    if (!game.foreign) {
        library_.updateDatePlayed(game, time(nullptr));
    }

    string gameFile = "";

    PLOG_INFO << "Starting RetroArch Emu";

    if (game.foreign) {
        PLOG_INFO << "RA FOREIGN MODE";
    }

    if (!game.foreign) {
        gameFile = discImageFor(game);
        string base = raBaseNameFor(game);
        if (DirEntry::exists(game.folder + sep + base + ".m3u")) {
            gameFile = game.folder + sep + base + ".m3u";
        }
    } else {
        gameFile = game.image_path;
    }
    // figure out which plugin is selected
    string gpu;
    if (!game.foreign) {
        gpu = PcsxConfig::value(game, "gpu3"); // the game's own config's, when it has one
        gpu = Strings::trim(gpu);
        if (gpu.empty()) {
            gpu = PcsxNeonGpu;
        }
    } else {
        gpu = "NONE";
    }
    PLOG_INFO << "Using GPU plugin: " << gpu;

    string RACore = RaNeonCore;
    if (gpu != PcsxNeonGpu) {
        RACore = RaPeopsCore;
    }

    if (game.foreign) {
        RACore = game.core_path;
    }

    // core config here - to be optional
    if (config_.inifile.values["raconfig"] == "true") {
        backupRaConfig();
        transferRaConfig(game);
    }

    runner_.run(planRetroArch(gameFile, RACore));
    usleep(3 * 1000);

    // core config here - to be optional
    if (config_.inifile.values["raconfig"] == "true") {
        restoreRaConfig();
    }
}

//*******************************
// LaunchService::raMemcardIn
//*******************************
void LaunchService::raMemcardIn(PsGame &game) {
    if (!game.foreign) {
        memcards_.swapInForLaunch(game);

        // Copy the card moved to RA
        string inpath = game.ssFolder + sep + "memcards" + sep + "card1.mcd";
        string outpath = raSavesDir() + sep + raBaseNameFor(game) + ".srm";
        string backup = outpath + ".bak";
        if (!DirEntry::exists(backup)) {
            if (DirEntry::exists(outpath)) {
                DirEntry::copy(outpath, backup);
            }
        }
        if (DirEntry::exists(inpath)) {
            DirEntry::copy(inpath, outpath);
        }
    }
}

//*******************************
// LaunchService::raMemcardOut
//*******************************
void LaunchService::raMemcardOut(PsGame &game) {
    if (!game.foreign) {
        memcards_.swapOutAfterLaunch(game);

        string outpath = game.ssFolder + sep + "memcards" + sep + "card1.mcd";
        string inpath = raSavesDir() + sep + raBaseNameFor(game) + ".srm";
        string backup = inpath + ".bak";
        if (DirEntry::exists(inpath)) {
            DirEntry::copy(inpath, outpath);
        }

        if (DirEntry::exists(backup)) {
            DirEntry::removeFile(inpath);
            DirEntry::renameFile(backup, inpath);
        }
    }
}

//*******************************
// LaunchService::backupRaConfig
//*******************************
void LaunchService::backupRaConfig() {
    DirEntry::copy(raCoreOptionsFile(), raCoreOptionsFile() + ".bak");
    DirEntry::copy(raConfigFile(), raConfigFile() + ".bak");
}

//*******************************
// LaunchService::restoreRaConfig
//*******************************
void LaunchService::restoreRaConfig() {
    if (DirEntry::exists(raCoreOptionsFile() + ".bak")) {
        DirEntry::copy(raCoreOptionsFile() + ".bak", raCoreOptionsFile());
        DirEntry::removeFile(raCoreOptionsFile() + ".bak");
    }
    if (DirEntry::exists(raConfigFile() + ".bak")) {
        DirEntry::copy(raConfigFile() + ".bak", raConfigFile());
        DirEntry::removeFile(raConfigFile() + ".bak");
    }
}

//*******************************
// LaunchService::transferRaConfig
//*******************************
void LaunchService::transferRaConfig(PsGame &game) {
    const string coreOptions = raCoreOptionsFile();
    const string raConfig = raConfigFile();

    if (!game.foreign) {
        // the game's values as the emulator would see them: its own config over pcsx.cfg
        auto value = [&game](const char *key) { return PcsxConfig::value(game, key); };
        ConfigFileEditor processor;

        int highres = atoi(value("gpu_neon.enhancement_enable").c_str());
        int speedhack = atoi(value("gpu_neon.enhancement_no_main").c_str());
        int clock = strtol(value("psx_clock").c_str(), nullptr, 16);
        int dither = atoi(value("gpu_peops.iUseDither").c_str());
        int interpolation = strtol(value("spu_config.iUseInterpolation").c_str(), nullptr, 16);

        int scanlines = atoi(value("scanlines").c_str());
        int scanline_level = strtol(value("scanline_level").c_str(), nullptr, 16);
        int frameskip = atoi(value("frameskip3").c_str());
        string slowBoot = value("SlowBoot");
        bool bootLogo = slowBoot.empty() || atoi(slowBoot.c_str()) != 0;

        // the core options
        if (highres != 0)
            processor.replaceInFile(coreOptions, "pcsx_rearmed_neon_enhancement_enable",
                                    "pcsx_rearmed_neon_enhancement_enable = \"enabled\" ");
        else
            processor.replaceInFile(coreOptions, "pcsx_rearmed_neon_enhancement_enable",
                                    "pcsx_rearmed_neon_enhancement_enable = \"disabled\" ");

        if (dither != 0)
            processor.replaceInFile(coreOptions, "pcsx_rearmed_dithering", "pcsx_rearmed_dithering = \"enabled\" ");
        else
            processor.replaceInFile(coreOptions, "pcsx_rearmed_dithering", "pcsx_rearmed_dithering = \"disabled\" ");

        if (speedhack != 0)
            processor.replaceInFile(coreOptions, "pcsx_rearmed_neon_enhancement_no_main",
                                    "pcsx_rearmed_neon_enhancement_no_main = \"enabled\" ");
        else
            processor.replaceInFile(coreOptions, "pcsx_rearmed_neon_enhancement_no_main",
                                    "pcsx_rearmed_neon_enhancement_no_main = \"disabled\" ");

        processor.replaceInFile(coreOptions, "pcsx_rearmed_psxclock",
                                "pcsx_rearmed_psxclock = \"" + to_string(clock) + "\" ");
        processor.replaceInFile(coreOptions, "pcsx_rearmed_show_bios_bootlogo",
                                string("pcsx_rearmed_show_bios_bootlogo = \"") + (bootLogo ? "enabled" : "disabled") +
                                    "\" ");
        processor.replaceInFile(coreOptions, "pcsx_rearmed_nocdaudio", "pcsx_rearmed_nocdaudio  = \"enabled\" ");

        if (interpolation == 0) {
            processor.replaceInFile(coreOptions, "pcsx_rearmed_spu_interpolation",
                                    "pcsx_rearmed_spu_interpolation = \"off\" ");
        }
        if (interpolation == 1) {
            processor.replaceInFile(coreOptions, "pcsx_rearmed_spu_interpolation",
                                    "pcsx_rearmed_spu_interpolation = \"simple\" ");
        }
        if (interpolation == 2) {
            processor.replaceInFile(coreOptions, "pcsx_rearmed_spu_interpolation",
                                    "pcsx_rearmed_spu_interpolation = \"gaussian\" ");
        }
        if (interpolation == 3) {
            processor.replaceInFile(coreOptions, "pcsx_rearmed_spu_interpolation",
                                    "pcsx_rearmed_spu_interpolation = \"cubic\" ");
        }

        processor.replaceInFile(coreOptions, "pcsx_rearmed_frameskip",
                                "pcsx_rearmed_frameskip  = \"" + to_string(frameskip) + "\" ");
        if (scanlines == 1) {
            float opacity = scanline_level / 100.0f;
            processor.replaceInFile(raConfig, "input_overlay", "input_overlay  = \":/overlay/scanlines.cfg\" ");
            processor.replaceInFile(raConfig, "input_overlay_enable", "input_overlay_enable  = \"true\" ");
            processor.replaceInFile(raConfig, "input_overlay_opacity",
                                    "input_overlay_opacity  = \"" + to_string(opacity) + "\" ");
        }
    }

    // retroarch.cfg
    ConfigFileEditor processor;
    string aspect = config_.inifile.values["aspect"]; // true - 1280x720 - false 960x720
    if (aspect == "true") {
        // widescreen
        processor.replaceInFile(raConfig, "custom_viewport_width", "custom_viewport_width  = \"1280\" ");
        processor.replaceInFile(raConfig, "custom_viewport_height", "custom_viewport_height  = \"720\" ");
        processor.replaceInFile(raConfig, "custom_viewport_x", "custom_viewport_x  = \"0\" ");
        processor.replaceInFile(raConfig, "custom_viewport_y", "custom_viewport_y  = \"0\" ");
        processor.replaceInFile(raConfig, "aspect_ratio_index", "aspect_ratio_index  = \"23\" ");
    } else {
        // 4:3
        processor.replaceInFile(raConfig, "custom_viewport_width", "custom_viewport_width  = \"960\" ");
        processor.replaceInFile(raConfig, "custom_viewport_height", "custom_viewport_height  = \"720\" ");
        processor.replaceInFile(raConfig, "custom_viewport_x", "custom_viewport_x  = \"160\" ");
        processor.replaceInFile(raConfig, "custom_viewport_y", "custom_viewport_y  = \"0\" ");
        processor.replaceInFile(raConfig, "aspect_ratio_index", "aspect_ratio_index  = \"0\" ");
    }

    // a PS1 game's own filter (its pcsx.cfg): RetroArch smooths or it does not - Sharp is Off here, as in the
    // classic pcsx-ab. A foreign game has no pcsx.cfg and keeps RetroArch's own video_smooth.
    if (!game.foreign) {
        processor.replaceInFile(raConfig, "video_smooth",
                                string("video_smooth  = \"") + (filterModeFor(game) == 1 ? "true" : "false") + "\" ");
    }
}

//*******************************
// LaunchService::launchApp
//*******************************
void LaunchService::launchApp(PsGame &game) {
    PLOG_INFO << "calling LaunchService::launchApp()";
    PLOG_INFO << "Starting External App";

    library_.updateDatePlayed(game, time(nullptr));

    if (game.foreign) {
        PLOG_INFO << "FOREIGN MODE";
    }

    runner_.run(planApp(game));
    usleep(3 * 1000);
}
