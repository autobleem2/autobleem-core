// PackCatalog / PscRetroArchCatalog: the site's psc/<kind>/latest.json files as the installer reads them
#include <doctest/doctest.h>

#include <ableem/engine/update_catalog.h>

using namespace std;
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
