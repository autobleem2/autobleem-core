//
// PackageTable: the reader of rc/packages.ini and the player's Packages/packages.ini (docs/packages.md 4.3).
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/services/package_table.h"

#include <string>
#include <vector>

using std::string;
using std::vector;

namespace {

vector<PackageRow> parse(const string &text, vector<string> *problems = nullptr) {
    vector<PackageRow> rows;
    vector<string> sink;
    PackageTable::parse(text, rows, problems != nullptr ? *problems : sink);
    return rows;
}

} // namespace

TEST_CASE("PackageTable: a row with every key") {
    const string text = "# autobleem-packages 1\n"
                        "; another comment\n"
                        "\n"
                        "[quake-shareware]\n"
                        "kind=quake-id1\n"
                        "title=Quake (Shareware)\n"
                        "match=id1/pak0.pak\n"
                        "size=18689235\n"
                        "variant=v1.06\n"
                        "licence=Shareware\n"
                        "main=id1/pak0.pak\n"
                        "magic=PACK\n";
    vector<PackageRow> rows = parse(text);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].id == "quake-shareware");
    CHECK(rows[0].kind == "quake-id1");
    CHECK(rows[0].title == "Quake (Shareware)");
    CHECK(rows[0].match == vector<string>{"id1/pak0.pak"});
    CHECK(rows[0].size == 18689235);
    CHECK(rows[0].variant == "v1.06");
    CHECK(rows[0].licence == "Shareware");
    CHECK(rows[0].main == "id1/pak0.pak");
    CHECK(rows[0].magic == "PACK");
}

TEST_CASE("PackageTable: the main file is the first match unless main= says otherwise") {
    vector<PackageRow> rows = parse("[a]\nkind=k\ntitle=A\nmatch=x/one.dat;two.dat\n");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].match == vector<string>{"x/one.dat", "two.dat"}); // ';' separates, blanks trimmed
    CHECK(rows[0].mainFile() == "x/one.dat");
    CHECK(rows[0].size == -1);
}

TEST_CASE("PackageTable: keys in any case, CRLF line ends and a BOM") {
    const string text = "\xEF\xBB\xBF# autobleem-packages 1\r\n"
                        "[Doom2]\r\n"
                        "KIND=Doom-IWAD\r\n"
                        "Title = Doom II \r\n"
                        "MATCH = DOOM2.WAD \r\n";
    vector<PackageRow> rows = parse(text);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].id == "doom2"); // the id is lower-cased
    CHECK(rows[0].kind == "doom-iwad");
    CHECK(rows[0].title == "Doom II");
    CHECK(rows[0].match == vector<string>{"DOOM2.WAD"}); // the file name keeps its case: matching ignores it
}

TEST_CASE("PackageTable: only a line that starts with # or ; is a comment - a # inside a value is text") {
    vector<PackageRow> rows = parse("[a]\nkind=k\ntitle=Game #1 (the best)\nmatch=a.dat\n  # indented comment\n");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].title == "Game #1 (the best)");
}

TEST_CASE("PackageTable: a row without kind, title or match is skipped and said so; the rest loads") {
    vector<string> problems;
    vector<PackageRow> rows = parse("[nokind]\ntitle=T\nmatch=a\n"
                                    "[notitle]\nkind=k\nmatch=a\n"
                                    "[nomatch]\nkind=k\ntitle=T\n"
                                    "[good]\nkind=k\ntitle=T\nmatch=a\n",
                                    &problems);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].id == "good");
    CHECK(problems.size() == 3);
}

TEST_CASE("PackageTable: a bad id, kind, size or path drops that row only") {
    vector<string> problems;
    vector<PackageRow> rows = parse("[Bad Id]\nkind=k\ntitle=T\nmatch=a\n"
                                    "[bad-kind]\nkind=Not_A_Kind\ntitle=T\nmatch=a\n"
                                    "[bad-size]\nkind=k\ntitle=T\nmatch=a\nsize=big\n"
                                    "[escape]\nkind=k\ntitle=T\nmatch=../x\n"
                                    "[absolute]\nkind=k\ntitle=T\nmatch=/etc/passwd\n"
                                    "[drive]\nkind=k\ntitle=T\nmatch=C:/x\n"
                                    "[ok]\nkind=k\ntitle=T\nmatch=a\n",
                                    &problems);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].id == "ok");
    CHECK(problems.size() == 6);
}

TEST_CASE("PackageTable: a dos-game row's start, set.<name> and mapper") {
    vector<PackageRow> rows = parse("[prince]\nkind=dos-game\ntitle=Prince of Persia\nmatch=PRINCE.EXE\n"
                                    "start=PRINCE.EXE|Play;SETUP.EXE|Setup\n"
                                    "set.cycles=3000\nSET.Memsize=16\nmapper=keys.map\n");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].starts.size() == 2);
    CHECK(rows[0].starts[0].first == "PRINCE.EXE");
    CHECK(rows[0].starts[0].second == "Play");
    CHECK(rows[0].starts[1].second == "Setup");
    REQUIRE(rows[0].settings.size() == 2);
    CHECK(rows[0].settings[0].first == "cycles");
    CHECK(rows[0].settings[0].second == "3000");
    CHECK(rows[0].settings[1].first == "memsize");
    CHECK(rows[0].mapper == "keys.map");
}

TEST_CASE("PackageTable: rows keep their order, unknown keys are ignored, a key before any row is too") {
    vector<PackageRow> rows =
        parse("stray=1\n[b]\nkind=k\ntitle=B\nmatch=b\nfuture=thing\n[a]\nkind=k\ntitle=A\nmatch=a\n");
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].id == "b");
    CHECK(rows[1].id == "a");
}

TEST_CASE("PackageTable: five hundred rows load") {
    string text = "# autobleem-packages 1\n";
    for (int i = 0; i < 500; i++) {
        const string n = std::to_string(i);
        text += "[row-" + n + "]\nkind=doom-iwad\ntitle=Game " + n + "\nmatch=G" + n + ".WAD\n";
    }
    vector<PackageRow> rows = parse(text);
    CHECK(rows.size() == 500);
    CHECK(rows[499].id == "row-499");
}

TEST_CASE("PackageTable: names and relative paths") {
    CHECK(PackageTable::validName("doom-iwad"));
    CHECK(PackageTable::validName("q3-openarena"));
    CHECK_FALSE(PackageTable::validName(""));
    CHECK_FALSE(PackageTable::validName("-a"));
    CHECK_FALSE(PackageTable::validName("a-"));
    CHECK_FALSE(PackageTable::validName("a--b"));
    CHECK_FALSE(PackageTable::validName("A"));
    CHECK_FALSE(PackageTable::validName("a_b"));
    CHECK(PackageTable::validName(string(32, 'a')));
    CHECK_FALSE(PackageTable::validName(string(33, 'a')));

    CHECK(PackageTable::cleanRelativePath("id1/pak0.pak") == "id1/pak0.pak");
    CHECK(PackageTable::cleanRelativePath("id1\\pak0.pak") == "id1/pak0.pak");
    CHECK(PackageTable::cleanRelativePath("./a//b") == "a/b");
    CHECK(PackageTable::cleanRelativePath("../a") == "");
    CHECK(PackageTable::cleanRelativePath("a/../b") == "");
    CHECK(PackageTable::cleanRelativePath("/a") == "");
    CHECK(PackageTable::cleanRelativePath("C:/a") == "");
    CHECK(PackageTable::cleanRelativePath("") == "");
}

TEST_CASE("PackageTable: a file that is not there loads nothing") {
    TempDir tmp("package_table");
    vector<PackageRow> rows;
    vector<string> problems;
    CHECK_FALSE(PackageTable::load(tmp.at("missing.ini"), rows, problems));
    tmp.writeFile("t.ini", "[a]\nkind=k\ntitle=A\nmatch=a\n");
    CHECK(PackageTable::load(tmp.at("t.ini"), rows, problems));
    CHECK(rows.size() == 1);
}
