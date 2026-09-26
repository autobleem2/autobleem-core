//
// DebugDriver: a TCP line server that drives the program the way a pad and a keyboard would and hands
// back what is on the screen - for automated looks at the UI (tools/ab_drive.py is the client). Started
// only when the program asks for it (AutoBleem: AB_DEBUG_PORT in the environment on a dev host); nothing
// here runs otherwise.
//
// Loopback by default: AB_DEBUG_BIND names another address (a LAN IP, or 0.0.0.0) to reach the driver from
// another machine - a Pi 400 or a PSC. Off loopback, AB_DEBUG_TOKEN is mandatory: start() refuses to listen
// at all without one (see allowedToStart), so an unauthenticated driver can never end up reachable from the
// network. A token may also be set on a loopback bind (then it is required there too - "set = required" is
// the whole rule); with no AB_DEBUG_TOKEN and a loopback bind, nothing changes from before.
//
// One command per line, one reply per command ("ok ..." or "err ..."):
//   auth <token>              only needed when a token is configured (see above): must be the connection's
//                            first command - a wrong or missing token gets one "err" reply and the socket is
//                            closed. Harmless to send when no token is configured (always "ok").
//   press <button> [ms]      ButtonDown, a hold of ms (60), ButtonUp - x o s t start select l1 r1 l2 r2,
//                            up down left right (the d-pad)
//   down <button> / up <button>    a held button (down l2, press r2, up l2 = the L2+R2 system menu)
//   key <name>               KeyDown + KeyUp: escape return up down left right pageup pagedown home end tab
//                            backspace delete insert f1..f12, or one character (its Event::code); any of
//                            them after ctrl+ alt+ shift+ gui+ (key ctrl+c, key shift+tab)
//   text <utf8>              typed text (a TextInput event)
//   wait <ms>                sleep
//   shot <file.bmp|.png>     the last presented frame written to the file; waits up to 400 ms for a frame
//                            newer than the last input first, so a screen that redraws on events is caught
//                            after it did
//   grab                     the same frame as `shot`, but sent back instead of written to a file: a reply
//                            line "ok <n>" (n = byte count) immediately followed by exactly n bytes of PNG,
//                            with no trailing newline - so a test on a device (the PSC's stick) never has to
//                            write the screenshot there. "err no frame" as `shot` would.
//   frames                   how many frames were presented so far
//   screen                   the class name of the screen showing (GuiScreen::show keeps a stack; the
//                            launcher is "GuiLauncher", a dialog over it "GuiConfirm", ...) - what a client
//                            waits for before pressing anything
//   items                    the item names the showing screen published (setItems), '|'-separated -
//                            a picker's rows by a language-neutral name, so a client can pick one by name
//                            ("ok Re-Scan Games|Extensions|..."; "ok " when the screen published none)
//   ping                     ok
//   quit                     a Quit event, as the window's close button
//
#pragma once

#include "gui_base.h"

#include <string>
#include <vector>

namespace ableem {

class ABLEEM_API DebugDriver {
public:
    // listens on bindAddress:port from a thread of its own for the rest of the process ("" = 127.0.0.1,
    // unchanged default). false when allowedToStart() refuses (logged) or the port/address cannot be taken.
    static bool start(GuiBase &gui, int port, const std::string &bindAddress = "", const std::string &token = "");

    // The LAN-safety gate, pure (no socket, no I/O - unit-tested directly): false only for a bind beyond
    // loopback ("" and "127.0.0.1" count as loopback) with no token configured. A token is never mandatory
    // on loopback, but if one is given it is used there too - callers that configured a token always require
    // it, regardless of bindAddress.
    static bool allowedToStart(const std::string &bindAddress, const std::string &token);

    // Constant-time compare of a client's `auth <token>` attempt against the configured token - never
    // short-circuits on the first differing byte, and never logs either string. `configured` empty always
    // returns false (callers only compare when a token was actually set).
    static bool tokensMatch(const std::string &configured, const std::string &attempt);

    // The header line `grab` sends ahead of the raw PNG bytes. Pure formatting, split out so the framing
    // (does the header's count match what actually follows) is unit-tested without a socket.
    static std::string grabHeader(size_t byteCount);

    // the screen stack GuiScreen::show maintains (a typeid name; the compiler's decoration is stripped for
    // the `screen` reply). Cheap, and kept whether or not the driver runs.
    static void pushScreen(const char *typeName);
    static void popScreen();
    static std::string currentScreen();
    // what the `items` reply lists: a picker publishes its rows' names when it shows and clears them when it
    // closes. Kept whether or not the driver runs, like the screen stack.
    static void setItems(const std::vector<std::string> &items);
    static std::vector<std::string> items();
};

} // namespace ableem
