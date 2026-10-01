// PackCatalog / PscRetroArchCatalog: the site's psc/<kind>/latest.json files as the installer reads them
#include <doctest/doctest.h>

#include <ableem/engine/update_catalog.h>

using namespace std;
using ableem::ChannelCatalog;
using ableem::PackCatalog;
using ableem::PscRetroArchCatalog;
using ableem::ReleaseCatalog;

TEST_CASE("ReleaseCatalog reads the images of a nightly and of pc/images") {
    ReleaseCatalog nightly;
    REQUIRE(nightly.parse(R"({"version": "v2.0.0-alpha2-6-gba7365c", "channel": "dev",
        "files": {"pcusb": {"name": "p.tar.gz", "url": "https://site/p.tar.gz", "sha256": "aa", "size": 1}},
        "images": {"pc-i386": {"name": "i.img.xz", "url": "https://site/i.img.xz", "sha256": "bb", "size": 2},
                   "armhf": {"name": "a.img.xz", "url": "https://site/a.img.xz", "sha256": "cc", "size": 3}}})"));
    REQUIRE(nightly.imageFor("pc-i386"));
    CHECK(nightly.imageFor("pc-i386")->size == 2);
    CHECK(nightly.images.size() == 2);
    CHECK(nightly.fileFor("pcusb"));
    CHECK_FALSE(nightly.imageFor("pcusb"));

    ReleaseCatalog pc;
    REQUIRE(pc.parse(R"({"version": "v2.0.0-alpha2", "prerelease": true,
        "i386": {"name": "autobleem-v2.0.0-alpha2-pcusb-i386.img.xz", "size": 663360396, "sha256": "d8",
                 "url": "https://site/pc/images/v2.0.0-alpha2/autobleem-v2.0.0-alpha2-pcusb-i386.img.xz"}})"));
    CHECK(pc.prerelease);
    REQUIRE(pc.imageFor("i386"));
    CHECK(pc.imageFor("i386")->size == 663360396);
    CHECK(pc.images.size() == 1);
    CHECK(pc.files.empty());

    // a release.json of releases/ has no images
    ReleaseCatalog release;
    REQUIRE(release.parse(R"({"version": "v2.0.0", "files": {}})"));
    CHECK(release.images.empty());
}

TEST_CASE("PackCatalog reads a dated pack's latest.json") {
    PackCatalog cores;
    REQUIRE(cores.parse(R"({"name": "cores-psc-20260920.tar.gz", "size": 123, "sha256": "ab", "date": "20260920",
                            "url": "https://site/psc/cores/cores-psc-20260920.tar.gz",
                            "manifest": "https://site/psc/cores/cores-psc-20260920.json", "count": 171})"));
    CHECK(cores.file.name == "cores-psc-20260920.tar.gz");
    CHECK(cores.file.size == 123);
    CHECK(cores.file.url == "https://site/psc/cores/cores-psc-20260920.tar.gz");
    CHECK(cores.manifestUrl == "https://site/psc/cores/cores-psc-20260920.json");
    CHECK(cores.date == "20260920");
    CHECK(cores.count == 171);

    // the BIOS list: no date, no manifest, the total of what it names
    PackCatalog bios;
    REQUIRE(bios.parse(
        R"({"name": "biospack.txt", "size": 158027, "sha256": "8b", "url": "https://site/psc/bios/biospack.txt",
                           "retrobios_ref": "73be130", "count": 719, "total_bytes": 316784588})"));
    CHECK(bios.count == 719);
    CHECK(bios.totalBytes == 316784588);
    CHECK(bios.date.empty());

    CHECK_FALSE(PackCatalog().parse("not json"));
    CHECK_FALSE(PackCatalog().parse(R"({"name": "x"})")); // no url/sha256
}

TEST_CASE("PscRetroArchCatalog reads psc/retroarch/latest.json") {
    PscRetroArchCatalog ra;
    REQUIRE(ra.parse(R"({"version": "v1.22.2-4", "zip": {"name": "retroarch-psc-v1.22.2-4.zip", "size": 5129326,
                         "sha256": "f0", "url": "https://site/psc/retroarch/v1.22.2-4/retroarch-psc-v1.22.2-4.zip"},
                         "manifest": "https://site/psc/retroarch/v1.22.2-4/manifest.json"})"));
    CHECK(ra.version == "v1.22.2-4");
    CHECK(ra.zip.name == "retroarch-psc-v1.22.2-4.zip");
    CHECK(ra.zip.size == 5129326);
    CHECK(ra.manifestUrl == "https://site/psc/retroarch/v1.22.2-4/manifest.json");
    CHECK_FALSE(PscRetroArchCatalog().parse(R"({"version": "v1"})"));
}

//******************
// ChannelCatalog: channels.json
//******************
namespace {
const char *const ChannelsJson = R"json({"version": 1, "channels": [
    {"id": "release", "label": "Release", "index": "releases/latest.json", "images": "pc/images/release.json", "unstable": false},
    {"id": "testing", "label": "Testing", "index": "releases/unstable.json", "unstable": false},
    {"id": "nightly", "label": "Nightly", "index": "nightly/latest.json", "unstable": true},
    {"id": "preview", "label": "Preview (feature-x)", "index": "preview/latest.json", "unstable": true}]})json";
}

TEST_CASE("ChannelCatalog reads channels.json in its order, with the labels and the unstable flags") {
    ChannelCatalog c;
    REQUIRE(c.parse(ChannelsJson));
    REQUIRE(c.channels.size() == 4);
    CHECK(c.channels[0].id == "release");
    CHECK(c.channels[0].images == "pc/images/release.json");
    CHECK_FALSE(c.channels[0].unstable);
    CHECK(c.channels[3].id == "preview");
    CHECK(c.channels[3].label == "Preview (feature-x)");
    CHECK(c.channels[3].unstable);
    REQUIRE(c.find("nightly"));
    CHECK(c.find("nightly")->index == "nightly/latest.json");
    CHECK_FALSE(c.find("beta"));
}

TEST_CASE("ChannelCatalog skips what is unusable and refuses a file with nothing usable") {
    ChannelCatalog c;
    // a duplicate, an id-less entry, a path out of the site and an absolute URL are dropped; a leading / is trimmed
    REQUIRE(c.parse(R"({"channels": [{"id": "a", "index": "/a/latest.json"}, {"id": "a", "index": "x.json"},
        {"index": "y.json"}, {"id": "b", "index": "../etc/passwd"}, {"id": "c", "index": "http://evil/x.json"},
        {"id": "d", "index": "d/latest.json", "images": "http://evil/i.json"}, "junk", {"id": "e", "index": "e.json"}]})"));
    REQUIRE(c.channels.size() == 2);
    CHECK(c.channels[0].index == "a/latest.json");
    CHECK(c.channels[0].label == "a");
    CHECK(c.channels[1].id == "e");

    ChannelCatalog kept = ChannelCatalog::builtIn();
    CHECK_FALSE(kept.parse("not json"));
    CHECK_FALSE(kept.parse(R"({"channels": []})"));
    CHECK_FALSE(kept.parse(R"({"channels": [{"id": "x"}]})"));
    CHECK_FALSE(kept.parse(R"([1, 2])"));
    CHECK(kept.channels.size() == 3); // a failed parse leaves what it had
}

TEST_CASE("ChannelCatalog: the built-in three are what the programs always knew") {
    const ChannelCatalog b = ChannelCatalog::builtIn();
    REQUIRE(b.channels.size() == 3);
    CHECK(b.channels[2].unstable);
    CHECK(b.lists("release", false) == vector<string>{"releases/latest.json"});
    CHECK(b.lists("testing", false) == vector<string>{"releases/unstable.json", "releases/latest.json"});
    CHECK(b.lists("nightly", false) ==
          vector<string>{"nightly/latest.json", "releases/unstable.json", "releases/latest.json"});
    CHECK(b.lists("nightly", true) ==
          vector<string>{"nightly/latest.json", "pc/images/testing.json", "pc/images/release.json"});
    CHECK(b.lists("testing", true) == vector<string>{"pc/images/testing.json", "pc/images/release.json"});
}

TEST_CASE("ChannelCatalog: a channel falls back through the stable ones before it, never through an unstable one") {
    ChannelCatalog c;
    REQUIRE(c.parse(ChannelsJson));
    CHECK(c.lists("preview", false) ==
          vector<string>{"preview/latest.json", "releases/unstable.json", "releases/latest.json"});
    CHECK(c.lists("preview", true) ==
          vector<string>{"preview/latest.json", "releases/unstable.json", "pc/images/release.json"});
    CHECK(c.lists("nightly", false).front() == "nightly/latest.json");
    CHECK(c.lists("unknown", false).empty());
}

TEST_CASE("ChannelCatalog: the default channel follows how the program was built, else the first listed") {
    ChannelCatalog c;
    REQUIRE(c.parse(ChannelsJson));
    CHECK(c.defaultFor("v2.0.0", false, false) == "release");
    CHECK(c.defaultFor("v2.0.0-alpha2", false, true) == "testing");
    CHECK(c.defaultFor("v2.0.0-alpha2-6-gba7365c", true, true) == "nightly");
    CHECK(c.defaultFor("preview-feature-ab-gui-a880c7", true, false) == "preview");
    // the wanted channel is not listed (no preview on the site, a site without nightly): the first entry
    ChannelCatalog small;
    REQUIRE(small.parse(R"({"channels": [{"id": "release", "index": "releases/latest.json"}]})"));
    CHECK(small.defaultFor("preview-x", true, false) == "release");
    CHECK(small.defaultFor("v2.0.0-1-gabc", true, false) == "release");
    CHECK(ChannelCatalog().defaultFor("v2.0.0", false, false).empty());
}
