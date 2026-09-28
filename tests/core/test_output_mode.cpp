//
// OutputMode: the Display option's token, as config.ini, the emulator and rc/boot.sh spell it.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"

#include "core/services/output_mode.h"

#include <string>

using std::string;

TEST_CASE("OutputMode: the tokens the emulator has always known") {
    CHECK(OutputMode::parse("auto").isAuto());
    CHECK(OutputMode::parse("").isAuto());
    OutputMode m = OutputMode::parse("720");
    CHECK(m.w == 1280);
    CHECK(m.h == 720);
    CHECK(OutputMode::parse("1080").w == 1920);
    CHECK(OutputMode::parse("1280x720").token() == "720");
    CHECK(OutputMode::parse("1920x1080").token() == "1080");
    CHECK(OutputMode().token() == "auto");
}

TEST_CASE("OutputMode: any other mode is <w>x<h>") {
    OutputMode m = OutputMode::parse("3840x2160");
    CHECK(m.w == 3840);
    CHECK(m.h == 2160);
    CHECK(m.token() == "3840x2160");
    CHECK(m.label() == "2160p");
    CHECK(OutputMode::parse("1280x1024").label() == "1280x1024");
    CHECK(OutputMode::parse("1080").label() == "1080p");
}

TEST_CASE("OutputMode: nonsense is auto") {
    CHECK(OutputMode::parse("x720").isAuto());
    CHECK(OutputMode::parse("1280x").isAuto());
    CHECK(OutputMode::parse("0x0").isAuto());
    CHECK(OutputMode::parse("big").isAuto());
    CHECK(OutputMode::parse("-1x5").isAuto());
}

TEST_CASE("OutputMode: equality treats every auto as one") {
    CHECK(OutputMode() == OutputMode::parse("auto"));
    CHECK(OutputMode::parse("720") == OutputMode::parse("1280x720"));
    CHECK(OutputMode::parse("720") != OutputMode::parse("1080"));
}

TEST_CASE("OutputMode: readToken takes one trimmed line") {
    TempDir tmp("outputmode");
    tmp.writeFile("outputmode", "1080\r\n");
    string token;
    CHECK(OutputMode::readToken(tmp.path() + "/outputmode", token));
    CHECK(token == "1080");
    CHECK_FALSE(OutputMode::readToken(tmp.path() + "/missing", token));
}
