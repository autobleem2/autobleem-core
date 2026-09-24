//
// LanServer - see the header. The routes are abstored's, as they were in its main.cpp.
//
#include <ableem/lanserver/http_server.h>
#include <ableem/lanserver/index_page.h>
#include <ableem/lanserver/lan_server.h>

#include <ableem/engine/filesystem.h>
#include <ableem/engine/log.h>

#include <chrono>
#include <fstream>
#include <json.h>
#include <vector>

using namespace std;

namespace ableem {

namespace {

const size_t ActivityKept = 100;
const char *const Staging = ".uploading"; // <root>/.uploading/<game folder>/ - a dot folder, never scanned

// a query's value by its key ("offset=10&library=A%20B"), percent-decoded; "" when absent
string param(const string &query, const string &key) {
    for (size_t at = 0;;) {
        const size_t amp = query.find('&', at);
        const string pair = query.substr(at, amp == string::npos ? string::npos : amp - at);
        const size_t eq = pair.find('=');
        if (pair.substr(0, eq) == key)
            return eq == string::npos ? "" : HttpServer::decodePercent(pair.substr(eq + 1));
        if (amp == string::npos)
            return "";
        at = amp + 1;
    }
}

bool has(const string &query, const string &key) {
    for (size_t at = 0;;) {
        const size_t amp = query.find('&', at);
        const string pair = query.substr(at, amp == string::npos ? string::npos : amp - at);
        if (pair.substr(0, pair.find('=')) == key)
            return true;
        if (amp == string::npos)
            return false;
        at = amp + 1;
    }
}

// a name a game folder or a file of it may have on any disk the games live on, FAT included: no separator,
// no "." or "..", nothing Windows refuses, no dot first (the staging and state folders are dot folders)
bool safeName(const string &name) {
    if (name.empty() || name.size() > 200 || name[0] == '.' || name.back() == ' ' || name.back() == '.')
        return false;
    for (unsigned char c : name)
        if (c < 32 || string("<>:\"/\\|?*").find(static_cast<char>(c)) != string::npos)
            return false;
    return true;
}

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
        // only an upload or a removal writes; everything else is read
        if (request.method == "DELETE" && path.compare(0, 7, "/games/") == 0)
            return remove(request);
        if (request.method != "GET" && request.method != "HEAD" && path.compare(0, 8, "/upload/") != 0)
            return HttpServer::Response::text(405, "GET and HEAD only here\n");
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
            facts.uploads = config_.uploads;
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
        if (path == "/status.json") {
            HttpServer::Response r;
            r.contentType = "application/json; charset=utf-8";
            r.body = statusJson();
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
        if (path.compare(0, 8, "/upload/") == 0)
            return upload(request);
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

//*******************************
// LanServer::allowed / remove
//*******************************
// what may write: uploads switched on, and the token given
bool LanServer::allowed(const HttpServer::Request &request, HttpServer::Response &refusal) const {
    if (!config_.uploads) {
        refusal = HttpServer::Response::text(403, "uploads are off on this server (abstored --allow-uploads)\n");
        return false;
    }
    auto token = request.headers.find("x-ab-token");
    if (config_.uploadToken.empty() || token == request.headers.end() || token->second != config_.uploadToken) {
        refusal = HttpServer::Response::text(403, "wrong upload token\n");
        return false;
    }
    return true;
}

// DELETE /games/<game id>: a game the last scan listed, taken off the server - its folder moved into
// <its root>/.removed/ (" (2)" when a removed game of that name is there already), never deleted; a dot folder,
// so never scanned. Emptying .removed is for whoever has the server's disk.
HttpServer::Response LanServer::remove(const HttpServer::Request &request) {
    HttpServer::Response refusal;
    if (!allowed(request, refusal))
        return refusal;
    const string id = request.path.substr(7);
    bool listed = false;
    for (const LanGame &g : library_.snapshot()->games)
        listed = listed || g.id == id;
    if (!listed)
        return HttpServer::Response::text(404, "no game " + id + " on this server\n");
    string rootDir;
    for (const LanLibrary::Root &r : library_.effectiveRoots())
        if (rootDir.empty() && (r.name.empty() || id.compare(0, r.name.size() + 1, r.name + "/") == 0))
            rootDir = r.dir;
    const string source = library_.absolutePath(id);
    const size_t slash = id.find_last_of('/');
    const string name = slash == string::npos ? id : id.substr(slash + 1);
    const string removed = rootDir + sep + ".removed";
    DirEntry::createDirs(removed);
    string dest = removed + sep + name;
    for (int n = 2; DirEntry::exists(dest); n++)
        dest = removed + sep + name + " (" + to_string(n) + ")";
    if (source.empty() || rootDir.empty() || !DirEntry::renameFile(source, dest))
        return HttpServer::Response::text(500, "cannot move " + id + " out of the games\n");
    rescan_ = true;
    PLOG_INFO << request.peer << " removed " << id << " (kept in " << dest << ")";
    remember(request.peer, "removed " + id);
    return HttpServer::Response::text(200, "removed\n");
}

//*******************************
// LanServer::upload
//*******************************
// GET    /upload/<game folder>/<file>                how much of it is staged ("0" for nothing)
// PUT    /upload/<game folder>/<file>?offset=N       appends the body at N: 0 starts the file over, else N
//                                                     must be what is staged already
// POST   /upload/<game folder>?commit                 the staged folder into the games, a rescan asked for
// DELETE /upload/<game folder>                        the staged folder dropped
// Each with the token in X-AB-Token; ?library=<name> picks a folder when there are several.
HttpServer::Response LanServer::upload(const HttpServer::Request &request) {
    HttpServer::Response refusal;
    if (!allowed(request, refusal))
        return refusal;

    const string rest = request.path.substr(8);
    const size_t slash = rest.find('/');
    const string folder = rest.substr(0, slash);
    const string file = slash == string::npos ? "" : rest.substr(slash + 1);
    if (!safeName(folder) || (slash != string::npos && !safeName(file)))
        return HttpServer::Response::text(400, "not a name a game or its file may have\n");

    const vector<LanLibrary::Root> roots = library_.effectiveRoots();
    const string wanted = param(request.query, "library");
    const LanLibrary::Root *root = nullptr;
    for (const LanLibrary::Root &r : roots)
        if (r.name == wanted || (wanted.empty() && roots.size() == 1))
            root = &r;
    if (root == nullptr)
        return HttpServer::Response::text(400, "which library? (library=<name>)\n");
    const string staged = root->dir + sep + Staging + sep + folder;

    if (request.method == "DELETE" && file.empty()) {
        DirEntry::removeDirAndContents(staged);
        return HttpServer::Response::text(200, "dropped\n");
    }
    if (request.method == "POST" && file.empty() && has(request.query, "commit")) {
        if (!DirEntry::isDirectory(staged) || DirEntry::diru(staged).empty())
            return HttpServer::Response::text(404, "nothing staged for " + folder + "\n");
        string name = folder, dest = root->dir + sep + folder;
        for (int n = 2; DirEntry::exists(dest); n++) {
            name = folder + " (" + to_string(n) + ")";
            dest = root->dir + sep + name;
        }
        if (!DirEntry::renameFile(staged, dest))
            return HttpServer::Response::text(500, "cannot move the game into place\n");
        rescan_ = true;
        PLOG_INFO << request.peer << " uploaded " << name;
        remember(request.peer, "uploaded " + name);
        return HttpServer::Response::text(200, name + "\n");
    }
    if (file.empty())
        return HttpServer::Response::text(400, "a file, or ?commit\n");

    const string target = staged + sep + file;
    const long long found = DirEntry::exists(target) ? DirEntry::fileSize(target) : 0;
    const long long current = found < 0 ? 0 : found;
    if (request.method == "GET" || request.method == "HEAD")
        return HttpServer::Response::text(200, to_string(current) + "\n");
    if (request.method != "PUT")
        return HttpServer::Response::text(405, "GET, PUT, POST ?commit or DELETE\n");

    const long long offset = atoll(param(request.query, "offset").c_str());
    if (offset != 0 && offset != current)
        return HttpServer::Response::text(409, to_string(current) + "\n"); // what is there: go on from it
    if (request.contentLength > LanLibrary::freeSpace(root->dir))
        return HttpServer::Response::text(507, "not enough space for " + file + "\n");
    DirEntry::createDirs(staged);
    ofstream out(target, ios::binary | (offset == 0 ? ios::trunc : ios::app));
    if (!out)
        return HttpServer::Response::text(500, "cannot write " + file + "\n");
    vector<char> buffer(1 << 20);
    uint64_t written = 0;
    while (written < request.contentLength) {
        const long long got = request.readBody(buffer.data(), buffer.size());
        if (got <= 0)
            break; // the client went: what came is kept, the next PUT goes on from it
        out.write(buffer.data(), static_cast<streamsize>(got));
        if (!out)
            return HttpServer::Response::text(500, "cannot write " + file + "\n");
        written += static_cast<uint64_t>(got);
    }
    out.close();
    return HttpServer::Response::text(200, to_string(offset + static_cast<long long>(written)) + "\n");
}

//*******************************
// LanServer::statusJson
//*******************************
// {"schema": 1, "name", "version", "hashing", "uploads", "scannedAt",
//  "libraries": [{"name", "free"}], "games": [{"id", "title", "serial", "size", "discs", "files": [{"name",
//  "size", "disc"}]}], "problems": [{"path", "what", "error"}]}
string LanServer::statusJson() const {
    using nlohmann::json;
    const auto snap = library_.snapshot();
    json j;
    j["schema"] = 1;
    j["name"] = config_.name;
    j["version"] = config_.version;
    j["hashing"] = hashing();
    j["uploads"] = config_.uploads;
    j["scannedAt"] = static_cast<long long>(snap->scannedAt);
    json libraries = json::array();
    for (const LanLibrary::Root &r : library_.effectiveRoots())
        libraries.push_back({{"name", r.name}, {"free", LanLibrary::freeSpace(r.dir)}});
    j["libraries"] = libraries;
    json games = json::array();
    for (const LanGame &g : snap->games) {
        json files = json::array();
        int discs = 0;
        for (const LanFile &f : g.files) {
            files.push_back({{"name", f.name}, {"size", f.size}, {"disc", f.disc}});
            discs += f.disc > 0 ? 1 : 0;
        }
        games.push_back({{"id", g.id},
                         {"title", g.title},
                         {"serial", g.serial},
                         {"size", g.size()},
                         {"discs", discs},
                         {"files", files}});
    }
    j["games"] = games;
    json problems = json::array();
    for (const LanProblem &p : snap->problems)
        problems.push_back({{"path", p.path}, {"what", p.what}, {"error", p.error}});
    j["problems"] = problems;
    return j.dump(1);
}

vector<LanServer::Activity> LanServer::activity() const {
    lock_guard<mutex> lock(activityMutex_);
    return vector<Activity>(activity_.begin(), activity_.end());
}

} // namespace ableem
