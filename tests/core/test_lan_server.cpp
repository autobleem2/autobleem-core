//
// ableem_lanserver (abstored's and LAN Share's server): the read-only scan of a games folder, the TSV it serves (read
// back with the Store's own parser), the checksum cache, and an HTTP round trip with a Range.
//
#include "doctest/doctest.h"

#include "support/temp_dir.h"
#include <ableem/lanserver/http_server.h>
#include <ableem/lanserver/index_page.h>
#include <ableem/lanserver/lan_library.h>
#include <ableem/lanserver/lan_server.h>

#include <ableem/engine/filesystem.h>
#include <ableem/engine/sha256.h>
#include <ableem/engine/store_catalog.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define TEST_CLOSE closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define TEST_CLOSE close
#endif

using namespace std;
using namespace ableem;

namespace {

// every file under dir with its size - to prove the scan wrote nothing
string listing(const string &dir) {
    string out;
    for (const ableem::DirEntry &e : ableem::DirEntry::diru(dir)) {
        const string path = dir + "/" + e.name;
        out += path + (e.isDir ? "/\n" : "|" + to_string(ableem::DirEntry::fileSize(path)) + "\n");
        if (e.isDir)
            out += listing(path);
    }
    return out;
}

struct Games {
    Games() : tmp("lan") {
        tmp.makeSubDir("Games/Two Discs");
        tmp.writeFile("Games/Two Discs/Game (Disc 2).chd", "disc two");
        tmp.writeFile("Games/Two Discs/Game (Disc 1).chd", "disc one");
        tmp.writeFile("Games/Two Discs/cover.png", "png");
        tmp.makeSubDir("Games/Cue Game");
        tmp.writeFile("Games/Cue Game/game.cue", "FILE \"GAME.BIN\" BINARY\n  TRACK 01 MODE2/2352\n");
        tmp.writeFile("Games/Cue Game/game.bin", "binary data");
        tmp.makeSubDir("Games/Broken");
        tmp.writeFile("Games/Broken/b.cue", "FILE \"missing.bin\" BINARY\n");
        tmp.makeSubDir("Games/RPG/Nested");
        tmp.writeFile("Games/RPG/Nested/n.pbp", "pbp data");
        tmp.makeSubDir("Games/!SaveStates/Two Discs");
        tmp.writeFile("Games/!SaveStates/Two Discs/x.chd", "not a game");
        tmp.makeSubDir("Games/Empty");
        tmp.writeFile("Games/Empty/e.chd", "");
        tmp.writeFile("Games/loose.chd", "loose");
        tmp.makeSubDir("state");
    }
    LanLibrary::Config config() {
        LanLibrary::Config c;
        c.gamesDir = tmp.at("Games");
        c.stateDir = tmp.at("state");
        return c;
    }
    const LanGame *game(const LanSnapshot &s, const string &id) {
        for (const LanGame &g : s.games)
            if (g.id == id)
                return &g;
        return nullptr;
    }
    bool problem(const LanSnapshot &s, const string &path, const string &part) {
        for (const LanProblem &p : s.problems)
            if (p.path == path && p.what.find(part) != string::npos)
                return true;
        return false;
    }
    TempDir tmp;
};

} // namespace

TEST_CASE("abstored scans a games folder read-only: discs in order, cues with their files, problems said") {
    Games g;
    const string before = listing(g.tmp.at("Games"));
    LanLibrary library(g.config());
    library.scan();
    CHECK(listing(g.tmp.at("Games")) == before); // nothing written, renamed or repaired

    const auto snap = library.snapshot();
    REQUIRE(snap->games.size() == 3); // Two Discs, Cue Game, RPG/Nested
    const LanGame *two = g.game(*snap, "Two Discs");
    REQUIRE(two != nullptr);
    REQUIRE(two->files.size() == 2);
    CHECK(two->files[0].name == "Game (Disc 1).chd");
    CHECK(two->files[0].disc == 1);
    CHECK(two->files[1].disc == 2);
    CHECK(two->coverFile.find("cover.png") != string::npos);
    CHECK(two->title == "Two Discs"); // no Game.ini, no database: the folder's name

    const LanGame *cue = g.game(*snap, "Cue Game");
    REQUIRE(cue != nullptr);
    REQUIRE(cue->files.size() == 2);
    CHECK(cue->files[0].name == "game.cue");
    CHECK(cue->files[0].disc == 1);
    CHECK(cue->files[1].name == "game.bin"); // found whatever the case the cue names it in
    CHECK(cue->files[1].disc == 0);

    CHECK(g.game(*snap, "RPG/Nested") != nullptr);
    CHECK(g.game(*snap, "Broken") == nullptr);
    CHECK(g.game(*snap, "Empty") == nullptr);
    CHECK(g.problem(*snap, "Broken/b.cue", "missing.bin"));
    CHECK(g.problem(*snap, "Empty/e.chd", "empty"));
    CHECK(g.problem(*snap, "", "loose"));
    CHECK(g.problem(*snap, "Two Discs", "no serial")); // a warning: still served

    CHECK(library.servablePath("Two Discs/Game (Disc 1).chd") == g.tmp.at("Games") + "/Two Discs/Game (Disc 1).chd");
    CHECK(library.servablePath("!SaveStates/Two Discs/x.chd").empty());
    CHECK(library.servablePath("../outside.chd").empty());
    CHECK(library.servablePath("loose.chd").empty());
}

TEST_CASE("abstored's TSV is what the Store reads: one item per game, discs numbered, URLs encoded") {
    Games g;
    LanLibrary library(g.config());
    library.scan();
    const string tsv = LanLibrary::tsv(*library.snapshot(), {}, "http://10.0.0.5:8124", "Test Games");
    CHECK(tsv.find("http://10.0.0.5:8124/files/Two%20Discs/Game%20%28Disc%201%29.chd") != string::npos);

    ableem::StoreSourceTsv source = ableem::StoreSourceTsv::parse(tsv, "x");
    CHECK(source.name == "Test Games");
    CHECK(source.problems.empty());
    REQUIRE(source.items.size() == 3);
    const ableem::StoreItem *two = nullptr;
    for (const ableem::StoreItem &item : source.items)
        if (item.title == "Two Discs")
            two = &item;
    REQUIRE(two != nullptr);
    REQUIRE(two->files.size() == 2);
    CHECK(two->files[0].name == "Game (Disc 1).chd");
    CHECK(two->files[0].size == 8);
    CHECK(two->image == "http://10.0.0.5:8124/cover/Two%20Discs"); // it has a picture in its folder
}

TEST_CASE("abstored: two games of one title are told apart by their folders") {
    Games g;
    g.tmp.makeSubDir("Games/A");
    g.tmp.writeFile("Games/A/a.chd", "a");
    g.tmp.writeFile("Games/A/Game.ini", "[Game]\nTitle=Same Game\n");
    g.tmp.makeSubDir("Games/B");
    g.tmp.writeFile("Games/B/b.chd", "b");
    g.tmp.writeFile("Games/B/Game.ini", "[Game]\nTitle=Same Game\n");
    LanLibrary library(g.config());
    library.scan();
    const auto snap = library.snapshot();
    REQUIRE(g.game(*snap, "A") != nullptr);
    CHECK(g.game(*snap, "A")->title == "Same Game (A)");
    CHECK(g.game(*snap, "B")->title == "Same Game (B)");
}

TEST_CASE("abstored: a community disc without a serial is served under its own name, not reported") {
    Games g;
    g.tmp.makeSubDir("Games/RE1.5 (MZD)");
    g.tmp.writeFile("Games/RE1.5 (MZD)/BH2.cue", "FILE \"BH2.bin\" BINARY\n");
    g.tmp.writeFile("Games/RE1.5 (MZD)/BH2.bin", "no serial in here");
    g.tmp.writeFile("Games/RE1.5 (MZD)/Game.ini", "[Game]\nTitle=RE1.5 (MZD)\nSerial=\n");
    LanLibrary library(g.config());
    library.scan();
    const auto snap = library.snapshot();
    const LanGame *re = g.game(*snap, "RE1.5 (MZD)");
    REQUIRE(re != nullptr);
    CHECK(re->title == "Resident Evil 1.5");
    CHECK_FALSE(g.problem(*snap, "RE1.5 (MZD)", "no serial"));
}

TEST_CASE("abstored works the checksums out once and keeps them outside the games folder") {
    Games g;
    {
        LanLibrary library(g.config());
        library.scan();
        library.hashPending([] { return false; });
        const auto sums = library.checksums();
        CHECK(sums.size() == 5);
        CHECK(sums.at("Two Discs/Game (Disc 1).chd|8") == ableem::Sha256::ofString("disc one"));
        const string tsv = LanLibrary::tsv(*library.snapshot(), sums, "http://h", "n");
        CHECK(tsv.find(ableem::Sha256::ofString("disc one")) != string::npos);
    }
    CHECK(ableem::DirEntry::exists(g.tmp.at("state/checksums.tsv")));
    LanLibrary again(g.config());
    CHECK(again.checksums().size() == 5); // read back, nothing hashed again

    // a stop is honoured between files
    LanLibrary::Config noState;
    noState.gamesDir = g.tmp.at("Games");
    LanLibrary stopped(noState);
    stopped.scan();
    stopped.hashPending([] { return true; });
    CHECK(stopped.checksums().empty());
}

TEST_CASE("HttpServer::parseRange and decodePercent") {
    uint64_t first = 0, last = 0;
    bool unsatisfiable = false;
    REQUIRE(HttpServer::parseRange("bytes=10-", 100, first, last, unsatisfiable));
    CHECK((first == 10 && last == 99));
    REQUIRE(HttpServer::parseRange("bytes=10-19", 100, first, last, unsatisfiable));
    CHECK((first == 10 && last == 19));
    REQUIRE(HttpServer::parseRange("bytes=-30", 100, first, last, unsatisfiable));
    CHECK((first == 70 && last == 99));
    REQUIRE(HttpServer::parseRange("bytes=90-500", 100, first, last, unsatisfiable));
    CHECK(last == 99);
    CHECK_FALSE(HttpServer::parseRange("bytes=100-", 100, first, last, unsatisfiable));
    CHECK(unsatisfiable);
    CHECK_FALSE(HttpServer::parseRange("bytes=0-1,5-6", 100, first, last, unsatisfiable));
    CHECK_FALSE(unsatisfiable);
    CHECK_FALSE(HttpServer::parseRange("items=1-2", 100, first, last, unsatisfiable));
    CHECK(HttpServer::decodePercent("/files/Two%20Discs/A%28B%29.chd") == "/files/Two Discs/A(B).chd");
    CHECK(htmlEscape("<a & \"b\">") == "&lt;a &amp; &quot;b&quot;&gt;");
}

TEST_CASE("abstored over a socket: the list, a file resumed with a Range, a file it does not serve") {
    Games g;
    LanLibrary library(g.config());
    library.scan();
    HttpServer server([&](const HttpServer::Request &r) {
        if (r.path == "/store.tsv") {
            HttpServer::Response res;
            res.body = LanLibrary::tsv(*library.snapshot(), {}, "http://" + r.headers.at("host"), "n");
            return res;
        }
        if (r.path.compare(0, 7, "/files/") == 0 && !library.servablePath(r.path.substr(7)).empty()) {
            HttpServer::Response res;
            res.file = library.servablePath(r.path.substr(7));
            return res;
        }
        return HttpServer::Response::text(404, "no\n");
    });
    const int port = 20000 + static_cast<int>(chrono::steady_clock::now().time_since_epoch().count() % 20000);
    string error;
    REQUIRE(server.listen(port, error));
    atomic<bool> stop{false};
    thread serving([&] { server.serve(stop); });

    auto ask = [&](const string &request) {
        const int s = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
        string reply;
        if (connect(s, reinterpret_cast<sockaddr *>(&to), sizeof(to)) == 0) {
            send(s, request.data(), static_cast<int>(request.size()), 0);
            char buffer[4096];
            int got;
            while ((got = static_cast<int>(recv(s, buffer, sizeof(buffer), 0))) > 0)
                reply.append(buffer, static_cast<size_t>(got));
        }
        TEST_CLOSE(s);
        return reply;
    };
    const string list = ask("GET /store.tsv HTTP/1.1\r\nHost: 10.1.2.3:" + to_string(port) + "\r\n\r\n");
    CHECK(list.compare(0, 15, "HTTP/1.1 200 OK") == 0);
    CHECK(list.find("http://10.1.2.3:" + to_string(port) + "/files/Cue%20Game/game.bin") != string::npos);

    const string part =
        ask("GET /files/Cue%20Game/game.bin HTTP/1.1\r\nHost: h\r\nRange: bytes=7-\r\n\r\n"); // "binary data"
    CHECK(part.compare(0, 12, "HTTP/1.1 206") == 0);
    CHECK(part.find("Content-Range: bytes 7-10/11") != string::npos);
    CHECK(part.substr(part.size() - 4) == "data");

    CHECK(ask("GET /files/loose.chd HTTP/1.1\r\nHost: h\r\n\r\n").compare(0, 12, "HTTP/1.1 404") == 0);
    CHECK(ask("PATCH /store.tsv HTTP/1.1\r\nHost: h\r\n\r\n").compare(0, 12, "HTTP/1.1 405") == 0);
    stop = true;
    serving.join();
}

TEST_CASE("LanLibrary serves several folders: each game's id and path start with its folder's name") {
    TempDir tmp("lanroots");
    tmp.makeSubDir("A/Tekken 3");
    tmp.writeFile("A/Tekken 3/t3.chd", "from a");
    tmp.makeSubDir("B/Tekken 3");
    tmp.writeFile("B/Tekken 3/t3.chd", "from b, longer");
    tmp.writeFile("B/loose.chd", "loose");
    tmp.makeSubDir("state");

    LanLibrary::Config c;
    c.roots = {{"Living room", tmp.at("A")}, {"Attic", tmp.at("B")}, {"Gone", tmp.at("C")}};
    c.stateDir = tmp.at("state");
    LanLibrary library(c);
    library.scan();
    const auto snap = library.snapshot();

    REQUIRE(snap->games.size() == 2);
    map<string, const LanGame *> byId;
    for (const LanGame &g : snap->games)
        byId[g.id] = &g;
    REQUIRE(byId.count("Living room/Tekken 3"));
    REQUIRE(byId.count("Attic/Tekken 3"));
    // one title in two folders: told apart by the id, as two folders of one library are
    CHECK(byId["Attic/Tekken 3"]->title == "Tekken 3 (Attic/Tekken 3)");
    const LanFile &fromB = byId["Attic/Tekken 3"]->files.front();
    CHECK(fromB.relPath == "Attic/Tekken 3/t3.chd");
    CHECK(fromB.name == "t3.chd");

    // each path back to its own folder, and only what the scan listed
    CHECK(library.servablePath("Attic/Tekken 3/t3.chd") == tmp.at("B") + "/Tekken 3/t3.chd");
    CHECK(library.servablePath("Living room/Tekken 3/t3.chd") == tmp.at("A") + "/Tekken 3/t3.chd");
    CHECK(library.servablePath("Tekken 3/t3.chd").empty());
    CHECK(library.absolutePath("Nowhere/x.chd").empty());

    // the problems carry their folder's name too
    bool loose = false, gone = false;
    for (const LanProblem &p : snap->problems) {
        loose = loose || (p.path == "Attic" && p.what.find("loose") != string::npos);
        gone = gone || (p.path == "Gone" && p.what.find("is not there") != string::npos);
    }
    CHECK(loose);
    CHECK(gone);

    // the TSV names each file under its folder
    const string tsv = LanLibrary::tsv(*snap, {}, "http://h:1", "x");
    CHECK(tsv.find("http://h:1/files/Attic/Tekken%203/t3.chd") != string::npos);
    CHECK(tsv.find("http://h:1/files/Living%20room/Tekken%203/t3.chd") != string::npos);

    // the checksums work from the prefixed paths
    library.hashPending([] { return false; });
    CHECK(library.checksums().count(LanLibrary::checksumKey(fromB)) == 1);

    // a change in any folder shows in the fingerprint
    const string before = library.fingerprint();
    tmp.writeFile("B/Tekken 3/t3.sbi", "sbi");
    CHECK(library.fingerprint() != before);
}

TEST_CASE("LanLibrary with one root serves it like gamesDir, without a prefix") {
    TempDir tmp("lanroot1");
    tmp.makeSubDir("A/Tekken 3");
    tmp.writeFile("A/Tekken 3/t3.chd", "x");
    LanLibrary::Config c;
    c.roots = {{"Only", tmp.at("A")}};
    LanLibrary library(c);
    library.scan();
    REQUIRE(library.snapshot()->games.size() == 1);
    CHECK(library.snapshot()->games.front().id == "Tekken 3");
    CHECK(library.servablePath("Tekken 3/t3.chd") == tmp.at("A") + "/Tekken 3/t3.chd");
}

namespace {

// one request to 127.0.0.1:port, the whole reply
string httpGet(int port, const string &path) {
    const int s = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    string reply;
    if (connect(s, reinterpret_cast<sockaddr *>(&to), sizeof(to)) == 0) {
        const string request = "GET " + path + " HTTP/1.1\r\nHost: 10.9.8.7:" + to_string(port) + "\r\n\r\n";
        send(s, request.data(), static_cast<int>(request.size()), 0);
        char buffer[4096];
        int got;
        while ((got = static_cast<int>(recv(s, buffer, sizeof(buffer), 0))) > 0)
            reply.append(buffer, static_cast<size_t>(got));
    }
    TEST_CLOSE(s);
    return reply;
}

} // namespace

TEST_CASE("LanServer serves the library: the list, a file, the status page, what was asked for; stops and starts") {
    Games g;
    LanServer::Config c;
    c.library = g.config();
    c.port = 20000 + static_cast<int>((chrono::steady_clock::now().time_since_epoch().count() / 7) % 20000);
    c.name = "Living room";
    c.version = "9.9";
    LanServer server(c);
    string error;
    REQUIRE_MESSAGE(server.start(error), error);
    CHECK(server.running());

    const string list = httpGet(c.port, "/store.tsv");
    CHECK(list.find("# name: Living room") != string::npos);
    CHECK(list.find("http://10.9.8.7:" + to_string(c.port) + "/files/Cue%20Game/game.cue") != string::npos);
    CHECK(httpGet(c.port, "/files/Cue%20Game/game.bin").find("binary data") != string::npos);
    const string page = httpGet(c.port, "/");
    CHECK(page.find("Living room") != string::npos);
    CHECK(page.find("9.9") != string::npos);
    CHECK(httpGet(c.port, "/nothing").compare(0, 12, "HTTP/1.1 404") == 0);

    // the same for a program: the games with their files, the problems, the folders, uploads off
    const string status = httpGet(c.port, "/status.json");
    CHECK(status.find("Content-Type: application/json") != string::npos);
    CHECK(status.find("\"schema\": 1") != string::npos);
    CHECK(status.find("\"name\": \"Living room\"") != string::npos);
    CHECK(status.find("\"uploads\": false") != string::npos);
    CHECK(status.find("\"id\": \"Two Discs\"") != string::npos);
    CHECK(status.find("\"name\": \"Game (Disc 2).chd\"") != string::npos);
    CHECK(status.find("\"path\": \"Broken/b.cue\"") != string::npos);
    CHECK(status.find("\"free\":") != string::npos);

    const auto activity = server.activity();
    REQUIRE(activity.size() == 2);
    CHECK(activity[0].what == "read the list");
    CHECK(activity[1].what == "fetches Cue Game/game.bin");
    CHECK(activity[1].peer.find("127.0.0.1") != string::npos);

    // a second server on the same port is refused, and says why
    LanServer other(c);
    CHECK_FALSE(other.start(error));
    CHECK_FALSE(other.running());

    server.stop();
    CHECK_FALSE(server.running());
    CHECK(httpGet(c.port, "/store.tsv").empty()); // nobody listens any more
    REQUIRE_MESSAGE(server.start(error), error);  // and it starts again on the same port
    CHECK(httpGet(c.port, "/store.tsv").find("# name: Living room") != string::npos);
}

namespace {

// any request, a body with it; the whole reply
string httpSend(int port, const string &method, const string &path, const string &body = "", const string &token = "") {
    const int s = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    string reply;
    if (connect(s, reinterpret_cast<sockaddr *>(&to), sizeof(to)) == 0) {
        string request = method + " " + path + " HTTP/1.1\r\nHost: h\r\nContent-Length: " + to_string(body.size()) +
                         "\r\n" + (token.empty() ? "" : "X-AB-Token: " + token + "\r\n") + "\r\n" + body;
        send(s, request.data(), static_cast<int>(request.size()), 0);
        char buffer[4096];
        int got;
        while ((got = static_cast<int>(recv(s, buffer, sizeof(buffer), 0))) > 0)
            reply.append(buffer, static_cast<size_t>(got));
    }
    TEST_CLOSE(s);
    return reply;
}

bool status(const string &reply, int code) {
    return reply.compare(0, 12, "HTTP/1.1 " + to_string(code)) == 0;
}

string body(const string &reply) {
    const size_t at = reply.find("\r\n\r\n");
    return at == string::npos ? "" : reply.substr(at + 4);
}

} // namespace

TEST_CASE(
    "LanServer uploads: off by default; with the token a game arrives a file at a time, resumable, then committed") {
    Games g;
    LanServer::Config c;
    c.library = g.config();
    c.port = 20000 + static_cast<int>((chrono::steady_clock::now().time_since_epoch().count() / 11) % 20000);
    {
        LanServer off(c);
        string error;
        REQUIRE_MESSAGE(off.start(error), error);
        const string reply = httpSend(c.port, "PUT", "/upload/New/new.cue", "x", "t");
        CHECK(status(reply, 403));
        CHECK(body(reply).find("uploads are off") != string::npos);
    }
    c.uploads = true;
    c.uploadToken = "secret";
    LanServer server(c);
    string error;
    REQUIRE_MESSAGE(server.start(error), error);

    CHECK(status(httpSend(c.port, "POST", "/store.tsv", "x", "secret"), 405)); // only an upload writes
    CHECK(status(httpSend(c.port, "PUT", "/upload/New/new.cue", "x", "wrong"), 403));
    CHECK(status(httpSend(c.port, "PUT", "/upload/New/new.cue", "x"), 403));
    CHECK(status(httpSend(c.port, "PUT", "/upload/../escape.cue", "x", "secret"), 400));
    CHECK(status(httpSend(c.port, "PUT", "/upload/New/..%2F..%2Fx", "x", "secret"), 400));
    CHECK(status(httpSend(c.port, "PUT", "/upload/New/a%3Ab", "x", "secret"), 400)); // "a:b"

    // a file in two parts, the second going on from the first
    const string cue = "FILE \"new.bin\" BINARY\n  TRACK 01 MODE2/2352\n";
    CHECK(status(httpSend(c.port, "PUT", "/upload/Two%20Discs/new.cue?offset=0", cue, "secret"), 200));
    CHECK(status(httpSend(c.port, "PUT", "/upload/Two%20Discs/new.bin?offset=0", "binary ", "secret"), 200));
    const string stale = httpSend(c.port, "PUT", "/upload/Two%20Discs/new.bin?offset=3", "again", "secret");
    CHECK(status(stale, 409)); // not where the staged file ends: it says where that is
    CHECK(body(stale) == "7\n");
    CHECK(body(httpSend(c.port, "GET", "/upload/Two%20Discs/new.bin", "", "secret")) == "7\n");
    CHECK(body(httpSend(c.port, "PUT", "/upload/Two%20Discs/new.bin?offset=7", "data", "secret")) == "11\n");
    // staged in a dot folder: not a game yet
    server.library().scan();
    CHECK(server.library().snapshot()->games.size() == 3);

    // committed under a free name ("Two Discs" is taken), then scanned
    const string committed = httpSend(c.port, "POST", "/upload/Two%20Discs?commit", "", "secret");
    CHECK(status(committed, 200));
    CHECK(body(committed) == "Two Discs (2)\n");
    CHECK(g.tmp.readFile("Games/Two Discs (2)/new.bin") == "binary data");
    CHECK_FALSE(ableem::DirEntry::exists(g.tmp.at("Games/.uploading/Two Discs")));
    server.library().scan();
    CHECK(server.library().snapshot()->games.size() == 4);
    CHECK(server.activity().back().what == "uploaded Two Discs (2)");

    // nothing staged: nothing to commit; a staged folder can be dropped
    CHECK(status(httpSend(c.port, "POST", "/upload/Nothing?commit", "", "secret"), 404));
    CHECK(status(httpSend(c.port, "PUT", "/upload/Gone/x.cue?offset=0", "x", "secret"), 200));
    CHECK(status(httpSend(c.port, "DELETE", "/upload/Gone", "", "secret"), 200));
    CHECK_FALSE(ableem::DirEntry::exists(g.tmp.at("Games/.uploading/Gone")));
    CHECK(httpSend(c.port, "POST", "/upload/Gone?commit", "", "secret").compare(0, 12, "HTTP/1.1 404") == 0);
}
