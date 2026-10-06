//
// PackageService: the game data on the stick (autobleem-main docs/packages.md). Scans Packages/ - a folder that
// holds a package.ini (ours: the Store's, a PE mod's) or files the shipped/player's table recognises (the player's
// own Doom, Quake ...) - into an index held in RAM only (the quiet stick: nothing is written by a scan, nothing is
// created, nothing is put in the player's folders), and answers which games an engine (an App with Uses=) can run.
//
#pragma once

#include "app_manifest.h"
#include "package_table.h"

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

//******************
// PackageStart / PackageGame
//******************
// a program that starts a game (dos-game: the game itself and its SETUP), relative to the package root
struct PackageStart {
    std::string file;
    std::string title;
};

// one game of a package: what an engine is started with
struct PackageGame {
    std::string id; // stable: saves are filed under it ({package_game})
    std::string title;
    std::string variant;              // free text for the picker's second line
    std::string kind;                 // the content kind
    std::string file;                 // the main file, relative to the package root, '/', the real spelling on disk
    std::string licence;              // a recognised file's table licence ("" = none)
    std::vector<PackageStart> starts; // the first is the default
    std::vector<std::pair<std::string, std::string>> settings; // dos-game: name (lower case) -> value
    std::string mapper;                                        // dos-game: a mapper file, relative to the package root
};

//******************
// PackageInfo
//******************
// one package: a folder with a descriptor, a folder where the table recognised something, or unknown data
struct PackageInfo {
    std::string id; // "freedoom", "u/doom" (a recognised folder), "e/wad" (an engine's own folder)
    std::string title;
    std::string version, licence, author, description;
    std::string image;  // the cover: the absolute path of the PNG, "" = the generic icon
    std::string readme; // the absolute path of the readme text, ""
    std::string source; // "store", "mod", "user"
    std::string storeId, peSource;
    std::vector<std::string> replaces; // App folders it supersedes (PackageInstaller parks them)
    std::string root;                  // the folder (absolute)
    bool descriptor = false;           // it has a package.ini
    bool unknown = false;              // nothing in it was recognised: listed, never offered to an engine
    bool duplicate = false;            // another folder claimed its id first: listed, its games are not offered
    bool inApp = false;                // an engine's own data folder (PackageDir=), not in Packages/
    std::vector<PackageGame> games;

    // the distinct kinds of its games, in order
    std::vector<std::string> kinds() const;
};

//******************
// PackageEntry
//******************
// one game offered to one engine: what the picker lists and the launch is given
struct PackageEntry {
    std::string packageId, packageTitle;
    std::string source; // "store", "mod", "user" (an engine's own folder is "user" with inApp set)
    bool inApp = false;
    std::string root; // the package root (absolute) = AB_PKG_DIR
    PackageGame game;

    // <package id>/<game id>: AB_PKG_ID, the key of the choice
    std::string id() const { return packageId + "/" + game.id; }
    // the main file, absolute: AB_PKG_FILE
    std::string file() const { return root + "/" + game.file; }
    std::string mapperFile() const { return game.mapper.empty() ? std::string() : root + "/" + game.mapper; }
};

//******************
// PackageScanLimits
//******************
// what a scan visits: Packages/ and the folders below it up to 4 levels, at most 2000 folders
struct PackageScanLimits {
    int maxDepth = 4;
    size_t maxFolders = 2000;
};

//******************
// PackageService
//******************
// Owned by App (App::packages()). The scan runs on ScanService's worker (ScanPackages) and the screens read the
// index from the main thread: rescan() builds the new index aside and swaps it under a mutex.
class PackageService {
public:
    using Limits = PackageScanLimits;

    // the index, rebuilt from the stick: the player's Packages/packages.ini, then the shipped rc/packages.ini
    // (Env::getPathToPackagesTable()), then Packages/ (a missing one is an empty index and is NOT made)
    void rescan();
    // the same over explicit paths - the tests' and tools' way in
    void rescan(const std::string &packagesDir, const std::string &shippedTable, const Limits &limits = Limits());

    // every package of the index, unknown data and duplicates included, in folder order
    std::vector<PackageInfo> packages() const;
    size_t packageCount() const;
    // the table rows in force (the player's first), for the how-to text
    std::vector<PackageRow> rows() const;
    // what the last scan said about malformed rows, descriptors and limits (one line each)
    std::vector<std::string> problems() const;

    // the games of every package - Packages/ and the App's own PackageDir folders (scanned now, not kept) - whose
    // kind is in the App's Uses=, in the picker's order: by the kind's place in Uses=, then the game's title
    // (case-insensitive, numbers by value), then the package's title. Unknown data and duplicates are never offered.
    std::vector<PackageEntry> entriesFor(const AppManifest &app) const;
    // right before a start: the entry's main file (and its mapper) are still there
    static bool stillThere(const PackageEntry &entry);

    // a signature of the top-level listing of `packagesDir` (and one level below it), kept in RAM by the watcher:
    // equal text = nothing was added or removed; "" for a folder that is not there
    static std::string signatureOf(const std::string &packagesDir);

    // reads one descriptor (<folder>/package.ini, docs/packages.md 2.2) and checks it against the folder's files;
    // false (with `problem`) when it is no package: no Title, no valid game left, over 64 KB
    // `folderName` ("" = the folder's own name) is what the default id (no Id= key) is made from - the installer reads
    // a staged copy whose folder is not the name the package will have
    static bool readDescriptor(const std::string &folder, PackageInfo &out, std::string &problem,
                               const std::string &folderName = "");
    // the package.ini text as the reader sees it (keys lower-cased): the descriptor grammar with a BOM ignored and a
    // `#` starting a comment anywhere on a line
    static std::vector<std::pair<std::string, std::string>> parseDescriptor(const std::string &text);

    // the English display name of a kind ("Doom data"), translated by the caller with _(); an unknown kind is its id
    static std::string kindName(const std::string &kind);
    // the English label of a Source: "Store", "Mod", "Your files", "In this App"
    static std::string sourceName(const std::string &source, bool inApp);
    // every kind this build names, in the table's order
    static const std::vector<std::string> &knownKinds();
    // the text of the how-to (docs/packages.md 4.7): where Packages/ is, one example path per kind, the rules - the
    // README.txt the installers lay. `rows` are the table's rows (the examples are generated from them).
    static std::string readmeText(const std::vector<PackageRow> &rows);

    // case-insensitive "a before b" with digit runs compared by value ("Level 2" < "Level 10")
    static bool naturalLess(const std::string &a, const std::string &b);

private:
    struct Index {
        std::vector<PackageInfo> packages;
        std::vector<PackageRow> rows;
        std::vector<std::string> problems;
        Limits limits;
    };
    std::shared_ptr<const Index> index() const;

    mutable std::mutex mutex_;
    std::shared_ptr<const Index> index_ = std::make_shared<Index>();
};
