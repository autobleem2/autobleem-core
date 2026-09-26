#pragma once

#include <ableem/engine/environment.h>
#include <string>
#include <vector>

//*******************************
// AB_PLATFORM_<TARGET>
//*******************************
// Exactly one is defined by CMake from AB_TARGET (root CMakeLists.txt): AB_PLATFORM_PSC (the console),
// AB_PLATFORM_RPI (the Raspberry Pi appliance), AB_PLATFORM_PCUSB (the 32-bit PC USB-stick appliance),
// AB_PLATFORM_WIN (the Windows product) or AB_PLATFORM_DEV (a development host). The sources never test the
// CPU or the OS to tell them apart; where a build differs, it is by the four macros derived below - each
// named for what it means - or, for a path, by resources/platform/<name>.ini (Env::platformName()).
#if defined(AB_PLATFORM_PSC) + defined(AB_PLATFORM_RPI) + defined(AB_PLATFORM_PCUSB) + defined(AB_PLATFORM_WIN) +      \
        defined(AB_PLATFORM_DEV) !=                                                                                    \
    1
#error "exactly one AB_PLATFORM_* must be defined - configure with -DAB_TARGET=psc|rpi|pcusb|win|dev"
#endif

//*******************************
// AB_DEBUG_HOST
//*******************************
// a development machine: the emulators are not forked (a splash stands in), power off is exit(), free
// space is not measured, the internal games' covers come from the rdb. Only the dev target.
#if defined(AB_PLATFORM_DEV)
#define AB_DEBUG_HOST 1
#endif

//*******************************
// AB_APPLIANCE
//*******************************
// a machine of ours that is not the console: it forks the emulators and halts for real, but has no console
// tree behind it - everything lives on the data partition whose mount point is passed on the command line,
// there are no built-in games, and an update is the installer re-run by the session script (MENU_OPTION_UPDATE).
#if defined(AB_PLATFORM_RPI) || defined(AB_PLATFORM_PCUSB)
#define AB_APPLIANCE 1
#endif

//*******************************
// AB_ROOT_RELATIVE_LAYOUT
//*******************************
// every path comes from a root given on the command line (or found by the Windows product), instead of the
// console's fixed /media + /usr/sony tree. See EnvironmentSetup, the one place that decides the layout.
#if !defined(AB_PLATFORM_PSC)
#define AB_ROOT_RELATIVE_LAYOUT 1
#endif

//*******************************
// AB_HAS_INTERNAL_GAMES
//*******************************
// the console's built-in games (/gaadata, internal.db) can be shown: the console itself, and a dev host
// running against a copy of its database. An appliance or a Windows PC has none.
#if defined(AB_PLATFORM_PSC) || defined(AB_PLATFORM_DEV)
#define AB_HAS_INTERNAL_GAMES 1
#endif

// Every path comes from ableem::Environment, configured once in main() (see setupEnvironment there - that is
// where the debug-host vs console decisions are made). The app only adds its two runtime flags.
struct Environment : ableem::Environment {
    static bool autobleemKernel; // true if the kernel is the AutoBleem Kernel
    static bool hiddenMenuEnabled;

    // the platform this build is for - "psc" (the console), "rpi", "pcusb", "win" or "pc" (a dev host) -
    // which names the resources/platform/<name>.ini that PlatformConfig reads at start. The only place the
    // build macros decide a path.
    static const char *platformName();

    // the platform keys an App's or an extension's binary may be built for, most specific first - what
    // AppManifest resolves Exec.<key>= / Exec=bin/{key}/... against (docs/app-format-plan.md). The built-in
    // list (appPlatformKeysFor(buildTargetKey(), buildOs(), buildArch())) followed by the platform ini's
    // app_platform_keys, which may add keys but never remove one.
    static std::vector<std::string> appPlatformKeys();
    static void setExtraAppPlatformKeys(const std::vector<std::string> &keys);
    // the table itself, for any target: "psc" -> {psc}; "dev" -> {dev, (win on Windows), <os>-<arch>};
    // anything else -> {<target>, <os>-<arch>}
    static std::vector<std::string> appPlatformKeysFor(const std::string &targetKey, const std::string &os,
                                                       const std::string &arch);
    // what this build is: "psc", "rpi", "rpi64", "pcusb", "win" or "dev" (the update's package keys, without
    // their suffixes); the OS ("linux", "windows") and the CPU ("armhf", "arm64", "i386", "x86_64")
    // System/platform_keys: appPlatformKeys() on one line, space separated - what rc/app_resolve.sh reads when
    // an App's run.sh is started by hand, with no launcher to say. Written at start-up, only when it changed.
    static std::string platformKeysFile();
    static void writePlatformKeysFile();
    static const char *buildTargetKey();
    static const char *buildOs();
    static const char *buildArch();

    // where the RetroArch executable may be (PlatformConfig's retroarch_binary, resolved), and whether one
    // of them is there - what "RetroArch" in the system menu and Square on a game check
    static void setRetroArchBinaries(const std::vector<std::string> &paths);
    static const std::vector<std::string> &retroArchBinaries();
    static bool retroArchInstalled();

    // PlatformConfig's download_command: how this platform fetches a URL to a file (%u, %o), "" when it
    // cannot - what OnlineAssets runs
    static void setDownloadCommand(const std::string &command);
    static const std::string &downloadCommand();
    // PlatformConfig's repo_url and update_download_command: the download repository the online update
    // checks, and the command that fetches a package from it (no short timeout); "" = no update here.
    // retroarch_catalog is where that repository lists this platform's RetroArch builds
    // ("rpi/retroarch/latest.json", relative to repo_url); "" = RetroArch is not updated here
    static void setUpdateSource(const std::string &repoUrl, const std::string &downloadCommand,
                                const std::string &retroarchCatalog = "");
    static const std::string &repoUrl();
    static const std::string &updateDownloadCommand();
    static const std::string &retroArchCatalog();
    // PlatformConfig's store_download_command, %r (the launcher's own folder, where abfetch is) replaced: what
    // an extension - the AutoBleem Store - fetches a file with. It continues a partly downloaded %o, so the
    // same command is a fresh download and a resumed one. The update's command when the platform has none.
    static void setStoreDownloadCommand(const std::string &command);
    static std::string storeDownloadCommand();

    // PlatformConfig's launch_mode: "direct" - the launcher starts the emulators itself (the Windows
    // product); otherwise through the rc/launch*.sh scripts (the console, the appliances, a dev host)
    static void setDirectLaunch(bool direct);
    static bool directLaunch();
    // PlatformConfig's pcsx_dir / pcsxnxt_dir: where pcsx-ab and pcsx-abnxt are for a direct launch
    // (resolved, absolute); "" = the scripts know. Options -> "PS1 Emulator" picks between the two
    static void setPcsxDir(const std::string &path);
    static const std::string &pcsxDir();
    static void setPcsxNxtDir(const std::string &path);
    static const std::string &pcsxNxtDir();

    // the gamecontrollerdb.txt files SDL's pad mappings come from, first existing wins: the kernel's
    // (/etc/autobleem on the console - what the pscbios wizard writes when it is there), then the shipped
    // one in the resources dir (what the wizard writes otherwise)
    static std::vector<std::string> padMappingFiles();

    // the version every program shows (the splash, About, Hardware Information, the log banner, the PC
    // programs' windows, and through AB_VERSION the emulators and the console tools the launcher starts):
    // the package's - $AB_VERSION when a parent set it, else the first line of a VERSION file: the data
    // root's (the stick's own), the one next to the program (the installer's, flasher's and Windows
    // product's folders - the assembly writes them), or the one a folder up (<stick>/UpdateRoms/); else this
    // build's git describe. The owner's rule (2026-09-23): everything but RetroArch shows the package's
    // version, written the same way
    static std::string productVersion();
    // the folder the running program is in, "" when unknown
    static std::string executableDir();
    // puts productVersion() into AB_VERSION, so every program started from here inherits it
    static void exportProductVersion();

    // Whether the clock can be trusted right now (2026-09-26): the PSC has no battery-backed clock, and on
    // the AutoBleem kernel it is only set once a network is up (the payload's dhcpcd hook touches
    // clockSetMarkerFile()). Elsewhere (every other target, and the console before it has a network) the
    // system clock is whatever the host gave it, always trusted. A plain stat, re-checked on every call - the
    // clock can be set mid-session - never cached. What LaunchService checks before writing a "last played"
    // time, so a launch before the clock is set does not overwrite a valid earlier one with the epoch.
    static bool clockIsSet();
    // the marker clockIsSet() stats: "" (always set) off the console, "/run/autobleem/clock-set" on it - the
    // one place the path is spelled. Settable for tests; production never calls the setter.
    static std::string clockSetMarkerFile();
    static void setClockSetMarkerFile(const std::string &path);

    // "Keep logs on the stick" (docs/quiet-stick-plan.md): a tester's System/Logs/keep marker - what the rc
    // scripts see before the launcher runs - or config.ini's keeplogs=true (the Options row), or
    // $AB_KEEP_LOGS=1. config.ini on and no marker makes the marker, so the scripts agree from the next boot.
    static std::string keepLogsMarkerFile();
    static bool keepLogsRequested();
    // the Options row: makes or removes the marker (config.ini's keeplogs is the row's own value). Takes
    // effect at the next start - the logs of this one are where they are.
    static void setKeepLogsMarker(bool keep);
    // the crash folder the rc scripts saved since the launcher last looked (System/Logs/crash-<n> with a
    // .new marker - rc/ab_log.sh's ab_persist_logs): its name, the newest when there are several, "" when
    // none. Every .new marker is removed, so each crash is announced once.
    static std::string takeNewCrashLogs();
    // Hardware Information's "Save logs": this run's logs from RAM to System/Logs/saved-<n>/ (the last three
    // kept) - a tester's way to hand over a session that did not crash. Returns the folder's name; "" when
    // the logs are on the stick already (keepLogs()) or nothing could be copied.
    static std::string copyLogsToStick();
    // after setKeepLogs(): makes the logs dir, puts it and the runtime dir into AB_LOG_DIR / AB_RUNTIME_DIR
    // for every program started from here, and writes <runtime>/log_dir for the scripts that are not
    // (rc/selection.sh runs after the launcher has left)
    static void exportLogDirs();
};

using Env = Environment;
