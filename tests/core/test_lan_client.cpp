//
// LanClient and Publisher against a LanServer in the same process: what LAN Share does with a remote abstored -
// its status, a game published over HTTP (a stopped upload going on from what was staged), a game copied to
// the server's share, a taken name, a wrong token, a server that is not there - and the helpers.
//
#include "doctest/doctest.h"
#include "support/temp_dir.h"

#include <ableem/lanserver/lan_client.h>
#include <ableem/lanserver/lan_server.h>
#include <ableem/lanserver/publisher.h>

#include <ableem/engine/filesystem.h>

#include <chrono>
#include <string>

using namespace std;
using namespace ableem;

namespace {

struct Rig {
    Rig() : tmp("lanclient") {
        // the server's games
        tmp.makeSubDir("Server/Tekken 3");
        tmp.writeFile("Server/Tekken 3/t3.chd", "tekken");
        tmp.makeSubDir("state");
        // a folder of games on "the PC"
        tmp.makeSubDir("PC/Cue Game");
        tmp.writeFile("PC/Cue Game/game.cue", "FILE \"game.bin\" BINARY\n  TRACK 01 MODE2/2352\n");
        tmp.writeFile("PC/Cue Game/game.bin", string(3 * 1024 * 1024, 'b')); // big enough to be sent in parts
        tmp.writeFile("PC/Cue Game/cover.png", "png");
        port = 20000 + static_cast<int>((chrono::steady_clock::now().time_since_epoch().count() / 13) % 20000);
        LanServer::Config c;
        c.library.gamesDir = tmp.at("Server");
        c.library.stateDir = tmp.at("state");
        c.port = port;
        c.name = "Pi";
        c.checksums = false;
        c.uploads = true;
        c.uploadToken = "tok";
        server.reset(new LanServer(c));
        string error;
        started = server->start(error);
    }
    string url() const { return "http://127.0.0.1:" + to_string(port) + "/store.tsv"; }
    TempDir tmp;
    int port = 0;
    unique_ptr<LanServer> server;
    bool started = false;
};

// the PC's folder, scanned the way LAN Share scans it
const LanGame *pcGame(LanLibrary &pc, const string &id) {
    pc.scan();
    for (const LanGame &g : pc.snapshot()->games)
        if (g.id == id)
            return &g;
    return nullptr;
}

} // namespace

TEST_CASE("LanClient reads a server's status; an address it cannot use, or a server not there, says so") {
    Rig rig;
    REQUIRE(rig.started);
    LanClient client(rig.url()); // the Store's source URL is fine as the address
    CHECK(client.valid());
    CHECK(client.baseUrl() == "http://127.0.0.1:" + to_string(rig.port));
    const LanClient::Status s = client.status();
    REQUIRE_MESSAGE(s.ok, s.error);
    CHECK(s.name == "Pi");
    CHECK(s.uploads);
    REQUIRE(s.games.size() == 1);
    CHECK(s.games[0].id == "Tekken 3");
    CHECK(s.games[0].discs == 1);
    REQUIRE(s.libraries.size() == 1);
    CHECK(s.libraries[0].free > 0);
    string error;
    CHECK(client.rescan(error));

    CHECK_FALSE(LanClient("https://x:1").valid());
    CHECK_FALSE(LanClient("http://x:99999").valid());
    CHECK(LanClient("192.168.1.5:8126").baseUrl() == "http://192.168.1.5:8126");

    rig.server->stop();
    const LanClient::Status gone = client.status();
    CHECK_FALSE(gone.ok);
    CHECK(gone.error.find("does not answer") != string::npos);
}

TEST_CASE("Publisher uploads a game over HTTP; a stopped upload goes on from what the server has") {
    Rig rig;
    REQUIRE(rig.started);
    LanLibrary::Config pcConfig;
    pcConfig.gamesDir = rig.tmp.at("PC");
    LanLibrary pc(pcConfig);
    const LanGame *game = pcGame(pc, "Cue Game");
    REQUIRE(game != nullptr);
    const vector<Publisher::File> files = Publisher::filesOf(*game, pc);
    REQUIRE(files.size() == 3); // the cue, the bin it names, the picture
    CHECK(files[2].name == "cover.png");

    LanClient client(rig.url(), "tok");
    Publisher::Target target;
    target.client = &client;

    // stopped past the first megabyte: nothing appears, what came stays staged
    uint64_t seen = 0;
    const Publisher::Result stopped = Publisher::publish(files, "Cue Game", target, [&](uint64_t done, uint64_t) {
        seen = done;
        return done < 1024 * 1024;
    });
    CHECK_FALSE(stopped.ok);
    CHECK(stopped.error == "stopped");
    CHECK_FALSE(DirEntry::exists(rig.tmp.at("Server/Cue Game")));
    // the server may still be writing what came before the client went: give it a moment
    long long staged = -1;
    for (int i = 0; i < 50 && staged <= 0; i++) {
        staged = DirEntry::fileSize(rig.tmp.at("Server/.uploading/Cue Game/game.bin"));
        if (staged <= 0)
            this_thread::sleep_for(chrono::milliseconds(100));
    }
    CHECK(staged > 0);

    // again: the files go on from there, the game is committed and served
    uint64_t total = 0;
    const Publisher::Result done = Publisher::publish(files, "Cue Game", target, [&](uint64_t d, uint64_t t) {
        total = t;
        return d <= t;
    });
    REQUIRE_MESSAGE(done.ok, done.error);
    CHECK_FALSE(done.viaShare);
    CHECK(done.folder == "Cue Game");
    CHECK(total == 3 * 1024 * 1024 + rig.tmp.readFile("PC/Cue Game/game.cue").size() + 3);
    CHECK(rig.tmp.readFile("Server/Cue Game/game.bin") == rig.tmp.readFile("PC/Cue Game/game.bin"));
    CHECK(rig.tmp.readFile("Server/Cue Game/cover.png") == "png");
    rig.server->library().scan();
    CHECK(Publisher::serverHas(client.status(), "", "Cue Game"));

    // the same game again gets a folder of its own
    const Publisher::Result twice = Publisher::publish(files, "Cue Game", target, nullptr);
    REQUIRE(twice.ok);
    CHECK(twice.folder == "Cue Game (2)");

    // without the token nothing is written
    LanClient stranger(rig.url(), "nope");
    Publisher::Target other;
    other.client = &stranger;
    const Publisher::Result refused = Publisher::publish(files, "Other", other, nullptr);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "wrong upload token");
}

TEST_CASE("Publisher copies to the server's share, and asks for a rescan") {
    Rig rig;
    REQUIRE(rig.started);
    LanLibrary::Config pcConfig;
    pcConfig.gamesDir = rig.tmp.at("PC");
    LanLibrary pc(pcConfig);
    const LanGame *game = pcGame(pc, "Cue Game");
    REQUIRE(game != nullptr);
    LanClient client(rig.url()); // no token: a share needs none
    Publisher::Target target;
    target.client = &client;
    target.shareDir = rig.tmp.at("Server");

    // a stopped copy leaves nothing behind
    const Publisher::Result stopped = Publisher::publish(Publisher::filesOf(*game, pc), "Cue Game", target,
                                                         [](uint64_t d, uint64_t) { return d < 1024 * 1024; });
    CHECK_FALSE(stopped.ok);
    CHECK_FALSE(DirEntry::exists(rig.tmp.at("Server/.uploading/Cue Game")));
    CHECK_FALSE(DirEntry::exists(rig.tmp.at("Server/Cue Game")));

    const Publisher::Result r = Publisher::publish(Publisher::filesOf(*game, pc), "Tekken 3", target, nullptr);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.viaShare);
    CHECK(r.folder == "Tekken 3 (2)"); // taken on the server
    CHECK(rig.tmp.readFile("Server/Tekken 3 (2)/game.cue").find("game.bin") != string::npos);
    // the rescan it asked for: the watcher picks it up at once
    bool seen = false;
    for (int i = 0; i < 50 && !seen; i++) {
        this_thread::sleep_for(chrono::milliseconds(100));
        seen = client.status().games.size() == 2;
    }
    CHECK(seen);
}

TEST_CASE("Publisher's helpers: a folder name any disk takes; the server has it by serial, else by title") {
    CHECK(Publisher::folderNameFor("Final Fantasy VII: Disc 1") == "Final Fantasy VII - Disc 1");
    CHECK(Publisher::folderNameFor(" .What?*. ") == "What");
    CHECK(Publisher::folderNameFor("???") == "Game");

    LanClient::Status s;
    s.games = {{"a", "Tekken 3", "SCUS-94426", 1, 1}, {"b", "Homebrew", "", 1, 1}};
    CHECK(Publisher::serverHas(s, "scus-94426", "Anything"));
    CHECK_FALSE(Publisher::serverHas(s, "SLES-01234", "Tekken 3")); // both have serials, and they differ
    CHECK(Publisher::serverHas(s, "", "homebrew"));
    CHECK_FALSE(Publisher::serverHas(s, "", "Other"));
}

TEST_CASE("Publisher removes a game from the server - never deleted, kept in .removed - over HTTP or the share") {
    Rig rig;
    REQUIRE(rig.started);
    rig.tmp.makeSubDir("Server/RPG/Nested");
    rig.tmp.writeFile("Server/RPG/Nested/n.chd", "nested");
    rig.server->library().scan();

    // over HTTP: only with the token, only a game the server lists
    LanClient stranger(rig.url());
    Publisher::Target nobody;
    nobody.client = &stranger;
    string error;
    CHECK_FALSE(Publisher::remove("Tekken 3", nobody, error));
    CHECK(error == "wrong upload token");
    LanClient client(rig.url(), "tok");
    Publisher::Target target;
    target.client = &client;
    CHECK_FALSE(Publisher::remove("Not There", target, error));
    CHECK(error.find("no game") != string::npos);
    REQUIRE_MESSAGE(Publisher::remove("RPG/Nested", target, error), error);
    CHECK(rig.tmp.readFile("Server/.removed/Nested/n.chd") == "nested");
    CHECK_FALSE(DirEntry::exists(rig.tmp.at("Server/RPG/Nested")));
    bool gone = false;
    for (int i = 0; i < 50 && !gone; i++) {
        this_thread::sleep_for(chrono::milliseconds(100));
        gone = client.status().games.size() == 1;
    }
    CHECK(gone);

    // through the share: the same, done by this side
    Publisher::Target share;
    share.client = &client;
    share.shareDir = rig.tmp.at("Server");
    REQUIRE_MESSAGE(Publisher::remove("Tekken 3", share, error), error);
    CHECK(rig.tmp.readFile("Server/.removed/Tekken 3/t3.chd") == "tekken");
    CHECK_FALSE(Publisher::remove("Tekken 3", share, error)); // gone already
}
