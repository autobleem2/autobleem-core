//
// CoverAspectTable (core/services/cover_aspect.h): the no-art cover's shape per RetroArch system.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/services/cover_aspect.h"

TEST_CASE("CoverAspectTable::parse reads <database name>=w:h lines") {
    const CoverAspectTable table = CoverAspectTable::parse("Nintendo - Nintendo Entertainment System=5:7\n"
                                                           "Nintendo - Super Nintendo Entertainment System = 7 : 5\n"
                                                           "Nintendo - Game Boy=1:1\n");
    CHECK(table.size() == 3);
    CoverAspect nes = table.aspectFor("Nintendo - Nintendo Entertainment System");
    CHECK(nes.w == 5);
    CHECK(nes.h == 7);
    CHECK(nes.ratio() == doctest::Approx(5.0f / 7.0f));
    CoverAspect snes = table.aspectFor("Nintendo - Super Nintendo Entertainment System");
    CHECK(snes.w == 7);
    CHECK(snes.h == 5);
}

TEST_CASE("CoverAspectTable ignores comments, blank lines and CRLF") {
    const CoverAspectTable table = CoverAspectTable::parse("# the boxes\r\n"
                                                           "\r\n"
                                                           "   # indented comment=3:4\r\n"
                                                           "Arcade=3:4\r\n");
    CHECK(table.size() == 1);
    CHECK(table.aspectFor("Arcade").w == 3);
    CHECK(table.aspectFor("Arcade").h == 4);
}

TEST_CASE("CoverAspectTable: an unlisted system, an empty name and an App are 1:1") {
    const CoverAspectTable table = CoverAspectTable::parse("Arcade=3:4\n");
    CHECK(table.aspectFor("Sega - Saturn").w == 1);
    CHECK(table.aspectFor("Sega - Saturn").h == 1);
    CHECK(table.aspectFor("").w == 1);
    CHECK(table.aspectFor("").h == 1);
    CHECK(CoverAspectTable().aspectFor("Arcade").ratio() == doctest::Approx(1.0f));
}

TEST_CASE("CoverAspectTable skips bad lines and keeps the good ones") {
    const CoverAspectTable table = CoverAspectTable::parse("no equals sign\n"
                                                           "=3:4\n"
                                                           "NoColon=34\n"
                                                           "Zero=0:4\n"
                                                           "Negative=-3:4\n"
                                                           "Words=a:b\n"
                                                           "Half=1.5:2\n"
                                                           "Huge=100:4\n"
                                                           "Missing=3:\n"
                                                           "Extra=3:4:5\n"
                                                           "Good=4:3\n");
    CHECK(table.size() == 1);
    CHECK(table.aspectFor("Good").w == 4);
    CHECK(table.aspectFor("Good").h == 3);
    CHECK(table.aspectFor("Zero").w == 1);
    CHECK(table.aspectFor("Extra").h == 1);
}

TEST_CASE("CoverAspectTable: a later line wins, and a playlist's .lpl suffix is ignored") {
    const CoverAspectTable table = CoverAspectTable::parse("Nintendo - Nintendo 64=4:3\n"
                                                           "Nintendo - Nintendo 64=7:5\n");
    CHECK(table.aspectFor("Nintendo - Nintendo 64").w == 7);
    CHECK(table.aspectFor("Nintendo - Nintendo 64.lpl").w == 7);
    CHECK(table.aspectFor("Nintendo - Nintendo 64.LPL").h == 5);
    CHECK(table.aspectFor("Nintendo - Nintendo 64 ").w == 7);
}

TEST_CASE("CoverAspectTable::load reads the file, a missing one is an empty table") {
    TempDir tmp("cover_aspect");
    tmp.makeSubDir("platform");
    tmp.writeFile("platform/cover_aspects.cfg", "# test\nArcade=3:4\n");

    const CoverAspectTable table = CoverAspectTable::load(CoverAspectTable::pathFor(tmp.path()));
    CHECK(table.size() == 1);
    CHECK(table.aspectFor("Arcade").h == 4);

    CHECK(CoverAspectTable::load(tmp.path() + "/platform/nothing.cfg").size() == 0);
}
