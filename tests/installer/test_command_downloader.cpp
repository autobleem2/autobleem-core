// CommandDownloader: the platform's download command as InstallerJob's Downloader - over a fake command
#include <doctest/doctest.h>

#include "installer/command_downloader.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/sha256.h>

#include "support/temp_dir.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using ableem::DirEntry;

namespace {
// "fetch <url> <out>": writes the url into <out> and exits 0, unless the url says "missing"
struct FakeCommand {
    vector<string> lines;
    int operator()(const string &line) {
        lines.push_back(line);
        istringstream in(line);
        string verb, url, out;
        in >> verb >> url >> out;
        if (url.find("missing") != string::npos)
            return 22; // curl -f on a 404
        ofstream(out, ios::binary) << "from " << url;
        return 0;
    }
};
} // namespace

TEST_CASE("CommandDownloader runs the template with %u and %o, and lands the file whole or not at all") {
    TempDir tmp("command_downloader");
    FakeCommand fake;
    CommandDownloader dl("fetch %u %o", [&fake](const string &line) { return fake(line); });
    dl.setRetry(0, 0);
    dl.setNetwork([]() { return true; }, [](unsigned) {});
    string error;
    const string dest = tmp.path() + "/latest.json";

    uint64_t reported = 0;
    REQUIRE(dl.fetch(
        "http://site/releases/latest.json", dest,
        [&reported](uint64_t done, uint64_t) {
            reported = done;
            return true;
        },
        error));
    CHECK(fake.lines.back() == "fetch http://site/releases/latest.json " + dest + ".part");
    CHECK(tmp.readFile("latest.json") == "from http://site/releases/latest.json");
    CHECK_FALSE(DirEntry::exists(dest + ".part"));
    CHECK(reported == string("from http://site/releases/latest.json").size());

    // a failed command leaves neither the file nor its .part - the old file is not replaced
    CHECK_FALSE(dl.fetch("http://site/missing.json", dest, Downloader::Progress(), error));
    CHECK(error.find("exit status 22") != string::npos);
    CHECK(tmp.readFile("latest.json") == "from http://site/releases/latest.json");
    CHECK_FALSE(DirEntry::exists(dest + ".part"));

    // no command: said so
    CommandDownloader none("");
    CHECK_FALSE(none.fetch("http://site/x", dest, Downloader::Progress(), error));
    CHECK(error.find("no download command") != string::npos);
}

namespace {
const string Body = "0123456789abcdefghij"; // 20 bytes: what the fake site serves

// "fetch [--continue] <url> <out>": serves Body into <out>, appending the rest when --continue and a part is there.
// `cutAt` > 0: writes only that many bytes of the whole and fails (the connection dropped); `failures` fails first.
struct ResumingCommand {
    vector<string> lines;
    size_t cutAt = 0;
    int failures = 0;
    int operator()(const string &line) {
        lines.push_back(line);
        istringstream in(line);
        string word, url, out;
        bool resume = false;
        while (in >> word) {
            if (word == "--continue")
                resume = true;
            else if (word.rfind("http", 0) == 0)
                url = word;
            else if (word != "fetch")
                out = word;
        }
        if (failures > 0) {
            failures--;
            return 7;
        }
        string have;
        if (resume)
            DirEntry::readFile(out, have);
        string text = have + Body.substr(have.size());
        if (cutAt > 0 && cutAt < text.size()) {
            text.resize(cutAt);
            ofstream(out, ios::binary) << text;
            cutAt = 0;
            return 18;
        }
        ofstream(out, ios::binary) << text;
        return 0;
    }
};

CommandDownloader makeDownloader(ResumingCommand &fake) {
    CommandDownloader dl("fetch %c %u %o", [&fake](const string &line) { return fake(line); });
    dl.setRetry(0, 0, 5);
    dl.setNetwork([]() { return true; }, [](unsigned) {});
    return dl;
}
} // namespace

TEST_CASE("CommandDownloader keeps the .part of a dropped download and continues it with %c") {
    TempDir tmp("command_downloader");
    ResumingCommand fake;
    CommandDownloader dl = makeDownloader(fake);
    const string dest = tmp.path() + "/pack.tar", part = dest + ".part";
    string error;

    fake.cutAt = 8;
    CHECK_FALSE(dl.fetch("http://site/pack.tar", dest, Downloader::Progress(), error));
    CHECK(tmp.readFile("pack.tar.part") == Body.substr(0, 8)); // kept, not deleted
    CHECK_FALSE(DirEntry::exists(dest));
    CHECK(fake.lines.back().find("--continue") == string::npos); // nothing to continue the first time

    REQUIRE_MESSAGE(dl.fetch("http://site/pack.tar", dest, Downloader::Progress(), error), error);
    CHECK(fake.lines.back().find("fetch --continue http://site/pack.tar " + part) == 0);
    CHECK(tmp.readFile("pack.tar") == Body);
    CHECK_FALSE(DirEntry::exists(part));
}

TEST_CASE("CommandDownloader polls the .part for done and total, and a whole .part needs no command") {
    TempDir tmp("command_downloader");
    ResumingCommand fake;
    CommandDownloader dl = makeDownloader(fake);
    const string dest = tmp.path() + "/pack.tar";
    string error;

    uint64_t lastDone = 0, lastTotal = 0;
    dl.expect(Body.size(), "");
    REQUIRE(dl.fetch(
        "http://site/pack.tar", dest,
        [&](uint64_t done, uint64_t total) {
            lastDone = done;
            lastTotal = total;
            return true;
        },
        error));
    CHECK(lastDone == Body.size());
    CHECK(lastTotal == Body.size());

    // a part that already is the whole file: the command is not run, the file is finished from it
    tmp.writeFile("again.tar.part", Body);
    const size_t before = fake.lines.size();
    dl.expect(Body.size(), "");
    REQUIRE(dl.fetch("http://site/again.tar", tmp.path() + "/again.tar", Downloader::Progress(), error));
    CHECK(fake.lines.size() == before);
    CHECK(tmp.readFile("again.tar") == Body);
}

TEST_CASE("CommandDownloader checks the sha256 and deletes a part that does not match") {
    TempDir tmp("command_downloader");
    ResumingCommand fake;
    CommandDownloader dl = makeDownloader(fake);
    const string dest = tmp.path() + "/pack.tar";
    string error;

    tmp.writeFile("body.txt", Body);
    const string sha = ableem::Sha256::ofFile(tmp.at("body.txt"));
    CHECK(dl.expect(0, sha));
    CHECK(dl.fetch("http://site/pack.tar", dest, Downloader::Progress(), error));

    CHECK(dl.expect(0, string(64, '0')));
    CHECK_FALSE(dl.fetch("http://site/other.tar", tmp.path() + "/other.tar", Downloader::Progress(), error));
    CHECK(error.rfind(Downloader::ErrChecksum, 0) == 0);
    CHECK_FALSE(DirEntry::exists(tmp.path() + "/other.tar"));
    CHECK_FALSE(DirEntry::exists(tmp.path() + "/other.tar.part"));
}

TEST_CASE("CommandDownloader retries a failed command and waits for the network to come back") {
    TempDir tmp("command_downloader");
    ResumingCommand fake;
    fake.failures = 2;
    CommandDownloader dl("fetch %c %u %o", [&fake](const string &line) { return fake(line); });
    int checks = 0;
    unsigned slept = 0;
    dl.setRetry(3, 10000, 5);
    dl.setNetwork([&checks]() { return ++checks > 3; }, [&slept](unsigned ms) { slept += ms; });
    string error;
    REQUIRE_MESSAGE(dl.fetch("http://site/pack.tar", tmp.path() + "/pack.tar", Downloader::Progress(), error), error);
    CHECK(fake.lines.size() == 3); // two failures, then the file
    CHECK(slept >= 3000);          // it waited for the route (3 polls) and between the tries

    // no network and none coming: said so, and the command never ran
    ResumingCommand none;
    CommandDownloader dl2("fetch %c %u %o", [&none](const string &line) { return none(line); });
    dl2.setRetry(1, 2000, 5);
    dl2.setNetwork([]() { return false; }, [](unsigned) {});
    CHECK_FALSE(dl2.fetch("http://site/pack.tar", tmp.path() + "/b.tar", Downloader::Progress(), error));
    CHECK(error.rfind(Downloader::ErrNoNetwork, 0) == 0);
    CHECK(none.lines.empty());
}
