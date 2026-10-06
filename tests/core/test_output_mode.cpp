//
// OutputMode: the Display option's token, as config.ini, the emulator and rc/boot.sh spell it.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"

#include "core/services/output_mode.h"

#include <string>
#include <vector>

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

TEST_CASE("OutputMode::shownAt: a mode is in use only when the window has its size") {
    const OutputMode crt = OutputMode::parse("720x480");
    CHECK(crt.shownAt(720, 480));
    CHECK_FALSE(crt.shownAt(1280, 720)); // CRT 4:3 round 1: Weston stayed in 720p
    CHECK_FALSE(crt.shownAt(0, 0));
    CHECK(OutputMode::parse("720").shownAt(1280, 720));
    CHECK_FALSE(OutputMode::parse("720").shownAt(720, 480));
    CHECK(OutputMode::parse("1080").shownAt(1920, 1080));
    CHECK_FALSE(OutputMode::parse("1080").shownAt(1280, 720));
    // auto is whatever the display gives
    CHECK(OutputMode().shownAt(1280, 720));
    CHECK(OutputMode().shownAt(3840, 2160));
}

TEST_CASE("OutputMode::crtMargin: config.ini's CRT margin, 5 % when missing or nonsense, 0..20") {
    CHECK(OutputMode::crtMargin("") == 5);
    CHECK(OutputMode::crtMargin("abc") == 5);
    CHECK(OutputMode::crtMargin("-2") == 5);
    CHECK(OutputMode::crtMargin("0") == 0);
    CHECK(OutputMode::crtMargin("8") == 8);
    CHECK(OutputMode::crtMargin("10") == 10);
    CHECK(OutputMode::crtMargin("90") == 20);
}

TEST_CASE("OutputMode: readToken takes one trimmed line") {
    TempDir tmp("outputmode");
    tmp.writeFile("outputmode", "1080\r\n");
    string token;
    CHECK(OutputMode::readToken(tmp.path() + "/outputmode", token));
    CHECK(token == "1080");
    CHECK_FALSE(OutputMode::readToken(tmp.path() + "/missing", token));
}

TEST_CASE("OutputMode: 720x480 is the CRT 4:3 mode") {
    const OutputMode crt = OutputMode::parse("720x480");
    CHECK(crt.isCrt());
    CHECK(crt.token() == "720x480");
    CHECK(crt.label() == "CRT 4:3");
    CHECK(OutputMode::parse(OutputMode::CrtToken()) == crt);
    CHECK_FALSE(OutputMode::parse("720").isCrt());
    CHECK_FALSE(OutputMode::parse("720x576").isCrt());
    CHECK_FALSE(OutputMode::parse("auto").isCrt());
    CHECK(OutputMode::parse("720x576").label() == "720x576");
}

TEST_CASE("OutputMode::placeCrt: the CRT mode right after 720 and 1080") {
    using V = std::vector<string>;
    // the console's list
    CHECK(OutputMode::placeCrt(V{"720", "1080", "720x480"}) == V{"720", "1080", "720x480"});
    // a display's EDID order: the 16:9 modes first, the rest after - 720x480 is not the first of the rest
    CHECK(OutputMode::placeCrt(V{"auto", "720", "1080", "2560x1440", "720x480", "1280x1024"}) ==
          V{"auto", "720", "1080", "720x480", "2560x1440", "1280x1024"});
    CHECK(OutputMode::placeCrt(V{"auto", "720", "1080", "1280x1024", "720x480"}) ==
          V{"auto", "720", "1080", "720x480", "1280x1024"});
    // only one of the two is there
    CHECK(OutputMode::placeCrt(V{"auto", "1080", "1024x768", "720x480"}) == V{"auto", "1080", "720x480", "1024x768"});
    // listed before the 16:9 modes: it moves behind them
    CHECK(OutputMode::placeCrt(V{"720x480", "720", "1080", "1024x768"}) == V{"720", "1080", "720x480", "1024x768"});
    // no CRT mode: unchanged; no 720/1080: unchanged
    CHECK(OutputMode::placeCrt(V{"auto", "720", "1080", "1024x768"}) == V{"auto", "720", "1080", "1024x768"});
    CHECK(OutputMode::placeCrt(V{"auto", "1024x768", "720x480"}) == V{"auto", "1024x768", "720x480"});
}

TEST_CASE("OutputMode: the theme picker lists only 4:3 themes in the CRT mode") {
    const std::vector<string> themes{"ab2.0.0", "other", "third"};
    auto supports = [](const string &name) { return name == "ab2.0.0"; };
    CHECK(OutputMode::themesFor(OutputMode::parse("720x480"), themes, supports) == std::vector<string>{"ab2.0.0"});
    CHECK(OutputMode::themesFor(OutputMode::parse("720"), themes, supports) == themes);
    CHECK(OutputMode::themesFor(OutputMode::parse("auto"), themes, supports) == themes);
    // none supports it: the whole list, never an empty picker
    CHECK(OutputMode::themesFor(OutputMode::parse("720x480"), themes, [](const string &) { return false; }) == themes);
}

TEST_CASE("OutputMode: the default theme replaces one without a 4:3 layout - in the CRT mode only") {
    const OutputMode crt = OutputMode::parse("720x480");
    CHECK(OutputMode::needsDefaultTheme(crt, false));
    CHECK_FALSE(OutputMode::needsDefaultTheme(crt, true));
    CHECK_FALSE(OutputMode::needsDefaultTheme(OutputMode::parse("1080"), false));
    CHECK_FALSE(OutputMode::needsDefaultTheme(OutputMode(), false));
}

TEST_CASE("OutputMode::themeToSwitchTo: after the confirm and at the start the same rule") {
    const OutputMode crt = OutputMode::parse("720x480");
    CHECK(OutputMode::themeToSwitchTo(crt, "other", false, "ab2.0.0", true) == "ab2.0.0");
    // a theme with a 4:3 layout stays; so does the default itself
    CHECK(OutputMode::themeToSwitchTo(crt, "other", true, "ab2.0.0", true).empty());
    CHECK(OutputMode::themeToSwitchTo(crt, "ab2.0.0", false, "ab2.0.0", true).empty());
    // no default theme installed: nothing to switch to
    CHECK(OutputMode::themeToSwitchTo(crt, "other", false, "ab2.0.0", false).empty());
    // another mode: never
    CHECK(OutputMode::themeToSwitchTo(OutputMode::parse("720"), "other", false, "ab2.0.0", true).empty());
    CHECK(OutputMode::themeToSwitchTo(OutputMode(), "other", false, "ab2.0.0", true).empty());
}
