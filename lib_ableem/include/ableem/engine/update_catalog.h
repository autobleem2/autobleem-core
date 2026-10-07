// lib_ableem - engine: what the download repository says is current, and what the launcher remembers
// about updates - the JSON side of the launcher's online update (core/services/update_service.*), kept in
// the engine because the app includes no JSON library of its own.
//
//   releases/latest.json, releases/unstable.json   -> ReleaseCatalog   (tools/repo_index.py writes them)
//   channels.json                                  -> ChannelCatalog  (tools/repo_index.py writes it: the release
//                                                     channels the PC installers offer)
//   rpi/retroarch/latest.json                      -> RetroArchCatalog
//   <usb>/System/update.json                       -> UpdateState     (last check, skipped/postponed)
//   <usb>/System/Updates/pending.json              -> PendingUpdate   (what the launcher downloaded and
//                                                     verified, for the Pi's autobleem-update script)
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ableem {

struct UpdateFile {
    std::string name;
    std::string url;
    std::string sha256;
    uint64_t size = 0;
    bool valid() const { return !url.empty() && !sha256.empty(); }
};

//******************
// ReleaseCatalog
//******************
// One release as the site describes it: the version tag ("v2.0.0-pre0-df68521" for a pre-release, the
// tag plus the short hash), and its packages by platform key - "rpi", "rpi64", "psc", "win", "updateroms".
// Its disk images too, by architecture: a nightly's release.json lists them under "images" ("armhf",
// "arm64", "pc-i386"), pc/images/{release,testing,latest}.json has them at the top level ("i386") - the
// PC stick flasher reads both.
struct ReleaseCatalog {
    std::string version;
    bool prerelease = false;
    std::string date;
    std::map<std::string, UpdateFile> files;
    std::map<std::string, UpdateFile> images;

    bool parse(const std::string &jsonText);
    bool load(const std::string &path);
    const UpdateFile *fileFor(const std::string &platformKey) const;
    const UpdateFile *imageFor(const std::string &arch) const;
};

//******************
// ChannelCatalog
//******************
// The release channels the site offers the PC installers, channels.json: [{id, label, index, images, unstable}] in
// the order to show them (stable first). `index` is the channel's own latest.json (the stick package: a release.json
// shape); `images` is where the PC stick image is when that is another file (pc/images/release.json), else `index`.
// A channel that has no package of its own stands in with the stable channels listed before it (lists()).
struct ChannelEntry {
    std::string id;
    std::string label;
    std::string index;
    std::string images;
    bool unstable = false;
};

struct ChannelCatalog {
    std::vector<ChannelEntry> channels;

    // false (and nothing kept) for a file that is not a channels.json or lists no usable channel
    bool parse(const std::string &jsonText);
    // the three channels the programs knew before channels.json: release, testing, nightly
    static ChannelCatalog builtIn();
    const ChannelEntry *find(const std::string &id) const;
    // the lists a channel reads, in order: its own, then the stable channels before it, nearest first. `images`
    // asks for the PC stick images' files. Empty for an id the catalog does not list.
    std::vector<std::string> lists(const std::string &id, bool images) const;
    // the channel id a program built as `version` wants: "preview-..." -> preview, between tags -> nightly, a
    // pre-release tag -> testing, else release
    static std::string wantedFor(const std::string &version, bool betweenTags, bool preRelease);
    // that channel when the catalog lists it, else its first channel ("" when empty)
    std::string defaultFor(const std::string &version, bool betweenTags, bool preRelease) const;
};

//******************
// RetroArchCatalog
//******************
// The newest RetroArch build for the Pi, by architecture ("armhf", "arm64").
struct RetroArchCatalog {
    std::string version;
    std::map<std::string, UpdateFile> files;

    bool parse(const std::string &jsonText);
    bool load(const std::string &path);
    const UpdateFile *fileFor(const std::string &arch) const;
};

//******************
// PackCatalog
//******************
// One dated pack as the site's psc/<kind>/latest.json (cores, libs, apps, bios, samples) describes it:
// the file itself, its manifest's URL, its date (YYYYMMDD; "" for the BIOS list) and how many things are
// in it. The BIOS list is the file itself (biospack.txt), with the total of what it names in totalBytes.
struct PackCatalog {
    UpdateFile file;
    std::string manifestUrl;
    std::string date;
    std::string version; // a versioned pack (win/retroarch: RetroArch's own version), "" for a dated one
    int count = 0;
    uint64_t totalBytes = 0;
    bool parse(const std::string &jsonText);
    bool load(const std::string &path);
};

//******************
// PscRetroArchCatalog
//******************
// The console's RetroArch build, psc/retroarch/latest.json: the version tag ("v1.22.2-4"), the zip and the
// manifest's URL.
struct PscRetroArchCatalog {
    std::string version;
    UpdateFile zip;
    std::string manifestUrl;
    bool parse(const std::string &jsonText);
    bool load(const std::string &path);
};

//******************
// BundleCatalog
//******************
// bundle.json of an installer download that carries its own payload (AutoBleemInstaller-<v>-full.zip): the
// release version, the stick package's path, and every file of the folder by the path the site serves it under
// ("psc/cores/cores-psc-20261003.tar.gz", "assets/frontend/assets.zip"; the file lies at that path under the
// bundle folder) with its size and sha256. The installer checks each file against this once, then reads it in place.
struct BundleFile {
    std::string path;
    uint64_t size = 0;
    std::string sha256;
};

struct BundleCatalog {
    int format = 0;
    std::string version;
    std::string package; // path of the stick package (autobleem-psc-<v>.tar.gz) among `files`
    std::vector<BundleFile> files;

    bool parse(const std::string &jsonText);
    bool load(const std::string &path);
    const BundleFile *find(const std::string &path) const;
};

//******************
// UpdateState
//******************
// What the launcher remembers between runs, so a check happens once a day and a "skip" or "remind me
// tomorrow" is honoured. Times are unix seconds (the Pi has NTP; 0 = never).
struct UpdateState {
    int64_t lastCheck = 0;
    int64_t postponedUntil = 0;
    std::string skippedVersion;   // an AutoBleem version the user said no to
    std::string skippedRetroArch; // a RetroArch version the user said no to

    bool load(const std::string &path);
    bool save(const std::string &path) const;
};

//******************
// PendingUpdate
//******************
// The artefacts downloaded and verified, waiting for the update script: file names relative to the
// Updates directory. Either may be empty.
struct PendingUpdate {
    std::string autobleemVersion;
    std::string autobleemFile;
    std::string retroarchVersion;
    std::string retroarchFile;

    bool load(const std::string &path);
    bool save(const std::string &path) const;
};

} // namespace ableem
