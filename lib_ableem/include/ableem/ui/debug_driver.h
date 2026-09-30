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
//   shot <file.bmp|.png>     a frame presented after this command (and after the last input) written to the
//                            file - never an older one: it waits up to 5 s for it ("err no frame" past
//                            that). A relative path is under AB_DEBUG_OUT when that is set (a sandbox's own
//                            folder), else the program's cwd; the reply names the full path
//   grab                     the same frame as `shot`, but sent back instead of written to a file: a reply
//                            line "ok <n>" (n = byte count) immediately followed by exactly n bytes of PNG,
//                            with no trailing newline - so a test on a device (the PSC's stick) never has to
//                            write the screenshot there. "err no frame" as `shot` would.
//   clip start <name>        the frames the renderer presents, 25 a second, into the folder <name> (".mp4"
//                            dropped; under AB_DEBUG_OUT as `shot`): f000000.png, ... - a frame only when
//                            the picture changed - and at `clip stop` clip.ffconcat, which gives each its
//                            time, so `ffmpeg -f concat -i clip.ffconcat -vf fps=25 -pix_fmt yuv420p x.mp4`
//                            plays in real time. Ends by itself after 10 minutes or when the connection that
//                            started it closes. Frames are read back only while a clip or a shot is pending.
//   clip stop                "ok <folder> <n> frames <s> s"
//   frames                   how many frames were presented so far
//   screen                   the class name of the screen showing (GuiScreen::show keeps a stack; the
//                            launcher is "GuiLauncher", a dialog over it "GuiConfirm", ...) - what a client
//                            waits for before pressing anything
//   wait_screen <Name> [s]   until that screen shows (15 s by default; "err ..." with the one showing)
//   wait_idle <ms> [s]       until the picture has rested for ms: the screen says only ambient motion is
//                            left (Input's frame need is not Active), or two frames that far apart are the
//                            same; 10 s by default
//   wait_ready [s]           until no busy spinner shows (`busy` is 0) AND the picture has rested ~300 ms
//                            (wait_idle's test); 10 s by default. A job that starts meanwhile is waited out
//                            too. "err not ready after <s> s (screen <Name>, busy 0|1)" on timeout. Use it
//                            where `screen` already says GuiLauncher but the launcher is still applying
//                            something (Options closing) and would drop the next press
//   busy                     "ok 1" while Gui::beginBusy's spinner shows (or several nested), else "ok 0"
//   items                    the item names the showing screen published (setItems), '|'-separated -
//                            a picker's rows by a language-neutral name, so a client can pick one by name
//                            ("ok Re-Scan Games|Extensions|..."; "ok " when the screen published none).
//                            The System/Quick menu and the launcher's own pickers (set picker, Extensions,
//                            Scanner processors) publish English keys; the classic lists (Options, Game
//                            Manager, game editors, Memory Cards, ...) publish the row text as displayed, so
//                            translated. A heading row is published too, so an index matches what is drawn,
//                            with a leading '#' in its name ("#Leave"); it never holds the cursor. A
//                            screen's items are its own: a screen shown over another has none until it
//                            publishes, and the lower one's come back when it closes
//   selected                 "ok <index>|<name>": where the cursor is in `items` (0-based, headings counted
//                            where the screen publishes them) and the name there ("ok 2|Extensions"); the
//                            name is empty past the list's end, and "ok -1|" when the screen published no
//                            items
//   ping                     ok
//   quit                     the program leaves, as by a power off (Input::requestQuit)
//
// Virtual pads (Input::plugVirtualPad - SDL 2.24 or newer; "err ..." on an older SDL, the console's): padsim's
// words (pad_script.h), `@<n> <command>` for pad n (1..4), pad 1 without it - profile x360|ds4|generic
// [usb|bt], plug, unplug, press/release <btn>, hold <btn> <ms>, tap <btn> [ms], stick left|right <x> <y>,
// trigger l2|r2 <0..255>, dpad <dir>|center, reset, battery <0..100>|off, cable in|out. `press` without an @
// is the logical press above. Pad 1 is plugged in as an x360 by its first command, the others by `plug` or
// `profile`. A battery is a power_supply node under AB_PAD_BATTERY_DIR (the launcher reads its pads' batteries
// there), named as padsim names it.
//   kbd press|release <key>, kbd tap <key> [ms], kbd combo <key>+<key>..., kbd type <text>, kbd plug|unplug|
//   reset - padsim's keyboard words, as `key`/`text` events (padsim's key names: enter esc space minus ...)
//
#pragma once

#include "gui_base.h"

#include <string>
#include <utility>
#include <vector>

namespace ableem {

class ABLEEM_API DebugDriver {
public:
    // The pre-auth budget (see `serve()` in the .cpp): a peer that has not yet sent a matching `auth <token>`
    // gets this long to send its first line, and the line itself may not grow past this many bytes - past
    // either, the connection is dropped as if the peer had closed it. Generous for a real client (connect,
    // send one line, both well under a second) and short enough that a peer which never authenticates - or
    // trickles the line in a byte at a time - cannot tie up the one client this server serves at a time for
    // long. Neither applies once `auth` succeeds: an authenticated session reads with no timeout, since real
    // commands can be minutes apart. Public so a test can check the policy without a socket.
    static const int AuthTimeoutMs = 5000;
    static const size_t MaxAuthLine = 4096;

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

    // Where `shot`/`clip` write, pure: a relative path joined to outDir (AB_DEBUG_OUT) when that is set, anything
    // else as given.
    static std::string outputPath(const std::string &outDir, const std::string &path);
    // A clip's clip.ffconcat, pure: each frame file with how long it showed - until the next one, the last until
    // endMs - and the last named once more (the concat demuxer takes the last duration only from a following
    // entry). frames: (ms since the clip started, file name).
    static std::string clipConcat(const std::vector<std::pair<unsigned, std::string>> &frames, unsigned endMs);

    // the screen stack GuiScreen::show maintains (a typeid name; the compiler's decoration is stripped for
    // the `screen` reply). Cheap, and kept whether or not the driver runs.
    static void pushScreen(const char *typeName);
    static void popScreen();
    static std::string currentScreen();
    // what the `items` reply lists: a picker publishes its rows' names when it shows and clears them when it
    // closes. Kept whether or not the driver runs, like the screen stack.
    static void setItems(const std::vector<std::string> &items);
    static std::vector<std::string> items();
    // the cursor in items() (-1 = none): published with the items and updated whenever it moves
    static void setSelected(int index);
    static int selected();
    // items and cursor in one step, for the screen whose typeid name is typeName - and only while that screen is
    // the one showing (a screen redrawn as a backdrop under another must not publish into it). true when taken.
    static bool publish(const char *typeName, const std::vector<std::string> &items, int selected);
    // the `selected` reply, pure (unit-tested): "ok <index>|<name>", "ok -1|" when there are no items, the name
    // empty when index is outside them
    static std::string selectedReply(const std::vector<std::string> &items, int index);
    // abgui::Busy::begin() (Gui::beginBusy) calls setBusy(true), Busy::end() setBusy(false) when it ends a job:
    // a depth counter (nested jobs), never below 0 - an extra false is ignored. Kept whether or not the driver runs.
    // busy() = depth > 0 (the `busy` command), busyLevel() the depth itself.
    static void setBusy(bool on);
    static bool busy();
    // true once start() listens - a screen builds its `items` only then (they are rebuilt every frame)
    static bool active();
    static int busyLevel();
};

} // namespace ableem
