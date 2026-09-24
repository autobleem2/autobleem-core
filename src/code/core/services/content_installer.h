//
// The installers an extension (the AutoBleem Store) puts downloaded content in place with - an App (our
// multi-platform App format) into Apps/<name>/, a game's discs into Games/<title>/ - always through a staging
// folder on the same filesystem, so nothing half-written ever shows under Apps/ or Games/ (where the scan's
// watcher would pick it up). docs/store-plan.md and docs/app-format-plan.md in the launcher.
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

//******************
// InstallResult
//******************
struct InstallResult {
    bool ok = false;
    std::string error; // why not, for the log and the screen
    std::string path;  // where it went: Apps/<name> or Games/<folder>
    std::string name;  // the App's folder name, the game's folder name
};

//******************
// ArchiveUnpacker
//******************
// .zip, .tar.gz/.tgz/.tar and .7z (when CHD support is built - it brings the LZMA SDK) behind one call. The
// archive readers refuse unsafe entry names themselves. RAR is not supported (unrar is not GPL-compatible).
class ArchiveUnpacker {
public:
    static bool isArchive(const std::string &path);
    // the sum of the entries' unpacked sizes - what the unpacking needs in free space
    static bool unpackedSize(const std::string &archive, uint64_t &bytes, std::string &error);
    static bool unpack(const std::string &archive, const std::string &destDir, std::string &error);
};

//******************
// AppInstaller
//******************
class AppInstaller {
public:
    // unpacks `archive` into stagingDir, finds the App in it (app.ini at the root, in Apps/<name>/ or in its
    // one folder), checks this machine can run it (AppManifest over `keys`) and lays it over appsDir/<name>/:
    // the shared files and the ini replaced, other platforms' bin/<key>/ and lib/<key>/ kept - unless the
    // Version= changed, when every platform's binaries go (no two versions mix) - and a pad.ini already there
    // kept (the user's). Refused, and nothing under appsDir touched, when any step fails.
    static InstallResult install(const std::string &archive, const std::string &appsDir, const std::string &stagingDir,
                                 const std::vector<std::string> &keys);
    // the App's folder, gone
    static bool remove(const std::string &appFolder, std::string &error);
    // where in an unpacked tree the App is ("" when nowhere), and its folder name
    static std::string findAppRoot(const std::string &root, const std::string &fallbackName, std::string &name);
};

//******************
// GameInstaller
//******************
class GameInstaller {
public:
    // `files`: what was downloaded for one game, in disc order - archives and/or disc images (.chd .pbp .cue
    // .bin .img, and .sbi/.ecm alongside). Unpacked and gathered in stagingDir, a .cue written for a .bin that
    // has none, then moved - flat, every disc together - into gamesDir/<the title made safe for FAT>/, with
    // " (2)" and on when that exists. The scan does the rest (serial, metadata, cover, .m3u).
    static InstallResult install(const std::vector<std::string> &files, const std::string &title,
                                 const std::string &gamesDir, const std::string &stagingDir);
    static bool remove(const std::string &gameFolder, std::string &error);
    // a title as a folder name on FAT: no <>:"/\|?* or control characters, no trailing dots or blanks
    static std::string folderNameFor(const std::string &title);
    // the .cue for a single-track data .bin
    static std::string cueFor(const std::string &binName);
};
