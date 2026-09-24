//
// LanServer: a LanLibrary served over HTTP, with everything that keeps it current - what abstored's main did and
// LAN Share's window needs too, so both front ends run exactly this:
//   /           the status page (index_page.h)
//   /store.tsv  the Store's source, every URL built from the address the client used
//   /files/...  the games' files, only those the last scan listed (Range honoured)
//   /cover/...  a game's cover
//   /rescan     scan now
// A watcher scans again when the folders' fingerprint changes (checked every 10 s, or at once on /rescan) and
// a hasher works out the checksums behind everything else. start() scans once, listens and starts the three
// threads; stop() (or the destructor) ends them.
//
#pragma once

#include <ableem/lanserver/lan_library.h>

#include <atomic>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ableem {

class HttpServer;

class LanServer {
public:
    struct Config {
        LanLibrary::Config library;
        int port = 8124;
        std::string name = "My games"; // the source's name in the Store
        bool checksums = true;         // work out every file's SHA-256 (cached in library.stateDir)
        std::string version;           // the program's, on the status page
        std::string where;             // what the status page says is served ("" = the folders' paths)
    };
    // one request worth showing: a list read, a file fetched or resumed
    struct Activity {
        std::time_t when = 0;
        std::string peer;
        std::string what; // "read the list", "fetches Tekken 3/t3.chd", "resumes ..."
    };

    explicit LanServer(Config config);
    ~LanServer();
    LanServer(const LanServer &) = delete;
    LanServer &operator=(const LanServer &) = delete;

    // scans, listens on every interface and starts serving; false (and why) when the port is taken
    bool start(std::string &error);
    void stop();
    bool running() const { return running_; }

    const Config &config() const { return config_; }
    LanLibrary &library() { return library_; }
    void rescan() { rescan_ = true; }
    bool hashing() const;                   // checksums still being worked out
    std::vector<Activity> activity() const; // the last 100, oldest first

private:
    void remember(const std::string &peer, const std::string &what);

    Config config_;
    LanLibrary library_;
    std::unique_ptr<HttpServer> http_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> rescan_{false};
    bool running_ = false;
    std::thread serving_, watcher_, hasher_;
    mutable std::mutex activityMutex_;
    std::deque<Activity> activity_;
};

} // namespace ableem
