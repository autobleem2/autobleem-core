//
// RetroArchService: RetroArch's playlists as sets of games, and which core plays each entry.
//
#pragma once

#include "../model/ps_game.h"
#include "game_query.h"

#include <ableem/engine/retroarch_cores.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

//********************
// RAPlaylistInfo
//********************
struct RAPlaylistInfo {
    std::string displayName; // the .lpl file name without its extension
    std::string path;
    PsGames psGames;
    bool metadataLoaded = false; // see RetroArchService::ensureMetadata

    RAPlaylistInfo(const std::string &_displayName, const std::string &_path, const PsGames &games)
        : displayName(_displayName), path(_path), psGames(games) {}
};

//********************
// RACorePlatform
//********************
// one row of the RetroArch cores window: a system two or more installed cores play
struct RACorePlatform {
    std::string database;
    ableem::CoreInfos cores; // the platform file's own pick first (it is the one marked "(default)"), then by stem
    int current = 0;         // the index of the core in use: the user's choice, else 0
};

//********************
// RetroArchService
//********************
// Was the RAIntegrator singleton. Reads retroarch/info/*.info (the cores) and retroarch/playlists/*.lpl
// on first use - not before, because it needs Environment's paths - and answers the launcher's questions
// from that: which playlists there are, which games each holds, how many. It is the RetroArchGames that
// GameQueryService consumes, so the RetroArch and Apps sets come through the same door as the PS1 ones.
//
// Every playlist entry becomes a "foreign" PsGame whose core is resolved here: the entry's own core if the
// .so exists, else the one resources/platform/<platform>.cores.cfg names for its database, else the first core
// whose .info lists that database (ableem::CoreInfoTable holds that mapping). Entries whose core or image
// cannot be found are dropped. A game's publisher, year and player count come from the system's .rdb
// (<retroarch>/database/rdb/<playlist name>.rdb) by its label, read the first time the playlist is asked
// for and then dropped again - the launcher shows one playlist at a time and a database is megabytes.
//
// Owned by App (App::retroArch()).
class RetroArchService : public RetroArchGames {
public:
    RetroArchService() = default;

    // RetroArchGames, for GameQueryService
    PsGames gamesInPlaylist(const std::string &playlistName) override;
    std::string historyPlaylistName() override { return historyDisplayName_; }
    PsGames allGames() override;
    int playlistSize(const std::string &playlistName) override { return gameCount(playlistName); }
    PsGames allGamesWithoutMetadata() override { return allGames(false); }

    std::string favoritesPlaylistName() const { return favoritesDisplayName_; }

    // in the order the launcher shows them: the playlists sorted by name, then Favorites, then History
    std::vector<std::string> playlistNames();
    int gameCount(const std::string &playlistName);

    // RetroArch may have added or removed favorites and history entries while it ran
    void reloadFavoritesAndHistory();
    // the background scan rewrote playlists: read them all again (the cores stay as loaded)
    void reloadPlaylists();

    // the installed cores that can play the game's system, the platform's default first (the one a game with no
    // pick of its own gets); empty when none does - what the game editor's Core row cycles through
    ableem::CoreInfos coresForGame(const PsGame &game);
    // the core a game of that system gets when nobody picked one: the first of coresForGame()
    ableem::CoreInfoPtr defaultCoreForGame(const PsGame &game);
    // the game's own pick: written into its playlist entry (core_path/core_name - RetroArch's own per-game core
    // association), and into the game and its twins in the loaded playlists, so the next launch uses it. The
    // playlist is read and rewritten like the scanner does (beside it, then renamed); false when the entry
    // or the playlist cannot be found or written, with nothing changed.
    bool setGameCore(PsGame &game, const ableem::CoreInfoPtr &core);

    // the RetroArch cores window: every system with two or more installed cores, by name
    std::vector<RACorePlatform> corePlatforms();
    // what the window saved: the user's file (<state>/cores.user.cfg) gets a line per system whose choice differs
    // from the platform's pick, a system set back loses its line, and for every system whose core changed the
    // playlist entries under the ROM folders that carried the OLD core move to the new one (hand picks with another
    // core stay; Favorites, History and the PS1 export are not touched), here and in the loaded games. Returns how
    // many systems changed.
    int saveCorePicks(const std::vector<std::pair<std::string, ableem::CoreInfoPtr>> &choices);
    // <state>/cores.user.cfg
    static std::string userCoresCfgPath();

    // the platform's cores.cfg, next to the resources: resources/platform/<platform>.cores.cfg
    static std::string coresCfgPath();

    // a playlist title as RetroArch names the boxart file for it
    static std::string escapeName(const std::string &title);

    // a path from a playlist, as this machine sees it: a console playlist says /media/..., which is the
    // USB root there; on a dev host that prefix is mapped onto the fake USB tree, and a path that already
    // starts with the USB root (a Pi writes its real mount point, /media/autobleem/...) is left alone
    // Separators, a drive letter's case and redundant parts ("//", "/./") never matter to the match: the USB
    // root of a Windows host is written with backslashes, and RetroArch writes its own flavour.
    static std::string mapPlaylistPath(const std::string &path, const std::string &usbRoot);

    // A path in one form for comparing: forward slashes only, no empty or "." parts, ".." resolved against
    // the part before it, no trailing slash; lower case when `ignoreCase` (Windows paths). Textual only -
    // nothing on disk is looked at.
    static std::string normalizePath(const std::string &path, bool ignoreCase);
    // whether two paths name the same file once normalised; case counts on every host but Windows
    static bool samePath(const std::string &a, const std::string &b);
    static bool samePath(const std::string &a, const std::string &b, bool ignoreCase);
    // whether `path` is `dir` or inside it, compared as samePath does
    static bool isUnder(const std::string &path, const std::string &dir);
    static bool isUnder(const std::string &path, const std::string &dir, bool ignoreCase);

private:
    PsGames allGames(bool withMetadata);
    void ensureLoaded();
    void loadCores();
    // the entries of the system's playlist under the ROM folders that name `from` name `to`
    void moveCore(const std::string &database, const ableem::CoreInfoPtr &from, const ableem::CoreInfoPtr &to);
    void loadPlaylists();
    void reloadFavorites();
    void reloadHistory();
    // the Favorites/History playlist rebuilt from RetroArch's own file, with each entry's title and db_name
    // filled in from the playlist it came from (RetroArch leaves them empty), and entries whose game is gone
    // dropped
    void reloadSpecialPlaylist(const std::string &displayName, const std::string &path, bool copyTitle);
    std::string specialPlaylistPath(const std::string &fileName) const; // "" when RetroArch has none

    bool findPlaylist(const std::string &displayName, int *index) const;
    void ensureMetadata(RAPlaylistInfo &info);
    bool isValidPlaylist(const std::string &path) const;
    PsGames readGamesFromPlaylistFile(const std::string &path);
    bool isGameValid(const PsGame &game) const;

    bool autoDetectCorePath(const PsGame &game, std::string &core_name, std::string &core_path) const;

    bool loaded_ = false;
    ableem::CoreInfoTable cores_;
    std::vector<RAPlaylistInfo> playlistInfos_;
    std::string favoritesDisplayName_{"Favorites"};
    std::string historyDisplayName_{"History"};
};
