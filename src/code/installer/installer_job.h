//
// InstallerJob: AutoBleem onto a PlayStation Classic stick from a PC - the job behind the installer window
// and its --quiet line. The stick's file system comes from the package next to the program
// (autobleem-psc-<version>.tar.gz, the release's tarball: the launcher, pcsx-ab, the scripts, the themes,
// the console tools); everything else from the download repository, as asked: the cover databases (db/),
// RetroArch with its cores, libraries, apps and libretro's asset bundles (psc/*, buildbot.libretro.com),
// the BIOS files by the site's list (psc/bios -> RetroBIOS), the sample games (samples/).
//
// An update is the same run over a stick that has AutoBleem already: what the package ships is replaced
// (Autobleem/, the two console tools under Apps/, the shipped themes, Docs/), everything the user has -
// games, save states, memory cards, System/, RetroArch's saves, playlists and thumbnails, other apps and
// themes - is left as it is, and config.ini keeps the user's settings.
//
// No SDL and nothing of the launcher's UI: ab_core only, a Downloader the program provides (WinINet on
// Windows, a fake in the tests), a listener for the phases, the progress and the lines.
//
#pragma once

#include <ableem/engine/update_catalog.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

//******************
// InstallOptions
//******************
struct InstallOptions {
    std::string root;        // the stick's root: "F:" / "F:/" / "/media/me/SONY"
    std::string packageFile; // autobleem-psc-<version>.tar.gz; "" = the channel's, downloaded
    // the download site's channel the stick package (psc-fs) and UpdateRoms come from when packageFile is
    // empty: "release" (releases/latest.json), "testing" (releases/unstable.json, else the release),
    // "nightly" (nightly/latest.json, else testing's, else the release's)
    std::string channel;
    // the lists of that channel when channels.json named them (ChannelCatalog::lists); empty = channelLists(channel)
    std::vector<std::string> channelIndexes;
    std::string repoUrl = "https://autobleem.retromenele.pl";
    std::string buildbotUrl = "https://buildbot.libretro.com/assets/frontend"; // libretro's bundles
    std::string scratchDir; // where downloads land before they are unpacked; "" = <root>/System/Install
    bool coversJapan = true;
    bool coversUsa = true;
    bool coversPal = true;
    bool retroarch = false;
    bool bios = false; // needs retroarch (or RetroArch already on the stick)
    bool samples = false;
    // the console's own RetroArch update: a retroarch-psc-<v>.zip already downloaded and checked. The run then
    // does only that - the zip laid over the RetroArch that is on the stick (what the "RetroArch" phase does
    // with a new build, minus the cores, libraries, apps and bundles); packageFile and channel are not used
    std::string retroarchZip;
};

//******************
// StickInfo
//******************
// what InstallerJob::inspect finds: is this an update, of what
struct StickInfo {
    bool isStick = false;                      // the root exists and can be written to
    bool installed = false;                    // Autobleem/bin/autobleem/autobleem-gui is there
    bool legacyLayout = false;                 // an AutoBleem 1.0 / NG / RetroBoot stick: retroarch/, roms/ at the root
    std::string installedVersion;              // the stick's VERSION file, "" when none
    bool hasRetroArch = false;                 // RetroArch/bin/retroarch
    std::string retroarchVersion;              // RetroArch/bin/VERSION
    bool hasCovers[3] = {false, false, false}; // J, U, P
    std::string packageVersion;                // the package's VERSION entry
    std::string error;
};

//******************
// Downloader
//******************
// fetches one URL into a file; the progress callback (bytes so far, total or 0 when unknown) returns
// false to abort. `error` says why on a failure. Never throws.
class Downloader {
public:
    using Progress = std::function<bool(uint64_t done, uint64_t total)>;
    virtual ~Downloader() = default;
    virtual bool fetch(const std::string &url, const std::string &destFile, const Progress &progress,
                       std::string &error) = 0;
    // what the next fetch() should come to (size 0 / sha256 "" = not known). A downloader that checks the file
    // itself (a resumed download needs the size to know it is whole) says so by returning true: its failure
    // then starts with ErrChecksum and the caller does not hash the file a second time.
    virtual bool expect(uint64_t size, const std::string &sha256) {
        (void)size;
        (void)sha256;
        return false;
    }
    // the size of the file at `url` from a HEAD request (Content-Length), written through `scratchFile`; false when
    // this downloader cannot ask or the server did not say
    virtual bool contentLength(const std::string &url, const std::string &scratchFile, uint64_t &size) {
        (void)url;
        (void)scratchFile;
        (void)size;
        return false;
    }
    // how a failed fetch's `error` starts when the machine has no network / the file is not what was published
    static constexpr const char *ErrNoNetwork = "no network";
    static constexpr const char *ErrChecksum = "checksum";
    // a small text file (a latest.json, a .sha256 sidecar)
    bool fetchText(const std::string &url, const std::string &scratchFile, std::string &text, std::string &error);
};

//******************
// ChannelRelease
//******************
// what a channel of the download site offers a stick: the version, the stick package, UpdateRoms
struct ChannelRelease {
    std::string channel;
    std::string version;
    ableem::UpdateFile package;    // psc-fs: autobleem-psc-<version>.tar.gz
    ableem::UpdateFile updateRoms; // name "" when the release has none
};

//******************
// InstallListener
//******************
class InstallListener {
public:
    virtual ~InstallListener() = default;
    virtual void onPhase(int index, int total, const std::string &title) = 0; // 1-based
    virtual void onProgress(uint64_t done, uint64_t total) = 0;               // within the phase; 0/0 = unknown
    virtual void onLine(const std::string &line) = 0;
    // the package the phase is on now: its name, which one of how many in the phase, and what onProgress counts
    // from here on ("bytes", "files" or "items"). Resets the phase's done/total. A listener that does not show
    // packages ignores it.
    virtual void onItem(const std::string &name, int index, int count, const std::string &unit) {
        (void)name;
        (void)index;
        (void)count;
        (void)unit;
    }
};

//******************
// RetroArchOptions / RetroArchResult
//******************
// The RetroArch-only jobs for the PSC layout (<root>/RetroArch/bin) - InstallerJob::retroarchInstall /
// retroarchRemove, what `abupdate <ROOT> --retroarch install|remove [--bios] --progress <FILE>` runs.
// The one-call wiring is RetroArchRunner::run (installer/retroarch_runner.h): it builds the progress file,
// the downloader and the stop flag and returns the process exit code.
struct RetroArchOptions {
    std::string root; // the PSC's USB root ("/media", "F:/")
    std::string repoUrl = "https://autobleem.retromenele.pl";
    std::string buildbotUrl = "https://buildbot.libretro.com/assets/frontend";
    std::string scratchDir; // "" = <root>/System/Install/retroarch (kept on a stop or an error, for the resume)
    // free and total bytes of the filesystem `path` is on; empty = System::diskSpace
    std::function<bool(const std::string &path, uint64_t &freeBytes, uint64_t &totalBytes)> diskSpace;
    // a RetroArch the distribution's package manager owns (the Pi's /usr/bin/retroarch): remove refuses it
    // with FailKind::AptProvided when the root has no RetroArch of ours. "" = none (the PSC)
    std::string aptBinary;
};

// why a run failed; the progress file's end|error|<code> (RetroArchRunner::errorCode)
enum class FailKind { None, NoNetwork, NoSpace, Download, Checksum, AptProvided, Other };

struct RetroArchResult {
    bool ok = false;
    bool stopped = false; // SIGTERM / the stop flag: scratch files kept, a second install resumes
    FailKind kind = FailKind::None;
    std::string error;
};

//******************
// InstallerJob
//******************
class InstallerJob {
public:
    using ShouldStop = std::function<bool()>;

    // RetroArch alone (also the update: a newer remote version replaces the old one). Phases, in order, titled
    // with the contract's keys: check-space, download-retroarch, download-cores, download-libs, download-apps,
    // download-assets, [download-bios, only with `withBios`], unpack, merge-config, install-binary. Everything
    // lands first; the binary last (retroarch.new, then renamed), then the VERSION stamp. Downloads are kept
    // as .part files and complete files in the scratch folder until the run succeeds, so a stopped or failed
    // run resumes (HTTP Range through the downloader) without fetching a finished file again.
    static RetroArchResult retroarchInstall(const RetroArchOptions &options, bool withBios, Downloader &downloader,
                                            InstallListener &listener, const ShouldStop &shouldStop);
    // Removes the binary (+ VERSION, theme files), then cores, info, assets, database, cheats, overlays,
    // shaders, downloads, records, "Retroarch themes" (each renamed .removing, then deleted), and with
    // `withBios` the BIOS files. Roms, playlists, thumbnails, saves, savestates, screenshots, config/,
    // autoconfig, retroarch.cfg, retroboot/, games, Apps/, Autobleem/lib and (without `withBios`) the BIOS stay.
    // One phase: remove-files.
    static RetroArchResult retroarchRemove(const RetroArchOptions &options, bool withBios, InstallListener &listener,
                                           const ShouldStop &shouldStop);

    // the package next to the program: the newest autobleem-psc-*.tar.gz in argv[0]'s directory, "" if none
    static std::string packageNextTo(const std::string &programPath);
    // the lists a channel reads on the site, in order (its own, then what stands in when it has none)
    static std::vector<std::string> channelLists(const std::string &channel);
    // what the channel offers a stick now - for the window to show before the run, and the run itself
    static bool channelRelease(const std::string &repoUrl, const std::string &channel, Downloader &downloader,
                               const std::string &scratchDir, ChannelRelease &out, std::string &error);
    // the same with the lists given (a channel from channels.json)
    static bool channelRelease(const std::string &repoUrl, const std::string &channel,
                               const std::vector<std::string> &lists, Downloader &downloader,
                               const std::string &scratchDir, ChannelRelease &out, std::string &error);
    // the stick and the package as they are
    static StickInfo inspect(const InstallOptions &options);
    // the phases the options ask for, for a preview (the listener gets the same titles)
    static std::vector<std::string> phasesFor(const InstallOptions &options, const StickInfo &info);

    // the install; false with `error` on the first failure that cannot be gone past (a lost download of
    // one BIOS file is reported and skipped, a bad package is not)
    static bool run(const InstallOptions &options, Downloader &downloader, InstallListener &listener,
                    const ShouldStop &shouldStop, std::string &error);

    // "F:" -> "F:/", "/x/" -> "/x", backslashes forward
    static std::string normalizeRoot(const std::string &root);
};
