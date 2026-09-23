// CommandDownloader: the platform's download command as InstallerJob's Downloader - over a fake command
#include <doctest/doctest.h>

#include "installer/command_downloader.h"

#include <ableem/engine/filesystem.h>

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
