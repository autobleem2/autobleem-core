//
// DebugDriver: the LAN-safety gate (allowedToStart), the auth token compare (tokensMatch) and the `grab`
// framing (grabHeader) - the pure parts, with no socket and no Gui, so no display is needed to run them.
//
#include "doctest/doctest.h"

#include "ableem/ui/debug_driver.h"

using namespace std;
using ableem::DebugDriver;

TEST_CASE("allowedToStart: loopback never needs a token") {
    CHECK(DebugDriver::allowedToStart("", ""));
    CHECK(DebugDriver::allowedToStart("127.0.0.1", ""));
    // a token on loopback is fine too - just not required
    CHECK(DebugDriver::allowedToStart("127.0.0.1", "secret"));
}

TEST_CASE("allowedToStart: a LAN bind is refused without a token") {
    CHECK_FALSE(DebugDriver::allowedToStart("0.0.0.0", ""));
    CHECK_FALSE(DebugDriver::allowedToStart("192.168.1.50", ""));
}

TEST_CASE("allowedToStart: a LAN bind with a token is allowed") {
    CHECK(DebugDriver::allowedToStart("0.0.0.0", "secret"));
    CHECK(DebugDriver::allowedToStart("192.168.1.50", "secret"));
}

TEST_CASE("tokensMatch: the right token matches, everything else does not") {
    CHECK(DebugDriver::tokensMatch("secret", "secret"));
    CHECK_FALSE(DebugDriver::tokensMatch("secret", "wrong"));
    CHECK_FALSE(DebugDriver::tokensMatch("secret", ""));
    CHECK_FALSE(DebugDriver::tokensMatch("secret", "secretlonger"));
    CHECK_FALSE(DebugDriver::tokensMatch("secret", "secre"));
    // nothing configured never "matches", even an empty attempt
    CHECK_FALSE(DebugDriver::tokensMatch("", ""));
    CHECK_FALSE(DebugDriver::tokensMatch("", "anything"));
}

TEST_CASE("grabHeader: the byte count in the header is what a client should expect to read next") {
    CHECK(DebugDriver::grabHeader(0) == "ok 0");
    CHECK(DebugDriver::grabHeader(1234) == "ok 1234");
    // the framing a test relies on: parse the header, then read exactly that many bytes
    string header = DebugDriver::grabHeader(57);
    size_t space = header.find(' ');
    REQUIRE(space != string::npos);
    CHECK(header.substr(0, space) == "ok");
    CHECK(stoul(header.substr(space + 1)) == 57u);
}
