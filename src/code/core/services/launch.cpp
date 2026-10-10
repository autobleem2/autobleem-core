//
// LaunchService: what App::launchGame and the three EmuInterceptors used to do between them.
//
#include "launch.h"
#include "output_mode.h"
#include "app_manifest.h"
#include "app_settings.h"
#include "environment.h"
#include "../main.h"
#include "system.h"
#include "game_settings.h"
#include "pcsx_config.h"

#include <ableem/engine/config_file_editor.h>

#include <algorithm>
#include <cmath>
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

// config.ini "scaler" (Options -> "Emulator screen scaling") as pcsx-abnxt's g_scaler, the order of its menu's
// Scaler (SCALE_1_1 .. SCALE_FULLSCREEN); -1 for anything else
int scalerIndex(const string &token) {
    static const char *const tokens[] = {"1x1", "2x", "4:3", "4:3i", "full"};
    for (int i = 0; i < 5; i++)
        if (token == tokens[i])
            return i;
    return -1;
}

const char *const RaNeonCore = "NEON";
const char *const RaPeopsCore = "PEOPS";
const char *const PcsxNeonGpu = "builtin_gpu";

} // namespace

//*******************************
// LaunchService::recordLastPlayed
//*******************************
void LaunchService::recordLastPlayed(PsGame &game) {
    if (!Env::clockIsSet()) {
        PLOG_INFO << "clock not set yet - leaving \"" << game.title << "\"'s last played time as it was";
        return;
    }
    library_.updateDatePlayed(game, time(nullptr));
}

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

//*******************************
// LaunchService::pcsxSupportsPadOrder
//*******************************
bool LaunchService::pcsxSupportsPadOrder() const {
    const vector<string> features = pcsxFeatures();
    return std::find(features.begin(), features.end(), "padorder") != features.end();
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
    // Linear is its bilinear; Nearest, and every filter it does not have (Sharp and on), its nearest
    return mode == 1 ? "0" : "1";
}

int LaunchService::filterModeFor(const PsGame &game) {
    // the game's own config's filter when it has one: pcsx-abnxt would keep that one over -filter anyway,
    // the classic pcsx-ab only knows -filter. Hex in the file, as pcsx-abnxt writes it
    int mode = strtol(PcsxConfig::value(game, "plat_target.hwfilter").c_str(), nullptr, 16);
    return mode < 0 || mode >= GameSettingsService::FilterCount ? 0 : mode;
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
// A multi-platform App (autobleem-main docs/archive/app-format-plan.md): its app.ini resolved for this
// machine by AppManifest. Through a script (the console, the appliances): the App's own Startup= script
// when it has one, else the generic rc/app_run.sh - either sources rc/app_env.sh and execs $AB_APP_EXEC,
// which is what the ini names
// for this platform. Direct (Windows, no sh): the resolved program itself, with its Args=. Both get the
// AB_APP_* variables and the ini's Env=. An App of the old kind (Startup= only) is run as it always was where
// there is a shell; direct, it has nothing to run (AppManifest says so, and the Apps set does not list it).
LaunchPlan LaunchService::planApp(const PsGame &game, const PackageEntry *package) {
    LaunchPlan plan;
    AppManifest m = AppManifest::load(game.base, "app.ini", Env::appPlatformKeys());
    if (Env::directLaunch() && !m.runnable()) {
        PLOG_WARNING << "App " << game.base << " cannot run here: " << m.problem;
        return plan;
    }
    if (!m.runnable() || m.legacyStartup) {
        plan.exe = game.base + sep + game.startup;
        return plan;
    }

    plan.env = appEnvironment(m, package);
    plan.cwd = game.base;
    if (Env::directLaunch()) {
        plan.exe = m.program;
        plan.args = AppManifest::splitArgs(m.args);
        if (package != nullptr) {
            // per argument, after the split: a path with blanks stays one argument
            for (string &arg : plan.args)
                arg = expandPackage(arg, *package);
        }
        // the App's own libraries first, then the launcher's folder: its SDL2.dll is the one every App shares
        // (autobleem-main docs/decisions.md, "Third-party App ports"), as the launcher's SDL2 is on the console
        string path = m.libDir;
        for (const string &dir : {Env::executableDir(), string(getenv("PATH") != nullptr ? getenv("PATH") : "")}) {
            if (!dir.empty())
                path += (path.empty() ? "" : ";") + dir;
        }
        plan.env.emplace_back("PATH", path);
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

namespace {

// the value of a placeholder name ("package", "package_dir" ...); false for any other name
bool packageValue(const PackageEntry &p, const string &name, string &value) {
    if (name == "package")
        value = p.file();
    else if (name == "package_dir")
        value = p.root;
    else if (name == "package_kind")
        value = p.game.kind;
    else if (name == "package_title")
        value = p.game.title;
    else if (name == "package_id")
        value = p.id();
    else if (name == "package_game")
        value = p.game.id;
    else
        return false;
    return true;
}

// the shell's view of one argument: bare when it is plain (and was not quoted in the line), else in double quotes with
// \ " $ and ` escaped
string quoteArgument(const string &value, bool wasQuoted) {
    static const string plain = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./:=+,@%-";
    if (!wasQuoted && !value.empty() && value.find_first_not_of(plain) == string::npos)
        return value;
    string out = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"' || c == '$' || c == '`')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

} // namespace

string LaunchService::expandPackage(const string &text, const PackageEntry &package) {
    string out;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '{') {
            const size_t close = text.find('}', i);
            string value;
            if (close != string::npos && packageValue(package, text.substr(i + 1, close - i - 1), value)) {
                out += value;
                i = close + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

string LaunchService::expandPackageArgs(const string &args, const PackageEntry &package) {
    string out;
    size_t i = 0;
    while (i < args.size()) {
        if (args[i] == ' ' || args[i] == '\t') {
            out += args[i++];
            continue;
        }
        // one argument: up to a blank outside double quotes
        const size_t begin = i;
        bool quoted = false;
        string value;
        while (i < args.size() && (quoted || (args[i] != ' ' && args[i] != '\t'))) {
            if (args[i] == '"')
                quoted = !quoted;
            else
                value += args[i];
            i++;
        }
        const string raw = args.substr(begin, i - begin);
        const string expanded = raw.find('{') == string::npos ? value : expandPackage(value, package);
        out += expanded == value ? raw : quoteArgument(expanded, raw.find('"') != string::npos);
    }
    return out;
}

vector<pair<string, string>> LaunchService::appEnvironment(const AppManifest &m, const PackageEntry *package) {
    string keys;
    for (const string &k : Env::appPlatformKeys())
        keys += (keys.empty() ? "" : " ") + k;
    vector<pair<string, string>> env{
        {"AB_ROOT", Env::getPathToUSBRoot()},
        {"AB_RC_DIR", Env::getPathToRCDir()},
        {"AB_APP_DIR", m.folder},
        {"AB_APP_EXEC", m.program},
        {"AB_APP_ARGS", package != nullptr ? expandPackageArgs(m.args, *package) : m.args},
        {"AB_APP_LIB", m.libDir},
        {"AB_APP_KEY", m.key},
        {"AB_PLATFORM", Env::buildTargetKey()},
        {"AB_PLATFORM_KEYS", keys},
        {"AB_APP_VIRTUAL_PAD", m.usesVirtualPad() ? "1" : "0"},
        // the player's Game settings choice, else the ini's PadMode=, else "" (old behaviour)
        {"AB_APP_PAD_MODE", AppSettings::effectivePadMode(AppSettings::padModeOverride(m.folder), m.value("padmode"))},
        // the d-pad / stick flags the same way: the choice, else the ini's, else ""
        {"AB_APP_DPAD2ANALOG",
         AppSettings::effectiveFlag(AppSettings::flagOverride(m.folder, AppSettings::Dpad2AnalogKey),
                                    m.value("dpad2analog"))},
        {"AB_APP_ANALOG2DPAD",
         AppSettings::effectiveFlag(AppSettings::flagOverride(m.folder, AppSettings::Analog2DpadKey),
                                    m.value("analog2dpad"))}};
    if (package != nullptr) {
        // what the engine is started with (docs/packages.md 5.2); the root has no trailing slash
        env.emplace_back("AB_PKG_DIR", package->root);
        env.emplace_back("AB_PKG_FILE", package->file());
        env.emplace_back("AB_PKG_KIND", package->game.kind);
        env.emplace_back("AB_PKG_TITLE", package->game.title);
        env.emplace_back("AB_PKG_ID", package->id());
        env.emplace_back("AB_PKG_GAME", package->game.id);
        if (!package->game.starts.empty()) {
            string starts;
            for (const PackageStart &s : package->game.starts)
                starts += (starts.empty() ? "" : ";") + s.file + "|" + s.title;
            env.emplace_back("AB_PKG_STARTS", starts);
        }
        for (const auto &setting : package->game.settings)
            env.emplace_back("AB_PKG_SET_" + toUpperCopy(setting.first), setting.second);
        if (!package->game.mapper.empty())
            env.emplace_back("AB_PKG_MAPPER", package->mapperFile());
    }
    for (const auto &kv : m.env)
        env.emplace_back(kv.first, package != nullptr ? expandPackage(kv.second, *package) : kv.second);
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
void LaunchService::launch(PsGamePtr &game, EmuMode mode, int resumePoint, const PackageEntry *package) {
    if (game->package) {
        // a Packages row entry is game data, not a program (docs/packages.md 7): the screens open its info view
        PLOG_ERROR << "LaunchService: refusing to launch the package " << game->title;
        return;
    }
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
        // C11: Options -> "Swap Player 1 / Player 2" - a positional swap of the first two SDL pads' PS1
        // ports. Only sent when this emulator's abfeatures declares it understands the token; an emulator
        // without it gets nothing and keeps its own ascending-index order, and the launcher is told so it
        // can say so (Session::padOrderUnsupportedNotice) instead of silently claiming a swap that did not
        // happen.
        const bool padSwapWanted = config_.inifile.values["padswap"] == "true";
        if (padSwapWanted && has("padorder"))
            env.emplace_back("AB_PAD_ORDER", "1,0");
        else if (padSwapWanted)
            session_.padOrderUnsupportedNotice = true;
        // Options -> Display (OutputMode): the emulator opens in the launcher's mode instead of switching the
        // display to its own; a change the player makes in its menu comes back in <runtime>/outputmode, which
        // AutoBleem::runOutside takes into config.ini after the game
        if (has("outputmode")) {
            env.emplace_back("AB_OUTPUT_MODE",
                             OutputMode::parse(config_.inifile.values[OutputMode::ConfigKey]).token());
            DirEntry::removeFile(OutputMode::emulatorFile()); // nothing left over from an earlier game
        }
        // the CRT safe margin (percent per side) the emulator keeps its menu and HUD inside
        if (has("crtmargin")) {
            // a VGA 4:3 mode has its own setting (0 unless set); the tube and anything else the CRT one, as before
            const OutputMode shown = OutputMode::parse(config_.inifile.values[OutputMode::ConfigKey]);
            const int margin = shown.is43() && !shown.isCrt()
                                   ? OutputMode::vgaMargin(config_.inifile.values[OutputMode::VgaMarginKey])
                                   : OutputMode::crtMargin(config_.inifile.values[OutputMode::MarginKey]);
            env.emplace_back("AB_CRT_MARGIN", to_string(margin));
        }
        // the picture height adjust (output pixels, -40..40 even, one value for every 4:3 output) the emulator shows its
        // picture with, when it knows it
        if (has("crtvsize"))
            env.emplace_back("AB_CRT_VSIZE", to_string(OutputMode::vsize(config_.inifile.values[OutputMode::VsizeKey])));
        // Options -> Diagnostics -> "Show performance": the emulator's HUD shows its FPS and CPU as well, for
        // this run only (it keeps them out of the game's saved config)
        if (has("perfoverlay") && config_.inifile.values["perfoverlay"] == "true")
            env.emplace_back("AB_PERF_OVERLAY", "1");
        // Options -> "Emulator screen scaling": any of the emulator's own Scaler values; one without the
        // feature (the classic pcsx-ab) gets -ratio's full/4:3 only
        const int scaler = scalerIndex(config_.inifile.values["scaler"]);
        if (has("scaler") && scaler >= 0)
            env.emplace_back("AB_SCALER", to_string(scaler));
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
        launchRetroArch(*game, resumePoint);
        raMemcardOut(*game);
        break;

    case Path::App:
        launchApp(*game, package);
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

    recordLastPlayed(game);

    string lastCDpoint = game.ssFolder + sep + "lastcdimg.txt";
    string lastCDpointX = game.ssFolder + sep + "lastcdimg." + to_string(resumePoint) + ".txt";
    string gameFile = "";

    string aspect = "0";
    if (config_.inifile.values["scaler"] == "full") { // -ratio 1 fills the screen; the rest is 4:3 here
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
void LaunchService::launchRetroArch(PsGame &game, int resumePoint) {
    PLOG_INFO << "calling LaunchService::launchRetroArch()";

    // one of our own games: a playlist entry's gameId is only its index in the playlist, and would name
    // some unrelated row in regional.db
    if (!game.foreign) {
        recordLastPlayed(game);
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
    // the game's own save-state slots (a playlist entry whose core can save): the slot to resume is put in place
    // now, and ra-append.cfg tells RetroArch where its states are, to write one on the way out and whether to load
    raStates_ = RaStates();
    raGameOptions_ = raOptions_ != nullptr ? raOptions_->get(game) : RaGameOptions();
    if (game.foreign && !game.app && resumePoints_.raSupportsStates(game)) {
        raStates_.active = true;
        // RetroArch does not make its savestate folder: without it the first game of a fresh stick writes no state
        DirEntry::createDirs(ResumePointService::raStatesDir());
        raStates_.save = raGameOptions_.resume != RaGameOptions::ResumeNever;
        raStates_.load = resumePoints_.prepareRaLaunch(game, resumePoint);
    }
    prepareRaAppend(&game);
    runner_.run(planRetroArch(gameFile, RACore));
    usleep(3 * 1000);
    restoreAppended();
    raStates_ = RaStates();
    raGameOptions_ = RaGameOptions();
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
        memcards_.noteSessionCard(game, outpath); // the session's saves are in the .srm until raMemcardOut
    }
}

//*******************************
// LaunchService::raMemcardOut
//*******************************
void LaunchService::raMemcardOut(PsGame &game) {
    if (!game.foreign) {
        // the session's card onto the swapped-in card first, and only then the set swapped out: the other way round
        // the set got its cards back unchanged and the session's saves landed on the game's own card instead
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

        memcards_.swapOutAfterLaunch(game); // also clears the crash journal
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
// LaunchService::raScanlinesOverlay
//*******************************
// The scanlines overlay is shipped with the launcher (resources/overlay/) and named by its full path: ":/" is the
// RetroArch binary's folder, which holds no overlay on a Pi or a PC stick (/usr/local/bin). A launcher without the
// file (an old install) keeps the console's RetroBoot-era ":/overlay/scanlines.cfg".
string LaunchService::raScanlinesOverlay() {
    const string shipped = Env::getWorkingPath() + sep + "overlay" + sep + "scanlines.cfg";
    return DirEntry::exists(shipped) ? shipped : string(":/overlay/scanlines.cfg");
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
    // RetroArch's per-game play-time logs (playlists/logs/<core>/<game>.lrtl), written at every exit - nothing
    // of ours reads them (measured on the Pi 400: a write per RetroArch game)
    set(raConfig, "content_runtime_log", "false");
    set(raConfig, "content_runtime_log_aggregate", "false");
    const OutputMode mode = OutputMode::parse(config_.inifile.values[OutputMode::ConfigKey]);
#ifndef AB_PLATFORM_PSC
    // Options -> Display: RetroArch full screen in the launcher's mode (0 = the display's own); on the console
    // the mode is Weston's, whatever RetroArch asks for
    set(raConfig, "video_fullscreen_x", to_string(mode.isAuto() ? 0 : mode.w));
    set(raConfig, "video_fullscreen_y", to_string(mode.isAuto() ? 0 : mode.h));
#endif
    if (game != nullptr && config_.inifile.values["raconfig"] == "true")
        raSettingsFor(*game, raConfig, coreOptions);
    if (game != nullptr)
        RaOptionsService::apply(raGameOptions_, raScanlinesOverlay(),
                                raConfig); // the game editor's rows, over the scaler and the like
    if (mode.isCrt())
        raCrtSettings(raConfig); // the 720x480 tube: 3:2 pixels at 8:9, the margin for the messages, no shaders
    raMenuSettings(mode, raConfig); // the menu for the mode shown now, whatever an earlier mode left in the file
    if (raStates_.active) {
        // our slots (ResumePointService): RetroArch writes <game>.state.auto + picture when it ends and reads the
        // state the launcher put there only when asked to. The folder and the sorting are pinned so the file is
        // exactly where the launcher looks, whatever the stick's retroarch.cfg says
        set(raConfig, "savestate_directory", ResumePointService::raStatesDir());
        set(raConfig, "sort_savestates_enable", "false");
        set(raConfig, "sort_savestates_by_content_enable", "false");
        set(raConfig, "savestates_in_content_dir", "false");
        set(raConfig, "savestate_auto_save", raStates_.save ? "true" : "false");
        set(raConfig, "savestate_thumbnail_enable", "true");
        set(raConfig, "savestate_auto_load", raStates_.load ? "true" : "false");
    }

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

// A key of `lines` set to `value`: its line replaced, else added - none appears twice in the append file
static void replaceLine(ConfigFileEditor::CfgLines &lines, const string &key, const string &value) {
    const string line = key + " = \"" + value + "\"";
    for (auto &existing : lines) {
        if (existing.first == key) {
            existing.second = line;
            return;
        }
    }
    lines.emplace_back(key, line);
}

// RetroArch on the CRT 4:3 mode (720x480, pixels 8:9, the safe margin): the 4:3 and the core's own picture are 1.5 on
// the screen (aspect_ratio_index 20, "config": video_aspect_ratio - the 720x480 frame the tube shows at pixel aspect
// 8:9 is 4:3 in the end), the custom viewport's "full" becomes the real Full (24), no shader and no integer scale
// (both would fight the anamorphic frame), 59.94 Hz, and RetroArch's own messages inside the margin. Over what
// raSettingsFor and the game's rows put in `lines`; the keys stay in raAppended_, so restoreAppended() puts them back
void LaunchService::raCrtSettings(ConfigFileEditor::CfgLines &lines) {
    string aspect;
    for (const auto &line : lines)
        if (line.first == "aspect_ratio_index")
            ConfigFileEditor::valueIn(line.second, line.first, &aspect);
    if (aspect == "0" || aspect == "22") { // 4:3, or what the core reports
        replaceLine(lines, "aspect_ratio_index", "20");
        replaceLine(lines, "video_aspect_ratio", "1.500000");
    } else if (aspect == "23") { // the custom viewport (raSettingsFor's "full"): the whole screen
        replaceLine(lines, "aspect_ratio_index", "24");
    }
    replaceLine(lines, "video_shader_enable", "false");
    replaceLine(lines, "video_scale_integer", "false");
    replaceLine(lines, "video_refresh_rate", "59.940000");
    const double message = 0.05 + OutputMode::crtMargin(config_.inifile.values[OutputMode::MarginKey]) / 100.0;
    replaceLine(lines, "video_message_pos_x", to_string(message));
    replaceLine(lines, "video_message_pos_y", to_string(message));
}

// RetroArch's menu for the mode shown, at every launch (the owner, 2026-10-11: "przy kazdej zmianie zaleznie od
// wybranej rozdzielczosci"): the RetroArch patch's pixel aspect and safe margin (an older RetroArch ignores them) and
// the menu scale. The tube: pixels 8:9 inside the CRT margin, the menu bigger (XMB comes out small inside a 5 % margin,
// device round); a VGA 4:3 mode: square pixels inside its own margin; HD: the whole screen. Written for every mode,
// so a mode change never keeps what an earlier one left in retroarch.cfg (the tube's margin stayed on 720p).
void LaunchService::raMenuSettings(const OutputMode &mode, ConfigFileEditor::CfgLines &lines) {
    int margin = 0;
    if (mode.isCrt())
        margin = OutputMode::crtMargin(config_.inifile.values[OutputMode::MarginKey]);
    else if (mode.is43())
        margin = OutputMode::vgaMargin(config_.inifile.values[OutputMode::VgaMarginKey]);
    // in RetroArch's own format, so the value it saves back is the same string (restoreAppended)
    replaceLine(lines, "menu_pixel_aspect", mode.isCrt() ? "0.888889" : "1.000000");
    replaceLine(lines, "menu_scale_factor", mode.isCrt() ? "1.300000" : "1.000000");
    replaceLine(lines, "menu_safe_margin", to_string(margin));
}

// the same setting in two spellings: RetroArch saves a float as "1.300000" whatever it was given ("1.3")
static bool sameValue(const string &a, const string &b) {
    if (a == b)
        return true;
    if (a.empty() || b.empty())
        return false;
    char *endA = nullptr, *endB = nullptr;
    const double x = strtod(a.c_str(), &endA), y = strtod(b.c_str(), &endB);
    return *endA == '\0' && *endB == '\0' && fabs(x - y) < 1e-4;
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
        if (ConfigFileEditor::valueIn(after, raAppended_[i].first, &now) && sameValue(now, ours))
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
        // the game editor's Dithering row, pcsx-abnxt's key (no line = on where the game asks, its default);
        // the core option is on or off, so "always" is on
        string ditherLine = value("dithering2");
        int dither = ditherLine.empty() ? 1 : strtol(ditherLine.c_str(), nullptr, 16);
        int interpolation = strtol(value("spu_config.iUseInterpolation").c_str(), nullptr, 16);

        // 0 off, 1..3 how thick in pcsx-abnxt: RetroArch's overlay is one thickness
        int scanlines = strtol(value("scanlines").c_str(), nullptr, 16);
        int scanline_level = strtol(value("scanline_level").c_str(), nullptr, 16);
        // the emulators' setting (0 Auto, 1 Off, 2..4 skip 1..3; no line = Off), as the core's type + interval
        string skipLine = value("frameskip3");
        int frameskip = skipLine.empty() ? 1 : strtol(skipLine.c_str(), nullptr, 16);
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
        set(coreOptions, "pcsx_rearmed_frameskip_type",
            frameskip == 0                     ? "auto"
            : frameskip >= 2 && frameskip <= 4 ? "fixed_interval"
                                               : "disabled");
        if (frameskip >= 2 && frameskip <= 4)
            set(coreOptions, "pcsx_rearmed_frameskip_interval", to_string(frameskip - 1));

        if (scanlines != 0) {
            float opacity = scanline_level / 100.0f;
            set(raConfig, "input_overlay", raScanlinesOverlay());
            set(raConfig, "input_overlay_enable", "true");
            set(raConfig, "input_overlay_opacity", to_string(opacity));
        }
    }

    // retroarch.cfg: 1280x720 for "full" scaling (config.ini scaler), 960x720 centred for the rest - not on the CRT
    // 4:3 mode, whose output is not 16:9 (raCrtSettings turns the index into the 4:3 / Full ones)
    bool wide = config_.inifile.values["scaler"] == "full";
    if (!OutputMode::parse(config_.inifile.values[OutputMode::ConfigKey]).isCrt()) {
        set(raConfig, "custom_viewport_width", wide ? "1280" : "960");
        set(raConfig, "custom_viewport_height", "720");
        set(raConfig, "custom_viewport_x", wide ? "0" : "160");
        set(raConfig, "custom_viewport_y", "0");
    }
    set(raConfig, "aspect_ratio_index", wide ? "23" : "0");

    // a PS1 game's own filter (its pcsx.cfg): RetroArch smooths or it does not - only Linear smooths, Sharp
    // and the rest are nearest here, as in the classic pcsx-ab. A foreign game has no pcsx.cfg and keeps
    // RetroArch's own video_smooth.
    if (!game.foreign)
        set(raConfig, "video_smooth", filterModeFor(game) == 1 ? "true" : "false");
}

//*******************************
// LaunchService::launchApp
//*******************************
void LaunchService::launchApp(PsGame &game, const PackageEntry *package) {
    PLOG_INFO << "calling LaunchService::launchApp()";
    PLOG_INFO << "Starting External App";

    recordLastPlayed(game);

    if (game.foreign) {
        PLOG_INFO << "FOREIGN MODE";
    }

    LaunchPlan plan = planApp(game, package);
    if (plan.exe.empty())
        return;
    runner_.run(plan);
    usleep(3 * 1000);
}
