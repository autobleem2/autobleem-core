//
// Platform's headless policy (AB_HEADLESS=1): pure functions, no SDL init and no display needed to run
// them. R29 - headless test runs on Windows (the owner kept seeing testers' launcher windows and
// firewall prompts).
//
#include "doctest/doctest.h"

#include "ableem/ui/platform.h"

#include <cstdlib>

using namespace std;
using ableem::Platform;

TEST_CASE("startsHidden: a headless run starts its window hidden, a normal run does not") {
    CHECK(Platform::startsHidden(true));
    CHECK_FALSE(Platform::startsHidden(false));
}

TEST_CASE("audioDriverOverride: a headless run forces SDL's dummy audio driver") {
    CHECK(Platform::audioDriverOverride(true) == "dummy");
    // a normal run leaves SDL's own probing alone
    CHECK(Platform::audioDriverOverride(false) == "");
}

TEST_CASE("headlessRequested: reads AB_HEADLESS from the environment") {
#ifdef _WIN32
    _putenv_s("AB_HEADLESS", "");
    CHECK_FALSE(Platform::headlessRequested());
    _putenv_s("AB_HEADLESS", "1");
    CHECK(Platform::headlessRequested());
    // anything but exactly "1" is not headless - "0", "true", empty, ...
    _putenv_s("AB_HEADLESS", "0");
    CHECK_FALSE(Platform::headlessRequested());
    _putenv_s("AB_HEADLESS", "true");
    CHECK_FALSE(Platform::headlessRequested());
    _putenv_s("AB_HEADLESS", "");
#else
    unsetenv("AB_HEADLESS");
    CHECK_FALSE(Platform::headlessRequested());
    setenv("AB_HEADLESS", "1", 1);
    CHECK(Platform::headlessRequested());
    setenv("AB_HEADLESS", "0", 1);
    CHECK_FALSE(Platform::headlessRequested());
    setenv("AB_HEADLESS", "true", 1);
    CHECK_FALSE(Platform::headlessRequested());
    unsetenv("AB_HEADLESS");
#endif
}

TEST_CASE("parseWindowSize: AB_WINDOW_SIZE is <width>x<height>, nothing else") {
    int w = -1, h = -1;
    REQUIRE(Platform::parseWindowSize("1280x720", w, h));
    CHECK(w == 1280);
    CHECK(h == 720);
    REQUIRE(Platform::parseWindowSize("1920X1080", w, h));
    CHECK(w == 1920);
    CHECK(h == 1080);
    w = h = -1;
    CHECK_FALSE(Platform::parseWindowSize("", w, h));
    CHECK_FALSE(Platform::parseWindowSize("1280", w, h));
    CHECK_FALSE(Platform::parseWindowSize("1280x", w, h));
    CHECK_FALSE(Platform::parseWindowSize("x720", w, h));
    CHECK_FALSE(Platform::parseWindowSize("1280x720x2", w, h));
    CHECK_FALSE(Platform::parseWindowSize(" 1280x720", w, h));
    CHECK_FALSE(Platform::parseWindowSize("8x8", w, h));       // too small to be a window
    CHECK_FALSE(Platform::parseWindowSize("99999x720", w, h)); // too big
    CHECK(w == -1);                                            // a refusal leaves them alone
    CHECK(h == -1);
}
