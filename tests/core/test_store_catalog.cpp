//
// StoreCatalog and StoreSourceTsv: what the AutoBleem Store offers - our catalog.json and the user's TSV
// sources (docs/store-plan.md in the launcher).
//
#include "doctest/doctest.h"

#include <ableem/engine/store_catalog.h>

using namespace std;
using ableem::StoreCatalog;
using ableem::StoreItem;
using ableem::StoreSourceTsv;

TEST_CASE("StoreCatalog: our catalog.json, files by disc, requires, bad items skipped") {
    const char *json = R"({"schema": 1, "platform": "psc", "date": "2026-10-01",
      "items": [
        {"id": "app/opentyrian", "kind": "app", "title": "OpenTyrian", "version": "2.1", "author": "The authors",
         "licence": "GPL-2.0", "description": "A shooter", "image": "https://site/store/psc/opentyrian.png",
         "files": [{"name": "opentyrian-psc-2.1.zip", "size": 4521330, "sha256": "ABCDEF", "url": "https://site/x.zip"}],
         "requires": ["pack/psc-libs"]},
        {"id": "ps1/SLUS-99999", "kind": "ps1", "title": "Two Discs", "serial": "SLUS-99999",
         "files": [{"url": "https://site/d2.chd", "disc": 2, "size": 20}, {"url": "https://site/d1.chd", "disc": 1, "size": 10}]},
        {"id": "no-files", "kind": "ps1", "title": "Nothing to fetch", "files": []},
        {"kind": "app", "title": "No id", "files": [{"url": "https://x/y"}]},
        {"id": "bad-url", "kind": "app", "title": "FTP only", "files": [{"url": "ftp://x/y"}]},
        "not an object"
      ]})";
    StoreCatalog c;
    string error;
    REQUIRE(c.loadJson(json, "AutoBleem", error));
    CHECK(c.schema == 1);
    CHECK(c.platform == "psc");
    CHECK(c.date == "2026-10-01");
    REQUIRE(c.items.size() == 2);
    CHECK(c.skipped == 4);

    const StoreItem &app = c.items[0];
    CHECK(app.id == "app/opentyrian");
    CHECK(app.kind == "app");
    CHECK(app.version == "2.1");
    CHECK(app.licence == "GPL-2.0");
    CHECK(app.source == "AutoBleem");
    CHECK(app.dependsOn == vector<string>{"pack/psc-libs"});
    REQUIRE(app.files.size() == 1);
    CHECK(app.files[0].name == "opentyrian-psc-2.1.zip");
    CHECK(app.files[0].sha256 == "abcdef"); // lower-cased
    CHECK(app.size() == 4521330);

    const StoreItem &game = c.items[1];
    REQUIRE(game.files.size() == 2);
    CHECK(game.files[0].disc == 1);
    CHECK(game.files[0].url == "https://site/d1.chd");
    CHECK(game.size() == 30);
    CHECK(game.serial == "SLUS-99999");
}

TEST_CASE("StoreCatalog: not a catalog") {
    StoreCatalog c;
    string error;
    CHECK_FALSE(c.loadJson("{nope", "x", error));
    CHECK_FALSE(c.loadJson("{\"schema\": 1}", "x", error));
    CHECK_FALSE(c.load("/no/such/catalog.json", "x", error));
}

TEST_CASE("StoreSourceTsv: a header names the columns, lines of one title are one item by disc") {
    const string tsv = "# autobleem-store 1\n"
                       "# name: Acme Homebrew\n"
                       "kind\ttitle\turl\tsize\tsha256\tdisc\tserial\timage\tversion\tdescription\n"
                       "ps1\tSome Game\thttps://acme.example/d2.chd\t398442496\t\t2\n"
                       "ps1\tSome Game\thttps://acme.example/d1.chd\t412334080\tFF00\t1\tSLES-12345\t"
                       "https://acme.example/sg.png\n"
                       "app\tAcme Player\thttps://acme.example/player-psc.zip\t\t\t\t\t\t1.2\tPlays things\n";
    StoreSourceTsv s = StoreSourceTsv::parse(tsv, "acme.tsv");
    CHECK(s.name == "Acme Homebrew");
    CHECK(s.problems.empty());
    REQUIRE(s.items.size() == 2);

    const StoreItem &game = s.items[0];
    CHECK(game.id == "ps1/Some Game");
    CHECK(game.source == "Acme Homebrew");
    CHECK(game.serial == "SLES-12345");
    CHECK(game.image == "https://acme.example/sg.png");
    REQUIRE(game.files.size() == 2);
    CHECK(game.files[0].url == "https://acme.example/d1.chd");
    CHECK(game.files[0].size == 412334080);
    CHECK(game.files[0].sha256 == "ff00");
    CHECK(game.files[1].disc == 2);

    const StoreItem &app = s.items[1];
    CHECK(app.kind == "app");
    CHECK(app.version == "1.2");
    CHECK(app.description == "Plays things");
    CHECK(app.size() == 0); // a size unknown
}

TEST_CASE("StoreSourceTsv: without a header, title url [size]; columns in any order with one") {
    StoreSourceTsv bare = StoreSourceTsv::parse("A Game\thttps://x/a.chd\t100\r\nOther\thttp://x/b.pbp\n", "bare");
    CHECK(bare.name == "bare");
    REQUIRE(bare.items.size() == 2);
    CHECK(bare.items[0].kind == "ps1");
    CHECK(bare.items[0].files[0].size == 100);
    CHECK(bare.items[1].files[0].url == "http://x/b.pbp");

    StoreSourceTsv reordered =
        StoreSourceTsv::parse("URL\tTitle\tExtra\thttps://x/c.chd\n"
                              "https://x/c.chd\tReordered\twhatever\n",
                              "r");
    // the header is the first line with a url field: "URL" counts, and the rest of its fields are names
    REQUIRE(reordered.items.size() == 1);
    CHECK(reordered.items[0].title == "Reordered");
}

TEST_CASE("StoreSourceTsv: a bad line is skipped and reported, the rest loads") {
    const string tsv = "title\turl\tsize\n"
                       "\thttps://x/no-title.chd\n"
                       "No URL\tftp://x/y\n"
                       "Bad Size\thttps://x/z.chd\tlots\n"
                       "Fine\thttps://x/fine.chd\t5\n";
    StoreSourceTsv s = StoreSourceTsv::parse("\xEF\xBB\xBF" + tsv, "s"); // with a BOM too
    REQUIRE(s.items.size() == 2); // Bad Size is kept, without a size
    CHECK(s.items[0].title == "Bad Size");
    CHECK(s.items[0].files[0].size == 0);
    CHECK(s.items[1].title == "Fine");
    REQUIRE(s.problems.size() == 3);
    CHECK(s.problems[0] == "line 2: no title");
    CHECK(s.problems[1] == "line 3: no http(s) url");
    CHECK(s.problems[2].find("line 4: size") == 0);
}

TEST_CASE("StoreSourceTsv: the NoPayStation column layout, one item per regional release") {
    // made-up rows in that layout - no link points anywhere real
    const string tsv = "Title ID\tRegion\tName\tPKG direct link\tContent ID\tLast Modification Date\tOriginal Name"
                       "\tFile Size\tSHA256\n"
                       "NPUJ00001\tJP\tPlaceholder\thttp://example.invalid/a.pkg\tJP0000-NPUJ00001_00-01\t2020-01-01"
                       "\t\t1000\tAB12\n"
                       "NPUF00002\tEU\tPlaceholder\thttp://example.invalid/b.pkg\tEP0000-NPUF00002_00-02\t2020-01-01"
                       "\t\t2000\t\n"
                       "NPUI00003\tUS\tOther\tMISSING\tUP0000-NPUI00003_00-03\t2020-01-01\t\t0\t\n";
    StoreSourceTsv s = StoreSourceTsv::parse(tsv, "nps");
    REQUIRE(s.items.size() == 2); // the MISSING link is skipped
    REQUIRE(s.problems.size() == 1);
    CHECK(s.problems[0] == "line 4: no http(s) url");

    CHECK(s.items[0].kind == "ps1");
    CHECK(s.items[0].title == "Placeholder");
    CHECK(s.items[0].serial == "NPUJ00001");
    CHECK(s.items[0].id == "ps1/Placeholder/NPUJ00001"); // two releases of one title: the serial tells them apart
    REQUIRE(s.items[0].files.size() == 1);
    CHECK(s.items[0].files[0].url == "http://example.invalid/a.pkg");
    CHECK(s.items[0].files[0].size == 1000);
    CHECK(s.items[0].files[0].sha256 == "ab12");
    CHECK(s.items[1].serial == "NPUF00002");
    CHECK(s.items[1].id == "ps1/Placeholder/NPUF00002");
}

TEST_CASE("StoreSourceTsv: our layout's multi-disc lines still make one item, the serial on one of them") {
    const string tsv = "kind\ttitle\turl\tdisc\tserial\tname\n"
                       "ps1\tTwo Discs\thttps://x/d1.chd\t1\tSLUS-00001\tdisc-one.chd\n"
                       "ps1\tTwo Discs\thttps://x/d2.chd\t2\t\tdisc-two.chd\n";
    StoreSourceTsv s = StoreSourceTsv::parse(tsv, "ours");
    REQUIRE(s.items.size() == 1);
    CHECK(s.items[0].id == "ps1/Two Discs"); // one item of that title: no serial in the id
    CHECK(s.items[0].files.size() == 2);
    CHECK(s.items[0].files[0].name == "disc-one.chd"); // "name" is still the file's name when there is a "title"
}

TEST_CASE("StoreSourceTsv: empty, comments only, a missing file") {
    CHECK(StoreSourceTsv::parse("", "e").items.empty());
    CHECK(StoreSourceTsv::parse("# only\n# comments\n\n", "c").items.empty());
    StoreSourceTsv out;
    string error;
    CHECK_FALSE(StoreSourceTsv::load("/no/such.tsv", "x", out, error));
}
