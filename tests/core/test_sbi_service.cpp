//
// SbiService: the LibCrypt .sbi files from the site into a save folder. The download command is a fake that
// "fetches" from a map of URLs.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/main.h"
#include "core/services/sbi_service.h"

#include <fstream>
#include <map>

using namespace std;

namespace {

const string Site = "https://site.example";
const string Magic("SBI\0", 4);

struct Fake {
    map<string, string> bodies; // url -> content
    vector<string> urls;        // every request, in order

    Downloader::CommandRunner runner() {
        return [this](const string &line) {
            size_t a = line.find(' '), b = line.find(' ', a + 1);
            const string url = line.substr(a + 1, b - a - 1), out = line.substr(b + 1);
            urls.push_back(url);
            auto it = bodies.find(url);
            if (it == bodies.end()) {
                return 22;
            }
            ofstream(out, ios::binary) << it->second;
            return 0;
        };
    }
};

struct Sbi {
    Sbi() : tmp("sbi"), downloader("get %u %o", "", fake.runner()), service(downloader, Site + "/", tmp.at("scratch")) {
        fake.bodies[Site + "/sbi/index.json"] =
            R"({"SCES-02835": ["SCES-02835.1.sbi", "SCES-02835.2.sbi"], "SLES-00001": ["SLES-00001.sbi"]})";
        fake.bodies[Site + "/sbi/SCES-02835.1.sbi"] = Magic + "one";
        fake.bodies[Site + "/sbi/SCES-02835.2.sbi"] = Magic + "two!";
        fake.bodies[Site + "/sbi/SLES-00001.sbi"] = Magic + "single";
        save = tmp.at("saves/disc-1");
    }
    string read(const string &name) const {
        string text;
        DirEntry::readFile(save + sep + name, text);
        return text;
    }
    TempDir tmp;
    Fake fake;
    Downloader downloader;
    SbiService service;
    string save;
};

} // namespace

TEST_CASE("one version is saved as <ID>.sbi, the serial without its dash") {
    Sbi lib;
    CHECK(lib.service.fetchFor("SLES-00001", lib.save) == SbiService::Result::Fetched);
    CHECK(lib.read("SLES00001.sbi") == Magic + "single");
    CHECK_FALSE(DirEntry::exists(lib.save + sep + "SLES00001.sbi.dl"));
}

TEST_CASE("several versions are <ID>.1.sbi, <ID>.2.sbi in the index order") {
    Sbi lib;
    CHECK(lib.service.fetchFor("SCES-02835", lib.save) == SbiService::Result::Fetched);
    CHECK(lib.read("SCES02835.1.sbi") == Magic + "one");
    CHECK(lib.read("SCES02835.2.sbi") == Magic + "two!");
    CHECK_FALSE(DirEntry::exists(lib.save + sep + "SCES02835.sbi"));
}

TEST_CASE("a serial not in the index writes nothing, not even the folder") {
    Sbi lib;
    CHECK(lib.service.fetchFor("SLUS-01234", lib.save) == SbiService::Result::NotLibCrypt);
    CHECK_FALSE(DirEntry::exists(lib.save));
    CHECK(lib.service.fetchFor("", lib.save) == SbiService::Result::NotLibCrypt);
}

TEST_CASE("the index is fetched once per run, and not before it is needed") {
    Sbi lib;
    CHECK(lib.fake.urls.empty());
    lib.service.fetchFor("SLUS-01234", lib.save);
    lib.service.fetchFor("SLES-00001", lib.save);
    lib.service.fetchFor("SCES-02835", lib.save);
    int indexFetches = 0;
    for (const string &url : lib.fake.urls) {
        indexFetches += url == Site + "/sbi/index.json";
    }
    CHECK(indexFetches == 1);
}

TEST_CASE("a file already there is kept and not fetched again") {
    Sbi lib;
    DirEntry::createDirs(lib.save);
    ofstream(lib.save + sep + "SLES00001.sbi", ios::binary) << Magic + "single";
    CHECK(lib.service.fetchFor("SLES-00001", lib.save) == SbiService::Result::Present);
    CHECK(lib.fake.urls.size() == 1); // the index alone

    SUBCASE("one of two there: only the other is fetched") {
        ofstream(lib.save + sep + "SCES02835.1.sbi", ios::binary) << Magic + "one";
        CHECK(lib.service.fetchFor("SCES-02835", lib.save) == SbiService::Result::Fetched);
        CHECK(lib.fake.urls.back() == Site + "/sbi/SCES-02835.2.sbi");
        CHECK(lib.fake.urls.size() == 2);
    }
    SUBCASE("a present file that is not an .sbi is replaced") {
        ofstream(lib.save + sep + "SCES02835.1.sbi", ios::binary) << "junk";
        CHECK(lib.service.fetchFor("SCES-02835", lib.save) == SbiService::Result::Fetched);
        CHECK(lib.read("SCES02835.1.sbi") == Magic + "one");
    }
}

TEST_CASE("a file that is not an .sbi is dropped") {
    Sbi lib;
    lib.fake.bodies[Site + "/sbi/SLES-00001.sbi"] = "<html>404</html>";
    CHECK(lib.service.fetchFor("SLES-00001", lib.save) == SbiService::Result::Error);
    CHECK_FALSE(DirEntry::exists(lib.save + sep + "SLES00001.sbi"));
    CHECK_FALSE(DirEntry::exists(lib.save + sep + "SLES00001.sbi.dl"));
}

TEST_CASE("a file the site fails to serve is an error and leaves nothing") {
    Sbi lib;
    lib.fake.bodies.erase(Site + "/sbi/SCES-02835.2.sbi");
    CHECK(lib.service.fetchFor("SCES-02835", lib.save) == SbiService::Result::Error);
    CHECK_FALSE(DirEntry::exists(lib.save + sep + "SCES02835.2.sbi"));
}

TEST_CASE("offline: nothing written, no exception, the index is not asked for again") {
    Sbi lib;
    lib.fake.bodies.clear();
    CHECK(lib.service.fetchFor("SCES-02835", lib.save) == SbiService::Result::Offline);
    CHECK_FALSE(DirEntry::exists(lib.save));
    CHECK(lib.service.fetchFor("SCES-02835", lib.save) == SbiService::Result::Offline);
    CHECK(lib.fake.urls.size() == 1);
}

TEST_CASE("an index that is not JSON of that shape is an error") {
    Sbi lib;
    lib.fake.bodies[Site + "/sbi/index.json"] = "<html>";
    CHECK(lib.service.fetchFor("SCES-02835", lib.save) == SbiService::Result::Error);
    CHECK_FALSE(DirEntry::exists(lib.save));
}

TEST_CASE("parseIndex reads the shape and refuses a broken one") {
    map<string, vector<string>> index;
    CHECK(SbiService::parseIndex("{}", index));
    CHECK(index.empty());
    CHECK(SbiService::parseIndex(" { \"A-1\" : [ ] , \"B-2\":[\"x.sbi\"] } ", index));
    CHECK(index["A-1"].empty());
    CHECK(index["B-2"] == vector<string>{"x.sbi"});
    CHECK_FALSE(SbiService::parseIndex("{\"A\": [\"x\"", index));
    CHECK_FALSE(SbiService::parseIndex("[]", index));
    CHECK_FALSE(SbiService::parseIndex("{\"A\": \"x\"}", index));
}
