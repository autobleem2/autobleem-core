//
// LanServer - see the header. The routes are abstored's, as they were in its main.cpp.
//
#include <ableem/lanserver/http_server.h>
#include <ableem/lanserver/index_page.h>
#include <ableem/lanserver/lan_server.h>

#include <ableem/engine/log.h>

#include <chrono>

using namespace std;

namespace ableem {

namespace {

const size_t ActivityKept = 100;

// sleeps in short steps, so a stop is noticed at once
void pause(const atomic<bool> &stopping, int tenths, const atomic<bool> *orEarlier = nullptr) {
    for (int i = 0; i < tenths && !stopping && !(orEarlier != nullptr && *orEarlier); i++)
        this_thread::sleep_for(chrono::milliseconds(100));
}

} // namespace

LanServer::LanServer(Config config) : config_(std::move(config)), library_(config_.library) {}

LanServer::~LanServer() {
    stop();
}

//*******************************
// LanServer::start / stop
//*******************************
bool LanServer::start(string &error) {
    if (running_)
        return true;
    stopping_ = false;
    library_.scan();
    http_ = make_unique<HttpServer>([this](const HttpServer::Request &request) {
        // every URL we hand out is built from the address the client used to reach us
        auto host = request.headers.find("host");
        const string baseUrl =
            "http://" + (host != request.headers.end() ? host->second : "localhost:" + to_string(config_.port));
        const string &path = request.path;
        if (path == "/" || path == "/index.html") {
            IndexPageFacts facts;
            facts.name = config_.name;
            facts.baseUrl = baseUrl;
            facts.gamesDir = config_.where;
            if (facts.gamesDir.empty()) {
                facts.gamesDir = config_.library.gamesDir;
                for (const LanLibrary::Root &r : config_.library.roots)
                    facts.gamesDir += (facts.gamesDir.empty() ? "" : ", ") + r.dir;
            }
            facts.version = config_.version;
            facts.hashing = hashing();
            HttpServer::Response r;
            r.contentType = "text/html; charset=utf-8";
            r.body = indexPage(*library_.snapshot(), library_.checksums(), facts);
            return r;
        }
        if (path == "/store.tsv") {
            HttpServer::Response r;
            r.contentType = "text/tab-separated-values; charset=utf-8";
            r.body = LanLibrary::tsv(*library_.snapshot(), library_.checksums(), baseUrl, config_.name);
            PLOG_INFO << request.peer << " read the list";
            remember(request.peer, "read the list");
            return r;
        }
        if (path == "/rescan") {
            rescan_ = true;
            return HttpServer::Response::redirect("/");
        }
        if (path.compare(0, 7, "/files/") == 0) {
            const string file = library_.servablePath(path.substr(7));
            if (file.empty())
                return HttpServer::Response::text(404, "not served here\n");
            const string what = (request.headers.count("range") ? "resumes " : "fetches ") + path.substr(7);
            PLOG_INFO << request.peer << " " << what;
            remember(request.peer, what);
            HttpServer::Response r;
            r.contentType = "application/octet-stream";
            r.file = file;
            return r;
        }
        if (path.compare(0, 7, "/cover/") == 0) {
            HttpServer::Response r;
            if (!library_.cover(path.substr(7), r.body, r.contentType))
                return HttpServer::Response::text(404, "no cover\n");
            return r;
        }
        return HttpServer::Response::text(404, "not found\n");
    });
    if (!http_->listen(config_.port, error)) {
        http_.reset();
        return false;
    }
    running_ = true;
    serving_ = thread([this] { http_->serve(stopping_); });
    // the folders watched: scanned again when anything in them changed (or /rescan asked)
    watcher_ = thread([this] {
        string last = library_.fingerprint();
        while (!stopping_) {
            pause(stopping_, 100, &rescan_); // ten seconds, or at once on /rescan
            if (stopping_)
                break;
            const string now = library_.fingerprint();
            if (now != last || rescan_.exchange(false)) {
                last = now;
                library_.scan();
            }
        }
    });
    // the checksums, a file at a time, behind everything else
    hasher_ = thread([this] {
        while (!stopping_ && config_.checksums) {
            library_.hashPending([this] { return stopping_.load(); });
            pause(stopping_, 50);
        }
    });
    return true;
}

void LanServer::stop() {
    if (!running_)
        return;
    stopping_ = true;
    for (thread *t : {&serving_, &watcher_, &hasher_})
        if (t->joinable())
            t->join();
    http_.reset();
    running_ = false;
}

//*******************************
// LanServer::hashing / activity
//*******************************
bool LanServer::hashing() const {
    if (!config_.checksums)
        return false;
    const auto sums = library_.checksums();
    for (const LanGame &g : library_.snapshot()->games)
        for (const LanFile &f : g.files)
            if (!sums.count(LanLibrary::checksumKey(f)))
                return true;
    return false;
}

void LanServer::remember(const string &peer, const string &what) {
    lock_guard<mutex> lock(activityMutex_);
    activity_.push_back({time(nullptr), peer, what});
    while (activity_.size() > ActivityKept)
        activity_.pop_front();
}

vector<LanServer::Activity> LanServer::activity() const {
    lock_guard<mutex> lock(activityMutex_);
    return vector<Activity>(activity_.begin(), activity_.end());
}

} // namespace ableem
