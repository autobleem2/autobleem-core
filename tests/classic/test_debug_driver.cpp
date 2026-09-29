//
// DebugDriver: the LAN-safety gate (allowedToStart), the auth token compare (tokensMatch) and the `grab`
// framing (grabHeader), the busy counter and the items/cursor a screen publishes - the pure parts, with no
// socket and no Gui, so no display is needed to run them.
//
#include "doctest/doctest.h"

#include "ableem/ui/debug_driver.h"

#include <string>
#include <utility>
#include <vector>

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

TEST_CASE("outputPath: a relative shot or clip goes under AB_DEBUG_OUT, anything else stays as given") {
    CHECK(DebugDriver::outputPath("", "a.png") == "a.png");
    CHECK(DebugDriver::outputPath("/mnt/abvm/sb/.abvm/out", "run1/a.png") == "/mnt/abvm/sb/.abvm/out/run1/a.png");
    CHECK(DebugDriver::outputPath("/out/", "a.png") == "/out/a.png");
    CHECK(DebugDriver::outputPath("/out", "/tmp/a.png") == "/tmp/a.png");
    CHECK(DebugDriver::outputPath("C:/out", "D:/a.png") == "D:/a.png");
}

TEST_CASE("clipConcat: each frame shows until the next, the last until the clip ended, and is named twice") {
    vector<pair<unsigned, string>> frames = {{0, "f000000.png"}, {120, "f000001.png"}, {1120, "f000002.png"}};
    const string text = DebugDriver::clipConcat(frames, 2000);
    CHECK(text == "ffconcat version 1.0\n"
                  "file 'f000000.png'\nduration 0.120\n"
                  "file 'f000001.png'\nduration 1.000\n"
                  "file 'f000002.png'\nduration 0.880\n"
                  "file 'f000002.png'\n");
    // no frames at all: only the header
    CHECK(DebugDriver::clipConcat({}, 500) == "ffconcat version 1.0\n");
}

TEST_CASE("busy: a depth counter - nested jobs, and an extra end never takes it below zero") {
    while (DebugDriver::busyLevel() > 0) // whatever an earlier case left
        DebugDriver::setBusy(false);
    CHECK_FALSE(DebugDriver::busy());
    DebugDriver::setBusy(true);
    CHECK(DebugDriver::busy());
    DebugDriver::setBusy(true); // nested
    CHECK(DebugDriver::busyLevel() == 2);
    DebugDriver::setBusy(false);
    CHECK(DebugDriver::busy()); // the outer job still runs
    DebugDriver::setBusy(false);
    CHECK_FALSE(DebugDriver::busy());
    DebugDriver::setBusy(false); // underflow: ignored
    DebugDriver::setBusy(false);
    CHECK(DebugDriver::busyLevel() == 0);
    DebugDriver::setBusy(true); // and the counter still works after it
    CHECK(DebugDriver::busy());
    DebugDriver::setBusy(false);
    CHECK_FALSE(DebugDriver::busy());
}

TEST_CASE("selectedReply: index and name, an empty name past the list, -1 when nothing is published") {
    const vector<string> items = {"#Leave", "Extensions", "Power off"};
    CHECK(DebugDriver::selectedReply(items, 1) == "ok 1|Extensions");
    CHECK(DebugDriver::selectedReply(items, 2) == "ok 2|Power off");
    CHECK(DebugDriver::selectedReply(items, 0) == "ok 0|#Leave"); // a heading keeps its marker
    CHECK(DebugDriver::selectedReply(items, 3) == "ok 3|");
    CHECK(DebugDriver::selectedReply(items, -1) == "ok -1|");
    CHECK(DebugDriver::selectedReply({}, 0) == "ok -1|");
    CHECK(DebugDriver::selectedReply({}, -1) == "ok -1|");
}

TEST_CASE("publish: items and cursor reach only the screen showing, and a screen's own come back when it is on top") {
    DebugDriver::pushScreen("8GuiLower"); // the typeid names carry a length prefix (gcc)
    CHECK(DebugDriver::publish("8GuiLower", {"a", "b"}, 1));
    CHECK(DebugDriver::items() == vector<string>({"a", "b"}));
    CHECK(DebugDriver::selected() == 1);

    DebugDriver::pushScreen("8GuiUpper");
    CHECK(DebugDriver::items().empty()); // a new screen starts with none
    CHECK(DebugDriver::selected() == -1);
    CHECK_FALSE(DebugDriver::publish("8GuiLower", {"x"}, 0)); // a backdrop redraw must not publish
    CHECK(DebugDriver::items().empty());
    CHECK(DebugDriver::publish("8GuiUpper", {"#Heading", "row"}, 1));
    CHECK(DebugDriver::selectedReply(DebugDriver::items(), DebugDriver::selected()) == "ok 1|row");

    DebugDriver::popScreen(); // the lower screen's rows and cursor are back
    CHECK(DebugDriver::items() == vector<string>({"a", "b"}));
    CHECK(DebugDriver::selected() == 1);
    DebugDriver::popScreen();
    CHECK(DebugDriver::items().empty());
    CHECK(DebugDriver::selected() == -1);
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
