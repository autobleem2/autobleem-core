//
// Downloader: the platform's download command into <target>.part, the checks, the rename - and resuming what
// an earlier attempt left (docs/store-plan.md in the launcher). The command is a fake that "fetches" from a
// map of URLs, and on "resume" appends the rest of the body to what the .part holds.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/services/downloader.h"
#include "core/main.h"

#include <ableem/engine/sha256.h>

#include <fstream>
#include <map>

using namespace std;

namespace {
// "get <url> <out>" writes the body; "resume <url> <out>" appends what is missing; `cut` bytes short and a
// failing exit status when the connection is "cut"
struct FakeCommand {
    map<string, string> bodies;
    vector<string> commands;
    size_t cut = 0; // the next download stops this many bytes short, and fails

    Downloader::CommandRunner runner() {
        return [this](const string &line) {
            commands.push_back(line);
            size_t a = line.find(' '), b = line.find(' ', a + 1);
            string verb = line.substr(0, a), url = line.substr(a + 1, b - a - 1), out = line.substr(b + 1);
            auto it = bodies.find(url);
            if (it == bodies.end())
                return 22;
            string body = it->second;
            size_t from = 0;
            if (verb == "resume") {
                long long have = DirEntry::fileSize(out);
                from = have > 0 ? static_cast<size_t>(have) : 0;
            }
            string rest = body.substr(min(from, body.size()));
            bool fail = false;
            if (cut > 0 && cut < rest.size()) {
                rest = rest.substr(0, rest.size() - cut);
                fail = true;
                cut = 0;
            }
            ofstream o(out, verb == "resume" ? ios::binary | ios::app : ios::binary | ios::trunc);
            o << rest;
            return fail ? 18 : 0;
        };
    }
};
} // namespace

TEST_CASE("Downloader: fetched into .part, checked by size and sum, renamed") {
    TempDir tmp("downloader");
    FakeCommand site;
    site.bodies["http://x/game.chd"] = "the whole game";
    Downloader d("get %u %o", "", site.runner());
    DownloadRequest r;
    r.url = "http://x/game.chd";
    r.target = tmp.at("game.chd");
    r.size = 14;
    r.sha256 = ableem::Sha256::ofString("the whole game");
    string error;
    CHECK(d.fetch(r, error) == Downloader::Result::Downloaded);
    CHECK(tmp.readFile("game.chd") == "the whole game");
    CHECK_FALSE(DirEntry::exists(tmp.at("game.chd.part")));
    CHECK(site.commands[0] == "get http://x/game.chd " + tmp.at("game.chd.part"));

    // there and right: not fetched again
    CHECK(d.fetch(r, error) == Downloader::Result::AlreadyThere);
    CHECK(site.commands.size() == 1);
}

TEST_CASE("Downloader: a wrong size or sum is refused and leaves nothing") {
    TempDir tmp("downloader");
    FakeCommand site;
    site.bodies["http://x/a"] = "abc";
    Downloader d("get %u %o", "", site.runner());
    DownloadRequest r;
    r.url = "http://x/a";
    r.target = tmp.at("a");
    string error;

    r.size = 4;
    CHECK(d.fetch(r, error) == Downloader::Result::WrongSize);
    CHECK(error.find("3 bytes") != string::npos);
    r.size = 3;
    r.sha256 = "0000";
    CHECK(d.fetch(r, error) == Downloader::Result::WrongChecksum);
    CHECK_FALSE(DirEntry::exists(tmp.at("a")));
    CHECK_FALSE(DirEntry::exists(tmp.at("a.part")));

    // neither known: taken as it came, and fetched again next time (nothing proves the one there right)
    r.size = 0;
    r.sha256.clear();
    CHECK(d.fetch(r, error) == Downloader::Result::Downloaded);
    CHECK(d.fetch(r, error) == Downloader::Result::Downloaded);
}

TEST_CASE("Downloader: a failed download without resume leaves no .part") {
    TempDir tmp("downloader");
    FakeCommand site;
    site.bodies["http://x/big"] = "0123456789";
    site.cut = 4;
    Downloader d("get %u %o", "resume %u %o", site.runner());
    DownloadRequest r;
    r.url = "http://x/big";
    r.target = tmp.at("big");
    string error;
    CHECK(d.fetch(r, error) == Downloader::Result::Failed);
    CHECK_FALSE(DirEntry::exists(tmp.at("big.part")));
    site.bodies.erase("http://x/big");
    CHECK(d.fetch(r, error) == Downloader::Result::Failed); // a URL the site does not have
    CHECK(error.find("22") != string::npos);
}

TEST_CASE("Downloader: resume keeps what a stopped download left and continues it") {
    TempDir tmp("downloader");
    FakeCommand site;
    site.bodies["http://x/big"] = "0123456789";
    site.cut = 4;
    Downloader d("get %u %o", "resume %u %o", site.runner());
    DownloadRequest r;
    r.url = "http://x/big";
    r.target = tmp.at("big");
    r.size = 10;
    r.sha256 = ableem::Sha256::ofString("0123456789");
    r.resume = true;
    string error;

    CHECK(d.fetch(r, error) == Downloader::Result::Failed);
    CHECK(tmp.readFile("big.part") == "012345"); // kept
    CHECK(site.commands.back().compare(0, 4, "get ") == 0); // nothing to resume the first time

    CHECK(d.fetch(r, error) == Downloader::Result::Downloaded);
    CHECK(site.commands.back().compare(0, 7, "resume ") == 0);
    CHECK(tmp.readFile("big") == "0123456789");
    CHECK_FALSE(DirEntry::exists(tmp.at("big.part")));
}

TEST_CASE("Downloader: a resume that gets nowhere drops the .part, so the next attempt starts over") {
    TempDir tmp("downloader");
    tmp.writeFile("big.part", "01234");
    Downloader d("get %u %o", "refuse %u %o", [](const string &) { return 33; }); // curl -C -: no ranges here
    DownloadRequest r;
    r.url = "http://x/big";
    r.target = tmp.at("big");
    r.resume = true;
    string error;
    CHECK(d.fetch(r, error) == Downloader::Result::Failed);
    CHECK_FALSE(DirEntry::exists(tmp.at("big.part")));
}

TEST_CASE("Downloader: without a resume command a resumed request starts over") {
    TempDir tmp("downloader");
    FakeCommand site;
    site.bodies["http://x/big"] = "0123456789";
    tmp.writeFile("big.part", "01234");
    Downloader d("get %u %o", "", site.runner());
    DownloadRequest r;
    r.url = "http://x/big";
    r.target = tmp.at("big");
    r.resume = true;
    string error;
    CHECK(d.fetch(r, error) == Downloader::Result::Downloaded);
    CHECK(site.commands.back().compare(0, 4, "get ") == 0);
    CHECK(tmp.readFile("big") == "0123456789");
}

TEST_CASE("Downloader::commandFor") {
    CHECK(Downloader::commandFor("curl -C - -o \"%o\" \"%u\"", "http://a/b c", "/x/y.part") ==
          "curl -C - -o \"/x/y.part\" \"http://a/b c\"");
}
