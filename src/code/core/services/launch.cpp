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
    for (const char *a : {"-filter", filterArg.c_str(), "-ratio", aspect.c_str(), "-lang", lang.c_str(), "-region", "4",
                          "-enter", "1"}) {
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
// LaunchService::pcsxDirForLaunch / pcsxFeatures / pcsxExitDir
//*******************************
string LaunchService::pcsxDirForLaunch() const {
    if (Env::directLaunch()) {
        const string exe = pcsxExecutable();
        return exe.empty() ? "" : exe.substr(0, exe.find_last_of('/'));
    }
    // what launch.sh picks: config.ini's emulator, Autobleem/bin/emu when emunxt has no binary
    const string bin = Env::getPathToAutobleemDir() + sep + "bin" + sep;
    const string nxt = bin + "emunxt";
    if (config_.inifile.values.at("emulator") == "pcsx-abnxt" && DirEntry::exists(nxt + sep + "pcsx-ab"))
        return nxt;
    return bin + "emu";
}

vector<string> LaunchService::pcsxFeatures() const {
    vector<string> features;
    const string dir = pcsxDirForLaunch();
    ifstream in(dir + sep + "abfeatures");
    string line;
    while (getline(in, line)) {
        line = Strings::trim(line);
        if (!line.empty() && line[0] != '#')
            features.push_back(line);
    }
    return features;
}

string LaunchService::pcsxExitDir() {
    return Env::getPathToRuntimeDir() + sep + "exit";
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
    plan.args = {"--config", raConfigFile(), "--appendconfig", raAppendFile(), "-L", corePath, "--fullscreen", file};
    return plan;
}

//*******************************
// LaunchService::planRetroArchMenu / launchRetroArchMenu
//*******************************
LaunchPlan LaunchService::planRetroArchMenu() {
    LaunchPlan plan;
    plan.exe = retroArchExecutable();
    plan.cwd = Env::getPathToRetroarchDir();
    plan.args = {"--config", raConfigFile(), "--appendconfig", raAppendFile(), "--fullscreen"};
    return plan;
}

void LaunchService::launchRetroArchMenu() {
    LaunchPlan plan = planRetroArchMenu();
    if (plan.exe.empty()) {
        PLOG_WARNING << "no RetroArch binary to run";
        return;
    }
    restoreLegacyRaBackup();
    prepareRaAppend(nullptr);
    runner_.run(plan);
    restoreAppended();
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

string LaunchService::raAppendFile() {
    return Env::getPathToRuntimeDir() + sep + "ra-append.cfg";
}

string LaunchService::raRuntimeCoreOptionsFile() {
    return Env::getPathToRuntimeDir() + sep + "ra-core-options.cfg";
}

//*******************************
// LaunchService::selectionScriptFile
//*******************************
string LaunchService::selectionScriptFile() {
    return Env::getPathToRuntimeDir() + sep + "autobleem_cfg.sh";
}

//*******************************
// LaunchService::writeSelectionScript
//*******************************
void LaunchService::writeSelectionScript() {
    if (Env::directLaunch()) {
        return; // no rc script runs after the launcher on a desktop - and no rc directory to write into
    }
    // a hand-over to the script that runs after us, so RAM, not the stick (docs/quiet-stick-plan.md)
    DirEntry::createDirs(Env::getPathToRuntimeDir());
    string text = "#!/bin/sh\n\n";
    text += "AB_SELECTION=" + to_string(session_.menuOption) + "\n";
    text += "AB_THEME=" + config_.inifile.values["theme"] + "\n";
    text += "AB_PCSX=" + config_.inifile.values["pcsx"] + "\n";
    DirEntry::writeFileIfChanged(selectionScriptFile(), text);
    // RetroArch's own menu, started by the scripts once we have left: it gets config_save_on_exit too
    if (session_.menuOption == MENU_OPTION_RETRO)
        prepareRaAppend(nullptr);
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
    switch (pathFor(*game, mode)) {
    case Path::Pcsx: {
        // what this emulator can take off the stick's hands (docs/quiet-stick-plan.md): the set played
        // where it is, the resume point of the way out in RAM, a kept slot read where it is
        const vector<string> features = pcsxFeatures();
        auto has = [&features](const char *f) {
            return std::find(features.begin(), features.end(), f) != features.end();
        };
        LaunchPlan::Env env;
        string cardSet = has("memcarddir") ? memcards_.setDirForLaunch(*game) : "";
        if (cardSet.empty())
            memcards_.swapInForLaunch(*game); // copies the set in, and out again below
        else
            env.emplace_back("AB_MEMCARD_DIR", cardSet);
        resumePoints_.setExitDir(has("exitdir") ? pcsxExitDir() : "");
        if (has("exitdir"))
            env.emplace_back("AB_EXIT_DIR", pcsxExitDir());
        const string loadState = resumePoints_.prepareForLaunch(*game, resumePoint, has("loadstate"));
        if (!loadState.empty())
            env.emplace_back("AB_LOAD_STATE", loadState);
        PcsxConfig::migrateLegacy(*game); // what an older build left becomes the game's own config
        launchPcsx(*game, resumePoint, env);
        PcsxConfig::migrateLegacy(*game); // ...and what an older emulator left just now
        if (cardSet.empty())
            memcards_.swapOutAfterLaunch(*game);
        break;
    }

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
// a stray carriage return on a value) breaks it. Every launch comes here, so the copy is written only when
// it differs from what dst already holds.
void LaunchService::copyCfgAsLf(const string &src, const string &dst) {
    string content;
    if (!DirEntry::readFile(src, content))
        return; // nothing to copy - the emulator falls back to its own defaults, as it did before
    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
    DirEntry::writeFileIfChanged(dst, content);
}

//*******************************
// LaunchService::launchPcsx
//*******************************
void LaunchService::launchPcsx(PsGame &game, int resumePoint, const LaunchPlan::Env &env) {
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

    const bool exitDir = std::find_if(env.begin(), env.end(), [](const pair<string, string> &e) {
                             return e.first == "AB_EXIT_DIR";
                         }) != env.end();
    if (!exitDir && DirEntry::exists(lastCDpoint))
        remove(lastCDpoint.c_str()); // (an emulator with an exit dir writes its own there, not here)

    if (DirEntry::exists(lastCDpointX)) {
        // resuming: the state's own record of which disc was in the drive is what PCSX must be given
        // (-cdfile below; the copy is for an emulator that reads it from its folder)
        if (!exitDir)
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
    plan.env.insert(plan.env.end(), env.begin(), env.end());
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

    restoreLegacyRaBackup();
    prepareRaAppend(&game);
    runner_.run(planRetroArch(gameFile, RACore));
    usleep(3 * 1000);
    restoreAppended();
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
// LaunchService::restoreLegacyRaBackup
//*******************************
void LaunchService::restoreLegacyRaBackup() {
    for (const string &file : {raCoreOptionsFile(), raConfigFile()}) {
        if (DirEntry::exists(file + ".bak")) {
            PLOG_INFO << "putting back " << file << " from the .bak an older launcher left";
            DirEntry::copy(file + ".bak", file);
            DirEntry::removeFile(file + ".bak");
        }
    }
}

//*******************************
// LaunchService::prepareRaAppend / restoreAppended
//*******************************
void LaunchService::prepareRaAppend(PsGame *game) {
    ConfigFileEditor::CfgLines raConfig, coreOptions;
    auto set = [](ConfigFileEditor::CfgLines &lines, const string &key, const string &value) {
        lines.emplace_back(key, key + " = \"" + value + "\"");
    };
    set(raConfig, "config_save_on_exit", config_.inifile.values["rapersist"] == "false" ? "false" : "true");
    if (game != nullptr && config_.inifile.values["raconfig"] == "true")
        raSettingsFor(*game, raConfig, coreOptions);

    DirEntry::createDirs(Env::getPathToRuntimeDir());
    if (!coreOptions.empty()) {
        // RetroArch's own core options, the game's on top, in RAM for this run
        string text;
        DirEntry::readFile(raCoreOptionsFile(), text); // none yet: the game's alone
        DirEntry::writeFileIfChanged(raRuntimeCoreOptionsFile(), text);
        ConfigFileEditor().replaceProperties(raRuntimeCoreOptionsFile(), coreOptions);
        set(raConfig, "core_options_path", raRuntimeCoreOptionsFile());
    }

    string append;
    for (const auto &line : raConfig)
        append += line.second + "\n";
    DirEntry::writeFileIfChanged(raAppendFile(), append);

    // what retroarch.cfg says of each of them now, for restoreAppended()
    string before;
    DirEntry::readFile(raConfigFile(), before);
    raAppended_ = raConfig;
    raOriginal_.clear();
    for (const auto &line : raConfig) {
        string value;
        raOriginal_.emplace_back(line.first, ConfigFileEditor::valueIn(before, line.first, &value)
                                                 ? line.first + " = \"" + value + "\""
                                                 : string());
    }
}

void LaunchService::restoreAppended() {
    string after;
    if (raAppended_.empty() || !DirEntry::readFile(raConfigFile(), after))
        return;
    ConfigFileEditor::CfgLines restore;
    for (size_t i = 0; i < raAppended_.size(); i++) {
        string ours, now;
        ConfigFileEditor::valueIn(raAppended_[i].second, raAppended_[i].first, &ours);
        // RetroArch saved our value into its file: the file's own value back (none: the line goes). A value
        // the player set in RetroArch meanwhile is theirs and stays.
        if (ConfigFileEditor::valueIn(after, raAppended_[i].first, &now) && now == ours)
            restore.push_back(raOriginal_[i]);
    }
    // writes nothing when RetroArch did not save - every line is then as it was
    if (!restore.empty())
        ConfigFileEditor().replaceProperties(raConfigFile(), restore);
    raAppended_.clear();
}

//*******************************
// LaunchService::raSettingsFor
//*******************************
void LaunchService::raSettingsFor(PsGame &game, ConfigFileEditor::CfgLines &raConfig,
                                  ConfigFileEditor::CfgLines &coreOptions) {
    auto set = [](ConfigFileEditor::CfgLines &lines, const string &key, const string &value) {
        lines.emplace_back(key, key + " = \"" + value + "\"");
    };

    if (!game.foreign) {
        // the game's values as the emulator would see them: its own config over pcsx.cfg
        auto value = [&game](const char *key) { return PcsxConfig::value(game, key); };

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
        set(coreOptions, "pcsx_rearmed_neon_enhancement_enable", highres != 0 ? "enabled" : "disabled");
        set(coreOptions, "pcsx_rearmed_dithering", dither != 0 ? "enabled" : "disabled");
        set(coreOptions, "pcsx_rearmed_neon_enhancement_no_main", speedhack != 0 ? "enabled" : "disabled");
        set(coreOptions, "pcsx_rearmed_psxclock", to_string(clock));
        set(coreOptions, "pcsx_rearmed_show_bios_bootlogo", bootLogo ? "enabled" : "disabled");
        set(coreOptions, "pcsx_rearmed_nocdaudio", "enabled");
        static const char *const interpolations[] = {"off", "simple", "gaussian", "cubic"};
        if (interpolation >= 0 && interpolation <= 3)
            set(coreOptions, "pcsx_rearmed_spu_interpolation", interpolations[interpolation]);
        set(coreOptions, "pcsx_rearmed_frameskip", to_string(frameskip));

        if (scanlines == 1) {
            float opacity = scanline_level / 100.0f;
            set(raConfig, "input_overlay", ":/overlay/scanlines.cfg");
            set(raConfig, "input_overlay_enable", "true");
            set(raConfig, "input_overlay_opacity", to_string(opacity));
        }
    }

    // retroarch.cfg: 1280x720 for widescreen (config.ini aspect=true), 960x720 centred for 4:3
    bool wide = config_.inifile.values["aspect"] == "true";
    set(raConfig, "custom_viewport_width", wide ? "1280" : "960");
    set(raConfig, "custom_viewport_height", "720");
    set(raConfig, "custom_viewport_x", wide ? "0" : "160");
    set(raConfig, "custom_viewport_y", "0");
    set(raConfig, "aspect_ratio_index", wide ? "23" : "0");

    // a PS1 game's own filter (its pcsx.cfg): RetroArch smooths or it does not - Sharp is Off here, as in the
    // classic pcsx-ab. A foreign game has no pcsx.cfg and keeps RetroArch's own video_smooth.
    if (!game.foreign)
        set(raConfig, "video_smooth", filterModeFor(game) == 1 ? "true" : "false");
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
