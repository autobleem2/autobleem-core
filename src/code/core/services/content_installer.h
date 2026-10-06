//
// The installers an extension (the AutoBleem Store) puts downloaded content in place with - an App (our
// multi-platform App format) into Apps/<name>/, a game's discs into Games/<title>/, a PE package into Mods/ -
// always through a staging folder on the same filesystem (a mod: renamed whole into place), so nothing half-written
// ever shows under Apps/, Games/ or Mods/ (where the scan's watcher would pick it up). The launcher's
// docs/store-plan.md and autobleem-main docs/archive/app-format-plan.md.
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
    // a package already at this version or newer was there: nothing was changed (ok is true)
    bool unchanged = false;
    // something the install did not manage that is not worth failing it for: a Migrate= copy that failed (the old
    // Apps then stay where they were) - for the log and the screen
    std::string warning;
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
    // Version= changed, when every platform's binaries go (no two versions mix) - and a pad.ini and an
    // ab_settings.ini (AppSettings) already there kept (the user's). Refused, and nothing under appsDir touched, when
    // any step fails.
    //
    // An App that merges others (docs/packages.md 11) says so in its app.ini: Replaces=<old App folders, ';'> and
    // Migrate=<old>:<path in it>><path in the new App>, ';' between entries. After the App is in place every entry
    // is COPIED (a file or a folder; a file that already exists at the destination is left alone; a missing source is
    // skipped - so a second install copies nothing new); when all copies worked each old App folder is moved to
    // Apps/.replaced/<name>/ - hidden from the Apps scan, restorable, never deleted. When a copy failed nothing is
    // moved and the result carries a warning. The launcher itself never does either at start-up.
    static InstallResult install(const std::string &archive, const std::string &appsDir, const std::string &stagingDir,
                                 const std::vector<std::string> &keys);
    // the App's folder, gone
    static bool remove(const std::string &appFolder, std::string &error);
    // where in an unpacked tree the App is ("" when nowhere), and its folder name
    static std::string findAppRoot(const std::string &root, const std::string &fallbackName, std::string &name);
};

//******************
// PackageInstaller
//******************
// Game data (docs/packages.md 2.4): a .zip/.tar.gz/.7z with a package.ini at its root or in its one folder, into
// Packages/<id>/. Same discipline as AppInstaller - unpacked to staging, the descriptor validated by the reader of
// 2.2 (a Title, a kind, every game's file there), then renamed whole into place; any failure leaves Packages/
// untouched. The scan never makes Packages/; the first install does (with the README.txt that says how to add games).
class PackageInstaller {
public:
    // `storeId` (the catalog id, "pkg/freedoom") and Source=store are stamped into the laid package.ini. The folder
    // is the package id made safe for FAT (GameInstaller::folderNameFor), " (2)" and on when it is another package's.
    // The same StoreId already there: an equal or older version is a no-op (InstallResult::unchanged); a newer one
    // replaces the folder whole (staged first, the old one removed after, so a failure leaves the old one). After the
    // install each App named by Replaces= in `appsDir` is moved to Apps/.replaced/ - only one that is ours (its
    // app.ini says PeSource=); "" for appsDir = nothing is parked.
    static InstallResult install(const std::string &archive, const std::string &packagesDir,
                                 const std::string &stagingDir, const std::string &storeId = "",
                                 const std::string &appsDir = "");
    // the package's folder, gone - only one whose descriptor says Source=store or Source=mod; never a player's folder
    static bool remove(const std::string &packageFolder, std::string &error);
    // <0, 0, >0: version `a` older than, equal to, newer than `b` (numbers by value, dots/dashes separate)
    static int compareVersions(const std::string &a, const std::string &b);
};

//******************
// ModInstaller
//******************
// A PE package (.mod, a Debian archive) into Mods/ - exactly where a user's own mod goes. The mods scanner
// processor (proc_pe) turns it into an App at the launcher's next scan (Apps/pe-<name>/, PeSource= naming the
// file) and then moves the .mod to Mods/done/ (not deleted: the original stays); this installer only places and
// removes the file - from Mods/ and from Mods/done/ alike - and what the processor made of it.
class ModInstaller {
public:
    // moves the downloaded `mod` to modsDir/<its file name> (the folder made when missing; a file of that name
    // there is replaced): renamed when it is on the same filesystem, else copied under "<name>.part" and renamed,
    // so the scan never meets half a file. Refused, and nothing placed, when it is no .mod or no ar archive.
    // `replaces` (a path in Mods/ or Mods/done/, "" = none): the mod of the version being updated, retired - in both
    // places - once the new one is in place. A copy of the new file's name in Mods/done/ goes too (the processor
    // puts the new one there after the scan).
    static InstallResult install(const std::string &mod, const std::string &modsDir, const std::string &appsDir,
                                 const std::string &replaces = "");
    // the .mod gone first from Mods/ and from Mods/done/ (so a scan in between has nothing to make the App from
    // again), then the Apps it made (every Apps/pe-* whose app.ini says PeSource=<this file>) and the processor's
    // marker for it. `modFile` is the file's path in either place. A mod that held only game data left a package in
    // Packages/ (a sibling of Mods/, PeSource=<this file>): it goes the same way.
    static bool remove(const std::string &modFile, const std::string &appsDir, std::string &error);
    // installed = the .mod is in Mods/ or in Mods/done/, or the processor's marker for it is in Apps/.pe_state/, or
    // a package made from it is in Packages/. `modFile` is the file's path in either place.
    static bool present(const std::string &modFile, const std::string &appsDir);
    // the Mods/ folder of a path in Mods/ or Mods/done/
    static std::string modsDirOf(const std::string &modFile);
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
