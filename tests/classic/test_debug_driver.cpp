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

TEST_CASE("the pre-auth budget is generous for a real client and bounded for one that never authenticates") {
    // AuthTimeoutMs/MaxAuthLine gate only the window before a matching `auth <token>` line - see the header
    // and serve()'s comment. Sanity-checked here since nothing else about them is reachable without a socket:
    // long enough that "connect, send one short line" never trips it under normal LAN latency, short enough
    // that a peer holding the connection open without authenticating does not do so indefinitely, and a line
    // cap that easily fits "auth <token>" for any realistic token while still being a hard, small bound.
    CHECK(DebugDriver::AuthTimeoutMs >= 1000);
    CHECK(DebugDriver::AuthTimeoutMs <= 30000);
    CHECK(DebugDriver::MaxAuthLine >= 256);   // room for "auth " + a generous token
    CHECK(DebugDriver::MaxAuthLine <= 65536); // still a hard cap, not effectively unbounded
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

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>

// Server::sendAll/handleGrab (debug_driver.cpp) send every reply - the `grab` PNG bytes included - with
// AB_SEND_FLAGS, which is MSG_NOSIGNAL on POSIX: without it, a client that authenticates, asks for `grab`,
// then closes before reading the bytes (Ctrl-C, a WiFi drop, a client-side timeout) raises SIGPIPE on the
// next send() into the closed socket, and the default disposition for that signal kills the whole process -
// this is the bug the LAN-safety review caught (the launcher, not just this connection, would die). A real
// Server needs a GuiBase (SDL), which this suite deliberately runs without (see the file's opening comment),
// so this reproduces the one fact that matters - the flag, not the GUI plumbed around it - with a bare
// socketpair standing in for "the peer is gone".
TEST_CASE("POSIX: sending into a socket whose peer is gone fails the call instead of raising SIGPIPE") {
    int fds[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    ::close(fds[1]); // "the peer closed mid-grab", from the writing end's point of view

    const char data[] = "some bytes that would otherwise have been PNG data";
    errno = 0;
    ssize_t n = send(fds[0], data, sizeof(data), MSG_NOSIGNAL);
    int sendErrno = errno;
    // reaching this CHECK at all is most of the point: with the default SIGPIPE disposition and no
    // MSG_NOSIGNAL, this send() would have terminated the process before returning anything to check
    CHECK(n == -1);
    CHECK(sendErrno == EPIPE);

    ::close(fds[0]);
}
#endif
