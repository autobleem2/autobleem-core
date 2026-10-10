#include "scan_service.h"
#include "environment.h"
#include "package_service.h"
#include "retroarch.h"
#include "system.h"
#include "../main.h"
#include "../model/timing.h"

#include <ableem/engine/retroarch_cores.h>
#include <ableem/engine/retroarch_scanner.h>
#include <ableem/engine/startup_timer.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <ableem/engine/log.h>

using namespace std;

//******************
// ScanService::Listener
//******************
// The ScanProgressListener the worker hands to GameScanner: every callback just packages a WorkerEvent and
// pushes it, on whatever thread the scan is running on (the worker's, always - see runScan()).
class ScanService::Listener : public ScanProgressListener {
public:
    explicit Listener(ScanService *owner) : owner_(owner) {}

    void onScanProgress(ScanStage stage, const string &detail, int done, int total) override {
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::Progress;
        event.stage = stage;
        event.detail = detail;
        event.done = done;
        event.total = total;
        owner_->pushEvent(std::move(event));
    }

    void onGameVerified(const UsbGame &game) override {
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::GameVerified;
        event.game.fullPath = game.fullPath;
        event.game.saveStatePath = game.saveStatePath;
        event.game.title = game.title;
        event.game.publisher = game.publisher;
        event.game.year = game.year;
        event.game.players = game.players;
        event.game.serial = game.serial;
        event.game.region = game.region;
        event.game.memcard = game.memcard;
        for (const Disc &disc : game.discs)
            event.game.discNames.push_back(disc.diskName);
        owner_->pushEvent(std::move(event));
    }

    void onGameFailedVerify(const string &fullPath) override {
        failedCount++;
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::GameFailedVerify;
        event.failedPath = fullPath;
        owner_->pushEvent(std::move(event));
    }

    int failedCount = 0;

private:
    ScanService *owner_;
};

//*******************************
// ScanService::~ScanService
//*******************************
ScanService::~ScanService() {
    stop();
}

//*******************************
// ScanService::fingerprintFilePath
//*******************************
string ScanService::fingerprintFilePath() {
    return Env::getPathToStateDir() + sep + "games.fingerprint";
}

//*******************************
// ScanService::romsFingerprintFilePath
//*******************************
string ScanService::romsFingerprintFilePath() {
    return Env::getPathToStateDir() + sep + "roms.fingerprint";
}

//*******************************
// ScanService::modsFingerprintFilePath
//*******************************
string ScanService::modsFingerprintFilePath() {
    return Env::getPathToStateDir() + sep + "mods.fingerprint";
}

namespace {
// Mods/ is only there where a mods processor is installed (openProcessors makes it) - no folder, nothing to watch
bool modsWatched() {
    return DirEntry::isDirectory(Env::getPathToModsDir());
}
} // namespace

//*******************************
// ScanService::romsFolderAliasesPath
//*******************************
string ScanService::romsFolderAliasesPath() {
    return Env::getWorkingPath() + sep + "platform" + sep + "roms_folders.cfg";
}

//*******************************
// ScanService::romsSkipListPath
//*******************************
string ScanService::romsSkipListPath() {
    return Env::getWorkingPath() + sep + "platform" + sep + "roms_skip.cfg";
}

//*******************************
// ScanService::coreMigrationMarkerPath / corePicksScanDue
//*******************************
string ScanService::coreMigrationMarkerPath() {
    return Env::getPathToStateDir() + sep + "core-picks-1.done";
}

bool ScanService::corePicksScanDue() {
    return romScanEnabled() && !DirEntry::exists(coreMigrationMarkerPath());
}

//*******************************
// ScanService::romScanEnabled
//*******************************
bool ScanService::romScanEnabled() {
    return Env::retroArchInstalled() && DirEntry::isDirectory(Env::getPathToRetroarchRomsDir());
}

//*******************************
// ScanService::setOnline
//*******************************
void ScanService::setOnline(bool enabled, const OnlineAssets::Config &config, OnlineAssets::CommandRunner runner) {
    lock_guard<mutex> lock(onlineMutex_);
    onlineEnabled_ = enabled && !config.downloadCommand.empty();
    onlineConfig_ = config;
    onlineRunner_ = std::move(runner);
}

//*******************************
// ScanService::romScanStateFilePath
//*******************************
string ScanService::romScanStateFilePath() {
    return Env::getPathToStateDir() + sep + "roms.scanstate";
}

//*******************************
// ScanService::fingerprintsMatchDisk
//*******************************
bool ScanService::fingerprintsMatchDisk() {
    GamesFingerprint stored;
    vector<string> patterns = processorWatchPatterns();
    GamesFingerprint games = GamesFingerprint::take(Env::getPathToGamesDir(), [&patterns](const string &name) {
        for (const string &p : patterns) {
            if (ProcessorCatalog::globMatch(name, p))
                return true;
        }
        return false;
    });
    if (!stored.load(fingerprintFilePath()) || stored != games)
        return false;
    if (modsWatched()) {
        GamesFingerprint storedMods;
        if (!storedMods.load(modsFingerprintFilePath()) ||
            storedMods != GamesFingerprint::takeAllFiles(Env::getPathToModsDir()))
            return false;
    }
    if (!romScanEnabled())
        return true;
    GamesFingerprint storedRoms;
    return storedRoms.load(romsFingerprintFilePath()) &&
           storedRoms == GamesFingerprint::takeAllFiles(Env::getPathToRetroarchRomsDir());
}

//*******************************
// ScanService::start
//*******************************
void ScanService::start() {
    if (thread_.joinable())
        return; // already running
    stopping_.store(false);
    thread_ = thread(&ScanService::threadMain, this);
}

//*******************************
// ScanService::stop
//*******************************
void ScanService::stop() {
    if (!thread_.joinable())
        return;
    stopping_.store(true);
    thread_.join();
    stopping_.store(false);
}

//*******************************
// ScanService::requestScan
//*******************************
bool ScanService::requestScan(ScanScope scope) {
    if (scope == ScanNone)
        return false;
    // kept even while a scan runs: the worker takes it after that one (the Store's ScanMods after an install landed
    // during a running scan and was lost - the package stayed in Mods/ unconverted, AUTOBLEEM-17)
    scanRequested_.fetch_or(scope & ScanAll);
    return !scanning_.load();
}

//*******************************
// ScanService::pushEvent
//*******************************
void ScanService::pushEvent(WorkerEvent event) {
    lock_guard<mutex> lock(queueMutex_);
    queue_.push_back(std::move(event));
}

//*******************************
// ScanService::threadMain
//*******************************
void ScanService::threadMain() {
    // this whole thread only ever does filesystem/database-adjacent work no one is waiting on; it must never
    // take CPU time away from a running emulator (or anything else on the system)
    System::lowerCurrentThreadPriority();

    watchPatterns_ = processorWatchPatterns();
    lastScannedFingerprint_.load(fingerprintFilePath()); // false (left empty) if nothing was ever scanned
    lastCheckFingerprint_ = lastScannedFingerprint_;
    lastScannedRomsFingerprint_.load(romsFingerprintFilePath());
    lastCheckRomsFingerprint_ = lastScannedRomsFingerprint_;
    lastScannedModsFingerprint_.load(modsFingerprintFilePath());
    lastCheckModsFingerprint_ = lastScannedModsFingerprint_;
    if (packages_ != nullptr)
        scanRequested_.fetch_or(ScanPackages); // the index is RAM only: every start builds it
    if (corePicksScanDue())
        scanRequested_.fetch_or(ScanAll);

    auto lastWatchCheck = chrono::steady_clock::now() - chrono::milliseconds(ScanWatchInterval);
    while (!stopping_.load()) {
        ScanScope due = scanRequested_.exchange(ScanNone);

        if (due == ScanNone && watching_.load()) {
            auto now = chrono::steady_clock::now();
            if (now - lastWatchCheck >= chrono::milliseconds(ScanWatchInterval)) {
                lastWatchCheck = now;
                due = checkForChanges();
            }
        }

        if (due != ScanNone) {
            runScan(due);
            lastWatchCheck = chrono::steady_clock::now(); // don't immediately re-check right after scanning
        } else {
            this_thread::sleep_for(chrono::milliseconds(250));
        }
    }
}

//*******************************
// ScanService::checkForChanges
//*******************************
ScanScope ScanService::checkForChanges() {
    // each tree on its own: a change in Games/ is a PS1 scan, in Mods/ the mods processors, in roms/ the ROM
    // scan - and a tree is due once it has been the same twice in a row (the debounce)
    ScanScope due = ScanNone;

    GamesFingerprint fresh = takeGamesFingerprint();
    if (!(fresh == lastScannedFingerprint_) && fresh == lastCheckFingerprint_)
        due |= ScanPs1;
    lastCheckFingerprint_ = fresh;

    if (modsWatched()) {
        GamesFingerprint freshMods = GamesFingerprint::takeAllFiles(Env::getPathToModsDir());
        if (!(freshMods == lastScannedModsFingerprint_) && freshMods == lastCheckModsFingerprint_)
            due |= ScanMods;
        lastCheckModsFingerprint_ = freshMods;
    }
    if (packages_ != nullptr) {
        const string freshPackages = PackageService::signatureOf(Env::getPathToPackagesDir());
        if (freshPackages != lastScannedPackagesSignature_ && freshPackages == lastCheckPackagesSignature_)
            due |= ScanPackages;
        lastCheckPackagesSignature_ = freshPackages;
    }
    if (romScanEnabled()) {
        GamesFingerprint freshRoms = GamesFingerprint::takeAllFiles(Env::getPathToRetroarchRomsDir());
        if (!(freshRoms == lastScannedRomsFingerprint_) && freshRoms == lastCheckRomsFingerprint_)
            due |= ScanRoms;
        lastCheckRomsFingerprint_ = freshRoms;
    }
    return due;
}

//*******************************
// ScanService::fetchMissingPs1BoxArt
//*******************************
// The covers the scan did not find for PS1 games - no PNG next to the game, nothing in the thumbnails tree
// - come from libretro's server one game at a time, the way the other systems' ROMs already get theirs,
// only where the platform has a download_command (a Pi, a PC; never the console). The file lands where
// ThumbnailLookup looks, and the game's Game.ini gets the cached path at once, so neither this scan's
// database rows nor the next scan have to look again. Reported to the launcher as BoxArtFetched, which
// makes it drop its thumbnail listings and reload the covers on show.
void ScanService::fetchMissingPs1BoxArt(Listener &listener, const vector<UsbGamePtr> &games) {
    unique_ptr<OnlineAssets> online;
    {
        lock_guard<mutex> lock(onlineMutex_);
        if (onlineEnabled_)
            online = make_unique<OnlineAssets>(onlineConfig_, onlineRunner_);
    }
    if (!online)
        return;
    vector<OnlineAssets::BoxArtRequest> requests = OnlineAssets::ps1Requests(games);
    if (requests.empty())
        return;
    int fetched = online->fetchMissingBoxArt(
        requests, Env::getPathToRetroarchThumbnailsDir(), &listener, [this]() { return stopping_.load(); }, nullptr,
        [&games](const OnlineAssets::BoxArtRequest &request, const string &path) {
            for (const UsbGamePtr &game : games) {
                if (game->coverPath.empty() && !game->coverImageFound &&
                    (game->recordName == request.label || (game->recordName.empty() && game->title == request.label))) {
                    game->coverPath = path;
                    game->saveGameIni(game->fullPath + sep + GAME_INI);
                }
            }
        });
    if (fetched > 0) {
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::BoxArtFetched;
        event.boxArtFetched = fetched;
        pushEvent(std::move(event));
    }
}

//*******************************
// ScanService::scanRetroArchRoms
//*******************************
// The worker's own CoreInfoTable, not RetroArchService's: the service belongs to the main thread, and the
// .info files are a few hundred small reads. The system table is what the service would answer - the same
// .info mapping under the same platform cores.cfg.
int ScanService::scanRetroArchRoms(Listener &listener, vector<string> &playlistsWritten) {
    // the online pass, when it is on and the network is there: the databases first, so the scan below
    // can name the games, the box art after it (fetchBoxArt), for the names it settled on
    unique_ptr<OnlineAssets> online;
    {
        lock_guard<mutex> lock(onlineMutex_);
        if (onlineEnabled_)
            online = make_unique<OnlineAssets>(onlineConfig_, onlineRunner_);
    }
    if (online)
        online->ensureDatabases(Env::getPathToRetroarchRdbDir());

    ableem::CoreInfoTable cores;
    {
        ableem::StartupTimer timer("scan-ra-core-info"); // on the scan worker, off the menu's path
        cores.load(Env::getPathToRetroarchDir(), RetroArchService::coresCfgPath(),
                   RetroArchService::userCoresCfgPath());
    }

    ableem::RetroArchScanner::Options options;
    options.romsDir = Env::getPathToRetroarchRomsDir();
    options.playlistsDir = Env::getPathToRetroarchPlaylistsDir();
    options.folderAliases = ableem::RetroArchScanner::loadFolderAliases(romsFolderAliasesPath());
    // a folder for every system an installed core plays, so there is somewhere to put its games
    ableem::RetroArchScanner::createMissingFolders(options.romsDir, cores, options.folderAliases,
                                                   ableem::RetroArchScanner::loadSkipList(romsSkipListPath()));
    options.rdbDir = Env::getPathToRetroarchRdbDir();        // a missing one just means nothing gets identified
    options.stateFile = romScanStateFilePath();              // so a folder nothing changed in is not scanned again
    options.coreMigrationMarker = coreMigrationMarkerPath(); // once per stick
    ableem::RetroArchScanner scanner(&listener);
    ableem::RetroArchScanResult result;
    {
        ableem::StartupTimer timer("scan-ra-roms");
        result = scanner.scan(options, ableem::RetroArchScanner::systemsFrom(cores));
    }
    playlistsWritten = result.playlistsWritten;
    PLOG_INFO << "RetroArch ROM scan: " << result.systemsScanned << " systems, " << result.gamesFound << " games ("
              << result.gamesIdentified << " named by a database), " << result.playlistsWritten.size()
              << " playlists written, " << result.unknownFolders.size() << " folders with no core";

    if (online && !result.games.empty()) {
        int fetched = online->fetchMissingBoxArt(result.games, Env::getPathToRetroarchThumbnailsDir(), &listener,
                                                 [this]() { return stopping_.load(); });
        if (fetched > 0) {
            WorkerEvent event;
            event.kind = WorkerEvent::Kind::BoxArtFetched;
            event.boxArtFetched = fetched;
            pushEvent(std::move(event));
        }
    }
    return result.gamesFound;
}

//*******************************
// ScanService::runScan
//*******************************
void ScanService::runScan(ScanScope scope) {
    scanning_.store(true);

    const bool ps1 = (scope & ScanPs1) != 0;
    const bool roms = (scope & ScanRoms) != 0 && romScanEnabled();
    const bool mods = (scope & ScanMods) != 0;
    const bool packages = (scope & ScanPackages) != 0 && packages_ != nullptr;
    string gamesDir = Env::getPathToGamesDir();
    Listener listener(this);

    // the scanner processors' preprocessing is the first thing, whoever asked for the scan: the folder
    // processors of the sequences in scope, before anything of the scan itself reads or moves a file
    ProcessorSession processors;
    if (ps1 || roms || mods)
        openProcessors(processors);
    if (processors.any) {
        if (ps1)
            runFolderProcessors(processors, ProcessorSequence::Ps1, gamesDir);
        if (roms)
            runFolderProcessors(processors, ProcessorSequence::Roms, Env::getPathToRetroarchRomsDir());
        if (mods)
            runModsProcessors(processors);
    }

    if (ps1) {
        if (GameScanner::hasLooseGameFiles(gamesDir)) {
            GameScanner mover(&listener);
            mover.moveLooseGameFilesIntoSubDirs(gamesDir);
        }
        // "(Disc n)" sibling folders become one folder before the tree is read, so the scan below only
        // ever sees the merged game - and the fingerprint taken after the scan is of the merged tree, so
        // the watcher does not fire on the merge's own moves
        GameScanner merger(&listener);
        merger.mergeMultiDiscFolders(gamesDir);
    }

    // then every game folder and every ROM file through its sequence's item chain, before the tree is read -
    // round again (a few times at most) for what a step produced: unzip's .rvz is the next step's input
    if (processors.any) {
        if (ps1) {
            for (int round = 0; round < 3 && runItemChains(processors, ProcessorSequence::Ps1, gamesDir); ++round) {
            }
        }
        if (roms) {
            string romsDir = Env::getPathToRetroarchRomsDir();
            for (int round = 0; round < 3 && runItemChains(processors, ProcessorSequence::Roms, romsDir); ++round) {
            }
        }
        processors.state.save();
    }

    GamesHierarchy hierarchy;
    GamesFingerprint fp;
    UsbGames gamesToAddToDB;
    ableem::FailedGames failedGames;
    int failedCount = 0;
    if (ps1) {
        hierarchy.getHierarchy(gamesDir);

        WorkerEvent started;
        started.kind = WorkerEvent::Kind::ScanStarted;
        for (const UsbGamePtr &game : hierarchy.getAllGames())
            started.currentPaths.push_back(game->fullPath);
        pushEvent(std::move(started));

        // this thread's own sqlite connection and rdb - never regional.db
        MetadataLookup metadata(Env::getPathToCoversDBDir(), Env::getPathToPlayStationRdbFile());
        GameScanner scanner(&listener);
        scanner.scanGamesDirectory(hierarchy, metadata);
        fetchMissingPs1BoxArt(listener, scanner.gamesToAddToDB);

        fp = takeGamesFingerprint();
        lastScannedFingerprint_ = fp;
        lastCheckFingerprint_ = fp;
        gamesToAddToDB = scanner.gamesToAddToDB;
        failedGames = scanner.failedGames;
        failedCount = listener.failedCount;
    }

    // the other systems' ROMs, when RetroArch is there to play them
    int romCount = 0;
    GamesFingerprint romsFp;
    if (roms) {
        vector<string> playlistsWritten;
        romCount = scanRetroArchRoms(listener, playlistsWritten);
        if (!playlistsWritten.empty()) {
            WorkerEvent written;
            written.kind = WorkerEvent::Kind::PlaylistsWritten;
            written.playlists = playlistsWritten;
            pushEvent(std::move(written));
        }
        romsFp = GamesFingerprint::takeAllFiles(Env::getPathToRetroarchRomsDir());
        lastScannedRomsFingerprint_ = romsFp;
        lastCheckRomsFingerprint_ = romsFp;
    }
    GamesFingerprint modsFp;
    if (mods && modsWatched()) {
        modsFp = GamesFingerprint::takeAllFiles(Env::getPathToModsDir());
        lastScannedModsFingerprint_ = modsFp;
        lastCheckModsFingerprint_ = modsFp;
    }

    // the game data under Packages/: the RAM index is rebuilt, nothing is written
    if (packages) {
        packages_->rescan();
        lastScannedPackagesSignature_ = PackageService::signatureOf(Env::getPathToPackagesDir());
        lastCheckPackagesSignature_ = lastScannedPackagesSignature_;
        WorkerEvent changed;
        changed.kind = WorkerEvent::Kind::PackagesChanged;
        pushEvent(std::move(changed));
    }

    // Apps alone (an App was installed or removed): nothing to scan, the launcher reloads the Apps set
    if (scope == ScanApps) {
        WorkerEvent apps;
        apps.kind = WorkerEvent::Kind::AppsChanged;
        pushEvent(std::move(apps));
    }

    WorkerEvent finished;
    finished.kind = WorkerEvent::Kind::Finished;
    finished.scope = (ps1 ? ScanPs1 : ScanNone) | (roms ? ScanRoms : ScanNone) | (mods ? ScanMods : ScanNone) |
                     (packages ? ScanPackages : ScanNone);
    finished.hierarchy = std::move(hierarchy);
    finished.gamesToAddToDB = gamesToAddToDB;
    finished.fingerprint = fp;
    finished.romsFingerprint = romsFp;
    finished.modsFingerprint = modsFp;
    finished.failedCount = failedCount;
    finished.failedGames = failedGames;
    finished.romCount = romCount;
    pushEvent(std::move(finished));

    scanning_.store(false);
}

//*******************************
// ScanService::applyVerifiedGame
//*******************************
void ScanService::applyVerifiedGame(const ScannedGame &game, ScanUpdate &update) {
    GameDatabase &db = library_.usbGames();

    // the PATH column always carries a trailing separator (insertGame below adds it - "path + sep" is a
    // no-op when one is already there), but a UsbGame's fullPath never does; every lookup by path has to
    // add it back to match what is actually stored
    int id = 0;
    bool existed = db.findGameIdByPath(game.fullPath + sep, &id);
    if (!existed && claimMovedGame(game, &id))
        existed = true; // the row is this game's again, at its new path
    if (!existed) {
        id = db.maxGameId() + 1;
        db.insertGame(id, game.title, game.publisher, game.players, game.year, game.fullPath + sep,
                      game.saveStatePath + sep, game.memcard);
    } else {
        db.updateGame(id, game.title, game.publisher, game.players, game.year, game.saveStatePath + sep, game.memcard);
    }
    db.replaceDiscs(id, game.discNames);

    GameRecord record;
    record.gameId = id;
    if (!db.reloadUsbGame(record)) {
        PLOG_WARNING << "ScanService: could not reload game id " << id << " (" << game.fullPath << ") after writing it";
        return;
    }

    PsGamePtr psGame = std::make_shared<PsGame>();
    static_cast<GameRecord &>(*psGame) = record;
    if (existed)
        update.updatedGames.push_back(psGame);
    else
        update.addedGames.push_back(psGame);
}

//*******************************
// ScanService::claimMovedGame
//*******************************
// The key is the folder's name plus its disc file names: a folder dragged into a sub-folder of Games/ (or
// back out) keeps both, and together they tell one game from another better than a name alone - two
// different games in folders of the same name have different image files. A folder that was *renamed*
// is not matched (a new game; its states stay under the old name in !SaveStates, as always).
bool ScanService::claimMovedGame(const ScannedGame &game, int *id) {
    string folderName = DirEntry::getFileNameFromPath(game.fullPath);
    vector<string> discs = game.discNames;
    sort(discs.begin(), discs.end());

    for (auto it = vanished_.begin(); it != vanished_.end(); ++it) {
        if (it->folderName != folderName)
            continue;
        vector<string> theirs = it->discNames;
        sort(theirs.begin(), theirs.end());
        if (theirs != discs)
            continue;

        if (!library_.usbGames().updateGamePath(it->gameId, game.fullPath + sep)) {
            PLOG_WARNING << "ScanService: could not move game id " << it->gameId << " to " << game.fullPath;
            return false;
        }
        PLOG_INFO << "ScanService: game id " << it->gameId << " (" << folderName << ") moved to " << game.fullPath;
        *id = it->gameId;
        vanished_.erase(it);
        return true;
    }
    return false;
}

//*******************************
// ScanService::deleteUnclaimedVanished
//*******************************
void ScanService::deleteUnclaimedVanished(ScanUpdate &update) {
    for (const VanishedGame &gone : vanished_) {
        if (library_.usbGames().deleteGame(gone.gameId))
            update.removedGameIds.push_back(gone.gameId);
    }
    vanished_.clear();
}

//*******************************
// ScanService::poll
//*******************************
ScanUpdate ScanService::poll() {
    ScanUpdate update;

    vector<WorkerEvent> events;
    {
        lock_guard<mutex> lock(queueMutex_);
        events.swap(queue_);
    }

    for (WorkerEvent &event : events) {
        switch (event.kind) {
        case WorkerEvent::Kind::ScanStarted: {
            update.active = true;

            // a previous scan that never reported Finished (it cannot happen, but) would leave rows here
            deleteUnclaimedVanished(update);

            // currentPaths are bare UsbGame::fullPath values (no trailing separator); PATH always has
            // one (see applyVerifiedGame) - add it back so the comparison below means what it looks like
            set<string> current;
            for (const string &path : event.currentPaths)
                current.insert(path + sep);

            // the rows whose folder is not where it was are not deleted yet: one may turn up at another
            // path as the same game, moved (claimMovedGame) - the rest go when the scan finishes
            for (const GamePath &row : library_.usbGames().loadGamePaths()) {
                if (current.find(row.path) == current.end()) {
                    VanishedGame gone;
                    gone.gameId = row.gameId;
                    gone.folderName = DirEntry::getFileNameFromPath(DirEntry::removeSeparatorFromEndOfPath(row.path));
                    gone.discNames = library_.usbGames().loadDiscNames(row.gameId);
                    vanished_.push_back(std::move(gone));
                }
            }
            break;
        }

        case WorkerEvent::Kind::Progress:
            update.progressed = true;
            update.stage = event.stage;
            update.detail = event.detail;
            update.done = event.done;
            update.total = event.total;
            break;

        case WorkerEvent::Kind::GameVerified:
            applyVerifiedGame(event.game, update);
            break;

        case WorkerEvent::Kind::GameFailedVerify: {
            int id = 0;
            if (library_.usbGames().findGameIdByPath(event.failedPath + sep, &id)) {
                if (library_.usbGames().deleteGame(id))
                    update.removedGameIds.push_back(id);
            }
            update.lastFailedGamePath = event.failedPath;
            break;
        }

        case WorkerEvent::Kind::PlaylistsWritten:
            if (retroArch_)
                retroArch_->reloadPlaylists();
            update.playlistsWritten.insert(update.playlistsWritten.end(), event.playlists.begin(),
                                           event.playlists.end());
            break;

        case WorkerEvent::Kind::BoxArtFetched:
            update.boxArtFetched += event.boxArtFetched;
            break;

        case WorkerEvent::Kind::ProcessorProgress:
            update.processorProgressed = true;
            update.processor = event.processor;
            break;

        case WorkerEvent::Kind::ProcessorNotice:
            update.processorNotices.push_back(event.notice);
            break;

        case WorkerEvent::Kind::AppsChanged:
            update.appsChanged = true;
            break;

        case WorkerEvent::Kind::PackagesChanged:
            update.packagesChanged = true;
            break;

        case WorkerEvent::Kind::Finished: {
            if (event.scope & ScanPs1) {
                // the vanished rows no moved game claimed are really gone - before the sub-dir rows are
                // rebuilt from what is left
                deleteUnclaimedVanished(update);

                // writeSubDirRows looks games up by UsbGame::fullPath (no trailing separator) - strip the
                // one loadGamePaths() rows always carry so the keys match
                map<string, int> idByPath;
                for (const GamePath &row : library_.usbGames().loadGamePaths())
                    idByPath[DirEntry::removeSeparatorFromEndOfPath(row.path)] = row.gameId;

                GameScanner::writeSubDirRows(event.hierarchy, library_.usbGames(), idByPath);
                library_.usbGames().replaceFailedGames(event.failedGames);
                library_.writeEmulationStationGamelist();
                library_.exportToRetroArchPlaylist();
                event.fingerprint.save(fingerprintFilePath());
            }
            if (event.scope & ScanRoms)
                event.romsFingerprint.save(romsFingerprintFilePath());
            if ((event.scope & ScanMods) && modsWatched())
                event.modsFingerprint.save(modsFingerprintFilePath());

            update.active = false;
            update.scanEnded = true;
            // a Mods-only scan has no summary to show and no game roster to reload (its Apps arrive as appsChanged)
            update.finished = (event.scope & (ScanPs1 | ScanRoms)) != 0;
            update.finishedGameCount = static_cast<int>(event.gamesToAddToDB.size());
            update.finishedFailedCount = event.failedCount;
            update.finishedRomCount = event.romCount;
            break;
        }
        }
    }

    return update;
}

//*******************************
// ScanService: the scanner processors
//*******************************
// docs/scanner-processors-plan.md in the launcher. Everything below runs on the worker thread, inside
// runScan(), except the setters and the static paths.

string ScanService::processorsDir() {
    return Env::getPathToSystemDir() + sep + "Processors";
}

string ScanService::processorSequencesFile() {
    return processorsDir() + sep + "sequence.ini";
}

string ScanService::processorStateFilePath() {
    return Env::getPathToStateDir() + sep + "processors.state";
}

string ScanService::processorsLogFilePath() {
    return Env::getPathToLogsDir() + sep + "processors.log";
}

vector<string> ScanService::processorWatchPatterns() {
    ProcessorCatalog catalog(processorsDir(), Env::appPlatformKeys());
    catalog.scan();
    return catalog.watchPatterns();
}

//*******************************
// ScanService::takeGamesFingerprint
//*******************************
GamesFingerprint ScanService::takeGamesFingerprint() const {
    if (watchPatterns_.empty())
        return GamesFingerprint::take(Env::getPathToGamesDir());
    return GamesFingerprint::take(Env::getPathToGamesDir(), [this](const string &name) {
        for (const string &p : watchPatterns_) {
            if (ProcessorCatalog::globMatch(name, p))
                return true;
        }
        return false;
    });
}

//*******************************
// ScanService::setProcessorsSuspended
//*******************************
void ScanService::setProcessorsSuspended(bool suspended) {
    processorsSuspended_.store(suspended);
    // what was held back gets its turn: straight onto the request flag - a scan may still be running, and
    // requestScan() would refuse it then; the worker picks the flag up after it
    if (!suspended && processorsHeldBack_.exchange(false))
        scanRequested_.store(true);
}

//*******************************
// ScanService::setProcessorLanguage
//*******************************
void ScanService::setProcessorLanguage(const string &language) {
    lock_guard<mutex> lock(processorLanguageMutex_);
    processorLanguage_ = language;
}

//*******************************
// ScanService::ProcessorSession
//*******************************
ScanService::ProcessorSession::ProcessorSession()
    : catalog(processorsDir(), Env::appPlatformKeys()), sequences(processorSequencesFile()),
      state(processorStateFilePath()) {}

//*******************************
// ScanService::processorEnvironment
//*******************************
vector<pair<string, string>> ScanService::processorEnvironment() {
    string keys;
    for (const string &k : Env::appPlatformKeys())
        keys += (keys.empty() ? "" : " ") + k;
    string language;
    {
        lock_guard<mutex> lock(processorLanguageMutex_);
        language = processorLanguage_;
    }
    return {{"AB_ROOT", Env::getPathToUSBRoot()},
            {"AB_GAMES_DIR", Env::getPathToGamesDir()},
            {"AB_ROMS_DIR", Env::getPathToRetroarchRomsDir()},
            {"AB_MODS_DIR", Env::getPathToModsDir()},
            {"AB_APPS_DIR", Env::getPathToAppsDir()},
            {"AB_RDB_DIR", Env::getPathToRetroarchRdbDir()},
            {"AB_PLATFORM", Env::buildTargetKey()},
            {"AB_PLATFORM_KEYS", keys},
            {"AB_LANGUAGE", language},
            {"AB_VERSION", Env::productVersion()}};
}

//*******************************
// ScanService::openProcessors
//*******************************
void ScanService::openProcessors(ProcessorSession &session) {
    if (!DirEntry::isDirectory(processorsDir()))
        return;
    session.catalog.scan();
    watchPatterns_ = session.catalog.watchPatterns();
    // the folder the PE mod packages are dropped into, made where a mods processor is installed for this machine
    // (the console's package) - and left alone everywhere else
    for (const ProcessorInfo &p : session.catalog.processors()) {
        if (p.has(ProcessorKind::Mods) && p.builtForThisSystem()) {
            DirEntry::createDirs(Env::getPathToModsDir());
            break;
        }
    }
    if (session.sequences.load(session.catalog.processors()))
        session.sequences.save();
    session.state.load();
    for (ProcessorSequence s : {ProcessorSequence::Ps1, ProcessorSequence::Roms}) {
        if (!session.sequences.chain(s, session.catalog.processors()).empty())
            session.any = true;
    }
    if (!session.any)
        return;

    ProcessorRunner::Options options;
    options.logFile = processorsLogFilePath();
    options.tmpBase = ProcessorRunner::defaultTmpBase();
    options.env = processorEnvironment();
    options.homeBase = Env::getPathToUSBRoot() + sep + "Home" + sep + "processors";
    session.runner = make_unique<ProcessorRunner>(processorProcess_ ? *processorProcess_ : streamingProcess_, options);
}

//*******************************
// ScanService::processorShouldStop
//*******************************
bool ScanService::processorShouldStop(const ProcessorInfo &processor) {
    if (stopping_.load())
        return true;
    if (processor.modifies && processorsSuspended_.load()) {
        processorsHeldBack_.store(true);
        return true;
    }
    return false;
}

//*******************************
// ScanService::runProcessor
//*******************************
bool ScanService::runProcessor(ProcessorSession &session, const ProcessorInfo &processor, ProcessorKind kind,
                               const vector<string> &targetArgs, const string &target, const string &item,
                               const string &digestBefore, const function<string()> &digestAfter, bool askIsMine,
                               bool *changed) {
    const string kindName = ProcessorCatalog::kindName(kind);
    if (session.state.isSettled(processor.name, processor.version, kindName, target, digestBefore))
        return true;
    if (processorShouldStop(processor))
        return false;
    auto stop = [this, &processor]() { return processorShouldStop(processor); };

    if (askIsMine && !session.runner->isMine(processor, targetArgs, stop)) {
        // not its business - remembered, so it is not asked again until the target changes
        session.state.record(processor.name, processor.version, kindName, target, digestBefore, ProcessorResult::Ok);
        return true;
    }

    ProcessorRunner::Outcome outcome = session.runner->start(
        processor, targetArgs, item,
        [this, &processor, &item](const ableem::ProcessorOutput &out) {
            if (!out.active())
                return; // "#Starting" alone is not worth a bubble
            WorkerEvent event;
            event.kind = WorkerEvent::Kind::ProcessorProgress;
            event.processor.title = out.title().empty() ? processor.title : out.title();
            event.processor.item = item;
            event.processor.stage = out.stage();
            event.processor.percent = out.percent();
            event.processor.done = out.done();
            event.processor.total = out.total();
            pushEvent(std::move(event));
        },
        stop);

    for (const string &warning : outcome.warnings) {
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::ProcessorNotice;
        event.notice = {processor.title, item, warning, false};
        pushEvent(std::move(event));
    }
    if (outcome.result == ProcessorResult::Failed) {
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::ProcessorNotice;
        event.notice = {processor.title, item, outcome.message, true};
        pushEvent(std::move(event));
    }

    // the digest after the run: its own output is not a change the next scan has to answer
    string after = digestAfter();
    if (changed && after != digestBefore)
        *changed = true;
    session.state.record(processor.name, processor.version, kindName, target, after, outcome.result);
    session.state.save(); // a power cut mid-scan must not make the next one redo what is done
    return outcome.result == ProcessorResult::Ok;
}

//*******************************
// ScanService::runFolderProcessors
//*******************************
void ScanService::runFolderProcessors(ProcessorSession &session, ProcessorSequence sequence, const string &treeDir) {
    if (!DirEntry::isDirectory(treeDir))
        return;
    ProcessorKind kind = sequence == ProcessorSequence::Ps1 ? ProcessorKind::GamesFolder : ProcessorKind::RomsFolder;
    const char *flag = sequence == ProcessorSequence::Ps1 ? "--games" : "--roms";
    for (const ProcessorInfo *p : session.sequences.chain(sequence, session.catalog.processors())) {
        if (stopping_.load())
            return;
        if (!p->has(kind))
            continue;
        map<string, long long> files = ProcessorState::files(treeDir);
        // nothing it could want: not started at all
        if (!p->match.empty()) {
            bool any = false;
            for (const auto &f : files) {
                if (p->matchesFile(DirEntry::getFileNameFromPath(f.first))) {
                    any = true;
                    break;
                }
            }
            if (!any)
                continue;
        }
        runProcessor(
            session, *p, kind, {flag, treeDir}, "", "", ProcessorState::digest(treeDir),
            [&treeDir]() { return ProcessorState::digest(treeDir); }, false, nullptr);
    }
}

//*******************************
// ScanService::runModsProcessors
//*******************************
// Kinds=mods: once per scan over Mods/, as "--start --mods <Mods>" (it knows AB_MODS_DIR and AB_APPS_DIR as
// well), when a file its Match takes is there and Mods/ changed since it last ran. It writes into Apps/, so
// the launcher is told when that tree is not what it was.
void ScanService::runModsProcessors(ProcessorSession &session) {
    const string modsDir = Env::getPathToModsDir();
    if (!DirEntry::isDirectory(modsDir))
        return;
    const string appsDir = Env::getPathToAppsDir();
    for (const ProcessorInfo *p : session.sequences.chain(ProcessorSequence::Ps1, session.catalog.processors())) {
        if (stopping_.load())
            return;
        if (!p->has(ProcessorKind::Mods))
            continue;
        bool any = false;
        for (const auto &f : ProcessorState::files(modsDir)) {
            if (p->matchesFile(DirEntry::getFileNameFromPath(f.first))) {
                any = true;
                break;
            }
        }
        if (!any)
            continue; // nothing it could want: not started at all
        const string appsBefore = ProcessorState::digest(appsDir);
        runProcessor(
            session, *p, ProcessorKind::Mods, {"--mods", modsDir}, "", "", ProcessorState::digest(modsDir),
            [&modsDir]() { return ProcessorState::digest(modsDir); }, false, nullptr);
        if (ProcessorState::digest(appsDir) != appsBefore) {
            WorkerEvent event;
            event.kind = WorkerEvent::Kind::AppsChanged;
            pushEvent(std::move(event));
        }
    }
}

namespace {

// every folder under Games/ that has files of its own (a game folder, or a folder a processor may make one
// of) - not the !SaveStates/!MemCards trees and nothing starting with '.'
void gameFolders(const string &dir, const string &rel, vector<pair<string, string>> &out) {
    bool hasFiles = false;
    for (const DirEntry &entry : DirEntry::diru(dir)) {
        if (entry.name.empty() || entry.name[0] == '.' || entry.name[0] == '!')
            continue;
        if (entry.isDir) {
            gameFolders(dir + sep + entry.name, rel.empty() ? entry.name : rel + "/" + entry.name, out);
        } else if (!ProcessorState::ignoredName(entry.name)) {
            hasFiles = true;
        }
    }
    if (hasFiles && !rel.empty())
        out.emplace_back(dir, rel);
}

// every file under roms/<system>/, with its system (the top folder's name)
struct RomFile {
    string path, rel, system;
};
void romFiles(const string &dir, const string &rel, const string &system, vector<RomFile> &out) {
    for (const DirEntry &entry : DirEntry::diru(dir)) {
        if (ProcessorState::ignoredName(entry.name))
            continue;
        string childRel = rel.empty() ? entry.name : rel + "/" + entry.name;
        if (entry.isDir)
            romFiles(dir + sep + entry.name, childRel, system.empty() ? entry.name : system, out);
        else if (!system.empty())
            out.push_back({dir + sep + entry.name, childRel, system});
    }
}

} // namespace

//*******************************
// ScanService::runItemChains
//*******************************
bool ScanService::runItemChains(ProcessorSession &session, ProcessorSequence sequence, const string &treeDir) {
    if (!DirEntry::isDirectory(treeDir))
        return false;
    ProcessorKind kind = sequence == ProcessorSequence::Ps1 ? ProcessorKind::Ps1 : ProcessorKind::Rom;
    vector<const ProcessorInfo *> chain;
    for (const ProcessorInfo *p : session.sequences.chain(sequence, session.catalog.processors())) {
        if (p->has(kind))
            chain.push_back(p);
    }
    if (chain.empty())
        return false;

    bool changed = false;
    if (sequence == ProcessorSequence::Ps1) {
        vector<pair<string, string>> folders;
        gameFolders(DirEntry::removeSeparatorFromEndOfPath(treeDir), "", folders);
        for (const auto &folder : folders) {
            const string &path = folder.first;
            string item = DirEntry::getFileNameFromPath(path);
            for (const ProcessorInfo *p : chain) {
                if (stopping_.load())
                    return false;
                if (!DirEntry::isDirectory(path))
                    break; // a step removed or renamed it: what it made is the next round's
                bool candidate = p->match.empty();
                for (const DirEntry &f : DirEntry::diru_FilesOnly(path)) {
                    if (!candidate && !ProcessorState::ignoredName(f.name) && p->matchesFile(f.name))
                        candidate = true;
                }
                if (!candidate)
                    continue;
                if (!runProcessor(
                        session, *p, kind, {"--ps1", path}, folder.second, item, ProcessorState::digestOfOwnFiles(path),
                        [&path]() { return ProcessorState::digestOfOwnFiles(path); }, true, &changed))
                    break; // failed or stopped: the rest of this game's chain waits
            }
        }
    } else {
        vector<RomFile> roms;
        romFiles(DirEntry::removeSeparatorFromEndOfPath(treeDir), "", "", roms);
        for (const RomFile &rom : roms) {
            string item = DirEntry::getFileNameFromPath(rom.path);
            for (const ProcessorInfo *p : chain) {
                if (stopping_.load())
                    return false;
                if (!DirEntry::exists(rom.path))
                    break; // a step turned it into something else: the next round takes that
                if (!p->matchesFile(item) || !p->wantsSystem(rom.system))
                    continue;
                if (!runProcessor(
                        session, *p, kind, {"--rom", rom.path, "--system", rom.system}, rom.rel, item,
                        ProcessorState::digest(rom.path), [&rom]() { return ProcessorState::digest(rom.path); }, true,
                        &changed))
                    break;
            }
        }
    }
    return changed;
}
