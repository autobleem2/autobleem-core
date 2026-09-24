#include "scan_service.h"
#include "environment.h"
#include "retroarch.h"
#include "system.h"
#include "../main.h"
#include "../model/timing.h"

#include <ableem/engine/retroarch_cores.h>
#include <ableem/engine/retroarch_scanner.h>

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
// ScanService::romsFolderAliasesPath
//*******************************
string ScanService::romsFolderAliasesPath() {
    return Env::getWorkingPath() + sep + "platform" + sep + "roms_folders.cfg";
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
bool ScanService::requestScan() {
    if (scanning_.load())
        return false;
    scanRequested_.store(true);
    return true;
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

    auto lastWatchCheck = chrono::steady_clock::now() - chrono::milliseconds(ScanWatchInterval);
    while (!stopping_.load()) {
        bool shouldScan = scanRequested_.exchange(false);

        if (!shouldScan && watching_.load()) {
            auto now = chrono::steady_clock::now();
            if (now - lastWatchCheck >= chrono::milliseconds(ScanWatchInterval)) {
                lastWatchCheck = now;
                shouldScan = checkForChanges();
            }
        }

        if (shouldScan) {
            runScan();
            lastWatchCheck = chrono::steady_clock::now(); // don't immediately re-check right after scanning
        } else {
            this_thread::sleep_for(chrono::milliseconds(250));
        }
    }
}

//*******************************
// ScanService::checkForChanges
//*******************************
bool ScanService::checkForChanges() {
    GamesFingerprint fresh = takeGamesFingerprint();
    bool changedFromScanned = !(fresh == lastScannedFingerprint_);
    bool stableSinceLastCheck = (fresh == lastCheckFingerprint_);
    lastCheckFingerprint_ = fresh;

    // the ROM folders, the same way; a change in either tree runs the whole cycle, once both are still
    if (romScanEnabled()) {
        GamesFingerprint freshRoms = GamesFingerprint::takeAllFiles(Env::getPathToRetroarchRomsDir());
        bool romsChanged = !(freshRoms == lastScannedRomsFingerprint_);
        bool romsStable = (freshRoms == lastCheckRomsFingerprint_);
        lastCheckRomsFingerprint_ = freshRoms;
        if (!stableSinceLastCheck || !romsStable)
            return false;
        return changedFromScanned || romsChanged;
    }
    return changedFromScanned && stableSinceLastCheck;
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
    cores.load(Env::getPathToRetroarchDir(), RetroArchService::coresCfgPath());

    ableem::RetroArchScanner::Options options;
    options.romsDir = Env::getPathToRetroarchRomsDir();
    options.playlistsDir = Env::getPathToRetroarchPlaylistsDir();
    options.folderAliases = ableem::RetroArchScanner::loadFolderAliases(romsFolderAliasesPath());
    options.rdbDir = Env::getPathToRetroarchRdbDir(); // a missing one just means nothing gets identified
    options.stateFile = romScanStateFilePath();       // so a folder nothing changed in is not scanned again
    ableem::RetroArchScanner scanner(&listener);
    ableem::RetroArchScanResult result = scanner.scan(options, ableem::RetroArchScanner::systemsFrom(cores));
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
void ScanService::runScan() {
    scanning_.store(true);

    string gamesDir = Env::getPathToGamesDir();
    Listener listener(this);

    // the scanner processors' preprocessing is the first thing, whoever asked for the scan: the folder
    // processors of both sequences, before anything of the scan itself reads or moves a file
    ProcessorSession processors;
    openProcessors(processors);
    if (processors.any) {
        runFolderProcessors(processors, ProcessorSequence::Ps1, gamesDir);
        if (romScanEnabled())
            runFolderProcessors(processors, ProcessorSequence::Roms, Env::getPathToRetroarchRomsDir());
    }

    if (GameScanner::hasLooseGameFiles(gamesDir)) {
        GameScanner mover(&listener);
        mover.moveLooseGameFilesIntoSubDirs(gamesDir);
    }
    {
        // "(Disc n)" sibling folders become one folder before the tree is read, so the scan below only
        // ever sees the merged game - and the fingerprint taken after the scan is of the merged tree, so
        // the watcher does not fire on the merge's own moves
        GameScanner merger(&listener);
        merger.mergeMultiDiscFolders(gamesDir);
    }

    // then every game folder and every ROM file through its sequence's item chain, before the tree is read -
    // round again (a few times at most) for what a step produced: unzip's .rvz is the next step's input
    if (processors.any) {
        for (int round = 0; round < 3 && runItemChains(processors, ProcessorSequence::Ps1, gamesDir); ++round) {
        }
        if (romScanEnabled()) {
            string romsDir = Env::getPathToRetroarchRomsDir();
            for (int round = 0; round < 3 && runItemChains(processors, ProcessorSequence::Roms, romsDir); ++round) {
            }
        }
        processors.state.save();
    }

    GamesHierarchy hierarchy;
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

    GamesFingerprint fp = takeGamesFingerprint();
    lastScannedFingerprint_ = fp;
    lastCheckFingerprint_ = fp;

    // the other systems' ROMs, when RetroArch is there to play them
    int romCount = 0;
    GamesFingerprint romsFp;
    if (romScanEnabled()) {
        vector<string> playlistsWritten;
        romCount = scanRetroArchRoms(listener, playlistsWritten);
        if (!playlistsWritten.empty()) {
            WorkerEvent written;
            written.kind = WorkerEvent::Kind::PlaylistsWritten;
            written.playlists = playlistsWritten;
            pushEvent(std::move(written));
        }
        romsFp = GamesFingerprint::takeAllFiles(Env::getPathToRetroarchRomsDir());
    }
    lastScannedRomsFingerprint_ = romsFp;
    lastCheckRomsFingerprint_ = romsFp;

    WorkerEvent finished;
    finished.kind = WorkerEvent::Kind::Finished;
    finished.hierarchy = std::move(hierarchy);
    finished.gamesToAddToDB = scanner.gamesToAddToDB;
    finished.fingerprint = fp;
    finished.romsFingerprint = romsFp;
    finished.failedCount = listener.failedCount;
    finished.failedGames = scanner.failedGames;
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

        case WorkerEvent::Kind::Finished: {
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
            event.romsFingerprint.save(romsFingerprintFilePath());

            update.active = false;
            update.finished = true;
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
