//
// DebugDriver: the TCP line server that drives the program for automated UI tests. See the header.
//
#include "ableem/ui/debug_driver.h"
#include "ableem/ui/input.h"
#include "ableem/ui/pad_script.h"
#include "ableem/ui/platform.h"
#include "ableem/ui/renderer.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#define AB_SEND_FLAGS 0
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#define AB_SEND_FLAGS MSG_NOSIGNAL
#endif

#include "sdl_common.h" // the clip's PNGs (SDL_image) - after winsock2.h, which must come before windows.h

using namespace std;

namespace ableem {

// Out-of-line definitions for the in-class initializers (header): pre-C++17, an odr-use - CHECK(...) in the
// tests binds them by const reference - needs one of these or the link fails.
const int DebugDriver::AuthTimeoutMs;
const size_t DebugDriver::MaxAuthLine;

namespace {
std::mutex screenMutex;
std::vector<std::string> screenStack;
std::vector<std::string> screenItems; // under screenMutex too

typedef chrono::steady_clock Clock;

// "11GuiLauncher" (gcc) / "class GuiLauncher" (msvc) -> "GuiLauncher"
std::string plainName(const char *typeName) {
    std::string s = typeName;
    if (s.compare(0, 6, "class ") == 0)
        s.erase(0, 6);
    size_t i = 0;
    while (i < s.size() && isdigit(static_cast<unsigned char>(s[i])))
        i++;
    return s.substr(i);
}

// the button names the client uses
bool buttonFor(const string &name, Button &button, bool &dpad) {
    dpad = false;
    if (name == "x" || name == "cross")
        button = Button::Cross;
    else if (name == "o" || name == "circle")
        button = Button::Circle;
    else if (name == "s" || name == "square")
        button = Button::Square;
    else if (name == "t" || name == "triangle")
        button = Button::Triangle;
    else if (name == "start")
        button = Button::Start;
    else if (name == "select")
        button = Button::Select;
    else if (name == "l1")
        button = Button::L1;
    else if (name == "r1")
        button = Button::R1;
    else if (name == "l2")
        button = Button::L2;
    else if (name == "r2")
        button = Button::R2;
    else {
        dpad = true;
        if (name == "up")
            button = Button::DpadUp;
        else if (name == "down")
            button = Button::DpadDown;
        else if (name == "left")
            button = Button::DpadLeft;
        else if (name == "right")
            button = Button::DpadRight;
        else
            return false;
    }
    return true;
}

bool keyFor(const string &name, Key &key) {
    static const struct {
        const char *name;
        Key key;
    } keys[] = {{"escape", Key::Escape},       {"return", Key::Return}, {"enter", Key::Return}, {"up", Key::Up},
                {"down", Key::Down},           {"left", Key::Left},     {"right", Key::Right},  {"pageup", Key::PageUp},
                {"pagedown", Key::PageDown},   {"home", Key::Home},     {"end", Key::End},      {"tab", Key::Tab},
                {"backspace", Key::Backspace}, {"delete", Key::Delete}, {"insert", Key::Insert}};
    for (const auto &k : keys) {
        if (name == k.name) {
            key = k.key;
            return true;
        }
    }
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f') {
        int n = atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12) {
            key = static_cast<Key>(static_cast<int>(Key::F1) + n - 1);
            return true;
        }
    }
    return false;
}

// "ctrl+alt+c" -> the modifiers and what is left ("c"); "ctrl+" alone is not a key
void splitModifiers(string &name, unsigned &mods) {
    mods = 0;
    static const struct {
        const char *prefix;
        unsigned mod;
    } prefixes[] = {{"ctrl+", KeyMod::Ctrl}, {"alt+", KeyMod::Alt}, {"shift+", KeyMod::Shift}, {"gui+", KeyMod::Gui}};
    for (bool found = true; found;) {
        found = false;
        for (const auto &p : prefixes) {
            size_t n = strlen(p.prefix);
            if (name.size() > n && name.compare(0, n, p.prefix) == 0) {
                mods |= p.mod;
                name.erase(0, n);
                found = true;
            }
        }
    }
}

// padsim's modifier key names -> the KeyMod bit (0: not a modifier)
unsigned modifierFor(const string &name) {
    if (name == "shift" || name == "rshift")
        return KeyMod::Shift;
    if (name == "ctrl" || name == "rctrl")
        return KeyMod::Ctrl;
    if (name == "alt" || name == "altgr")
        return KeyMod::Alt;
    if (name == "meta")
        return KeyMod::Gui;
    return 0;
}

// a key by the driver's names or padsim's (enter, esc, space, minus, ...) as a KeyDown event; false when unknown
bool keyEventFor(const string &name, Event &e) {
    static const struct {
        const char *name;
        char c;
    } chars[] = {{"space", ' '},      {"minus", '-'},      {"equal", '='},     {"leftbrace", '['},
                 {"rightbrace", ']'}, {"backslash", '\\'}, {"semicolon", ';'}, {"apostrophe", '\''},
                 {"grave", '`'},      {"comma", ','},      {"dot", '.'},       {"slash", '/'}};
    e = Event();
    e.type = Event::Type::KeyDown;
    string n = name == "esc" ? "escape" : name;
    if (keyFor(n, e.key))
        return true;
    for (const auto &c : chars) {
        if (n == c.name) {
            e.code = c.c;
            return true;
        }
    }
    if (n.size() == 1 && n[0] > 32 && n[0] < 127) {
        e.code = tolower(static_cast<unsigned char>(n[0]));
        return true;
    }
    return modifierFor(n) != 0; // a modifier alone: a key with nothing on it
}

void sleepMs(int ms) {
    this_thread::sleep_for(chrono::milliseconds(ms));
}

unsigned msSince(Clock::time_point t0) {
    return static_cast<unsigned>(chrono::duration_cast<chrono::milliseconds>(Clock::now() - t0).count());
}

// FNV-1a over the frame, 8 bytes at a time: "did the picture change"
uint64_t frameHash(const vector<unsigned char> &pixels) {
    uint64_t h = 1469598103934665603ULL;
    const size_t words = pixels.size() / 8;
    const unsigned char *p = pixels.data();
    for (size_t i = 0; i < words; i++) {
        uint64_t w;
        memcpy(&w, p + i * 8, 8);
        h = (h ^ w) * 1099511628211ULL;
    }
    for (size_t i = words * 8; i < pixels.size(); i++)
        h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

bool savePng(vector<unsigned char> &pixels, int w, int h, int pitch, const string &path) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), w, h, 32, pitch, SDL_PIXELFORMAT_ARGB8888);
    if (!s)
        return false;
    const int rc = IMG_SavePNG(s, path.c_str());
    SDL_FreeSurface(s);
    return rc == 0;
}

string parentDir(const string &path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == string::npos ? string() : path.substr(0, slash);
}

//*******************************
// ClipRecorder
//*******************************
// `clip start`: a thread of its own asks the renderer for a copy of a frame every 40 ms, and writes it as the next
// PNG when the picture changed since the last one written - so a resting screen costs a readback, not a file, per
// sample. Each file's time is kept for clip.ffconcat (DebugDriver::clipConcat).
class ClipRecorder {
public:
    enum : unsigned { SampleMs = 40 };          // 25 a second
    enum : unsigned { MaxMs = 10 * 60 * 1000 }; // a clip nobody stopped ends by itself

    ClipRecorder(Renderer &renderer, string dir) : renderer_(renderer), dir_(std::move(dir)) {
        thread_ = thread([this]() { run(); });
    }
    ~ClipRecorder() { finish(); }

    const string &dir() const { return dir_; }

    // the reply to `clip stop`
    string finish() {
        if (!finished_) {
            finished_ = true;
            stop_ = true;
            if (thread_.joinable())
                thread_.join();
            ofstream out(dir_ + "/clip.ffconcat", ios::binary);
            out << DebugDriver::clipConcat(frames_, endMs_);
        }
        char seconds[32];
        snprintf(seconds, sizeof(seconds), "%.1f", endMs_ / 1000.0);
        return "ok " + dir_ + " " + to_string(frames_.size()) + " frames " + seconds + " s" +
               (error_.empty() ? "" : " (" + error_ + ")");
    }

private:
    void run() {
        const Clock::time_point t0 = Clock::now();
        Clock::time_point next = t0;
        unsigned long lastCopied = 0;
        uint64_t lastHash = 0;
        vector<unsigned char> pixels;
        while (!stop_ && msSince(t0) < MaxMs) {
            renderer_.requestFrameCopy();
            next += chrono::milliseconds(static_cast<unsigned>(SampleMs));
            if (next < Clock::now())
                next = Clock::now(); // behind (a slow encode): the timing stays honest, a sample is skipped
            this_thread::sleep_until(next);
            int w = 0, h = 0, pitch = 0;
            const unsigned long copied = renderer_.copyLastFrame(pixels, w, h, pitch);
            if (copied == 0 || copied == lastCopied)
                continue;
            lastCopied = copied;
            const uint64_t hash = frameHash(pixels);
            if (!frames_.empty() && hash == lastHash)
                continue;
            lastHash = hash;
            char name[32];
            snprintf(name, sizeof(name), "f%06u.png", static_cast<unsigned>(frames_.size()));
            const unsigned at = msSince(t0);
            if (!savePng(pixels, w, h, pitch, dir_ + "/" + name)) {
                error_ = string("cannot write ") + name + ": " + SDL_GetError();
                break;
            }
            frames_.push_back(make_pair(at, string(name)));
        }
        endMs_ = msSince(t0);
    }

    Renderer &renderer_;
    string dir_;
    thread thread_;
    atomic<bool> stop_{false};
    bool finished_ = false;
    vector<pair<unsigned, string>> frames_;
    unsigned endMs_ = 0;
    string error_;
};

class Server {
public:
    Server(GuiBase &gui, int port, string bindAddress, string token)
        : gui_(gui), port_(port), bindAddress_(std::move(bindAddress)), token_(std::move(token)) {
        const char *out = getenv("AB_DEBUG_OUT");
        outDir_ = out ? out : "";
    }

    bool listen() {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return false;
#endif
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ == INVALID_SOCKET)
            return false;
        int one = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof(one));
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<unsigned short>(port_));
        if (bindAddress_.empty()) {
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        } else if (inet_pton(AF_INET, bindAddress_.c_str(), &addr.sin_addr) != 1) {
            PLOG_ERROR << "DebugDriver: bad AB_DEBUG_BIND address " << bindAddress_;
            CLOSESOCK(listener_);
            return false;
        }
        if (bind(listener_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
            CLOSESOCK(listener_);
            return false;
        }
        if (::listen(listener_, 1) != 0) {
            CLOSESOCK(listener_);
            return false;
        }
        return true;
    }

    void run() {
        // the renderer reads a frame back only when a shot, a grab, a clip or a wait asks for one
        gui_.renderer().setFrameCache(true);
        for (;;) {
            sock_t client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                continue;
            serve(client);
            CLOSESOCK(client);
            // a clip this connection left running ends with it
            if (clip_) {
                PLOG_INFO << "DebugDriver: " << clip_->finish() << " (the connection closed)";
                clip_.reset();
            }
        }
    }

private:
    // true with the next line in `line`; false when the peer closed, a recv timed out (SO_RCVTIMEO, see
    // setRecvTimeoutMs) or - with maxLen set - the line grew past it before any '\n' showed up. Either way the
    // caller's cue is the same: stop serving this client, nothing left worth reading.
    static bool nextLine(sock_t client, string &buffer, string &line, size_t maxLen = 0) {
        char chunk[512];
        size_t nl = buffer.find('\n');
        while (nl == string::npos) {
            if (maxLen && buffer.size() >= maxLen)
                return false;
            int n = recv(client, chunk, sizeof(chunk), 0);
            if (n <= 0)
                return false;
            buffer.append(chunk, static_cast<size_t>(n));
            nl = buffer.find('\n');
        }
        line = buffer.substr(0, nl);
        buffer.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return true;
    }

    // A missing SO_RCVTIMEO (0) blocks forever - what an authenticated session wants, since commands can be
    // minutes apart. Only the unauthenticated window before `auth` succeeds gets a real timeout, so a peer
    // that connects and never sends anything (or trickles a line in a byte at a time) cannot tie up the one
    // client this server serves at a time.
    static void setRecvTimeoutMs(sock_t client, int ms) {
#ifdef _WIN32
        DWORD timeout = static_cast<DWORD>(ms);
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
#endif
    }

    // false when the peer is gone (closed, reset, or SIGPIPE-worthy on POSIX - AB_SEND_FLAGS keeps that from
    // raising instead of just failing the call): the caller's cue to stop serving this client, never to crash.
    static bool sendAll(sock_t client, const char *data, size_t length) {
        while (length > 0) {
            int n = send(client, data, static_cast<int>(length), AB_SEND_FLAGS);
            if (n <= 0)
                return false;
            data += n;
            length -= static_cast<size_t>(n);
        }
        return true;
    }

    static bool sendLine(sock_t client, const string &reply) {
        string out = reply + "\n";
        return sendAll(client, out.c_str(), out.size());
    }

    void serve(sock_t client) {
        string buffer;
        // A configured token gates the whole connection: the first line must be `auth <token>` (a wrong or
        // missing one is one "err" reply and the socket closes) before any real command is read. Nothing
        // reachable beyond loopback runs without this, by construction of allowedToStart(). This is the one
        // window where the peer has not proven itself, so it also gets AuthTimeoutMs to answer in and
        // MaxAuthLine to say it in (see the header) - past either, the connection is dropped like any other
        // "peer is gone". Once authenticated the timeout comes off (0 = block forever): real commands can be
        // minutes apart.
        if (!token_.empty()) {
            setRecvTimeoutMs(client, DebugDriver::AuthTimeoutMs);
            string line;
            bool ok = nextLine(client, buffer, line, DebugDriver::MaxAuthLine);
            setRecvTimeoutMs(client, 0);
            if (!ok)
                return;
            istringstream in(line);
            string cmd, attempt;
            in >> cmd;
            getline(in, attempt);
            if (!attempt.empty() && attempt[0] == ' ')
                attempt.erase(0, 1);
            if (cmd != "auth" || !DebugDriver::tokensMatch(token_, attempt)) {
                sendLine(client, "err auth");
                return;
            }
            if (!sendLine(client, "ok"))
                return;
        }
        for (;;) {
            string line;
            if (!nextLine(client, buffer, line))
                return;
            istringstream peek(line);
            string cmd;
            peek >> cmd;
            if (cmd == "grab") {
                if (!handleGrab(client))
                    return;
                continue;
            }
            if (!sendLine(client, handle(line)))
                return;
        }
    }

    // shot and grab: a copy of a frame whose drawing began after this request and after the last input - the
    // renderer copies a frame only when asked (it costs a readback), so ask, and wait for it. Never an older
    // copy: false when no such frame came within 5 s (a screen that presents nothing - a hung job). A resting
    // screen still presents a few frames a second (the frame pacer), so this is quick.
    bool awaitFreshFrame() {
        Renderer &r = gui_.renderer();
        const unsigned long after = std::max(lastInputFrame_, r.requestFrameCopy()) + 1;
        const Clock::time_point t0 = Clock::now();
        while (r.copiedFrame() <= after) {
            if (msSince(t0) > 5000)
                return false;
            r.requestFrameCopy(); // again, in case an earlier frame took the request
            sleepMs(10);
        }
        return true;
    }

    bool handleGrab(sock_t client) {
        vector<unsigned char> png;
        if (!awaitFreshFrame() || !gui_.renderer().encodeLastFramePng(png))
            return sendLine(client, "err no frame");
        if (!sendLine(client, DebugDriver::grabHeader(png.size())))
            return false;
        return sendAll(client, reinterpret_cast<const char *>(png.data()), png.size());
    }

    void noteInput() { lastInputFrame_ = gui_.renderer().frameCount(); }

    void injectButton(Button button, bool dpad, bool down) {
        Event e;
        e.button = button;
        if (dpad)
            e.type = down ? Event::Type::DpadDown : Event::Type::DpadUp;
        else
            e.type = down ? Event::Type::ButtonDown : Event::Type::ButtonUp;
        gui_.input().inject(e);
        noteInput();
    }

    void injectKey(Event e, bool down) {
        e.type = down ? Event::Type::KeyDown : Event::Type::KeyUp;
        gui_.input().inject(e);
        noteInput();
    }

    //*******************************
    // virtual pads
    //*******************************
    struct PadState {
        string profile = "x360";
        bool bluetooth = false;
        bool plugged = false;
        bool touched = false; // a command has been sent to it (pad 1 is plugged in by its first one)
        int level = -1;       // battery percent, -1: no battery node
        bool cable = false;
    };
    PadState pads_[Input::VirtualPadSlots];

    PadScript::Profile profileOf(const PadState &p) {
        PadScript::Profile profile;
        PadScript::findProfile(p.profile, profile);
        return profile;
    }

    bool plugPad(int slot) {
        PadState &p = pads_[slot];
        if (!gui_.input().plugVirtualPad(slot, PadScript::specFor(profileOf(p))))
            return false;
        p.plugged = true;
        // the program registers the new pad when it next polls: a press sent before that would be lost
        sleepMs(100);
        gui_.input().virtualPadPlugged(slot); // a round trip through the polling thread
        noteInput();
        return true;
    }

    void unplugPad(int slot) {
        gui_.input().unplugVirtualPad(slot);
        pads_[slot].plugged = false;
        noteInput();
    }

    // the pad's battery node under AB_PAD_BATTERY_DIR, as its level, cable and plug say - gone when it has no
    // battery or is unplugged (padsim's rule)
    string syncBattery(int slot) {
        const PadState &p = pads_[slot];
        const char *root = getenv("AB_PAD_BATTERY_DIR");
        const bool present = p.level >= 0 && p.plugged;
        if (!root || !*root)
            return present ? "err AB_PAD_BATTERY_DIR is not set - nowhere for the battery" : "ok";
        const string dir = string(root) + "/" + PadScript::batteryNode(slot);
        static const char *const files[] = {"capacity", "status", "type", "scope"};
        if (!present) {
            for (const char *f : files)
                remove((dir + "/" + f).c_str());
#ifdef _WIN32
            _rmdir(dir.c_str());
#else
            rmdir(dir.c_str());
#endif
            return "ok";
        }
        DirEntry::createDirs(dir);
        const string values[] = {to_string(p.level), PadScript::batteryStatus(p.level, p.cable), "Battery", "Device"};
        for (size_t i = 0; i < 4; i++) {
            ofstream out(dir + "/" + files[i], ios::binary);
            out << values[i] << "\n";
            if (!out)
                return "err cannot write " + dir;
        }
        return "ok";
    }

    // a game controller's controls go through its mapping (Input::setVirtualPadControl*), the generic pad's raw
    bool setButton(int slot, bool gc, int button, bool down) {
        Input &in = gui_.input();
        return gc ? in.setVirtualPadControl(slot, button, down) : in.setVirtualPadButton(slot, button, down);
    }
    bool setAxis(int slot, bool gc, int axis, int value) {
        Input &in = gui_.input();
        return gc ? in.setVirtualPadControlAxis(slot, axis, value) : in.setVirtualPadAxis(slot, axis, value);
    }

    bool setTarget(int slot, bool gc, const PadScript::Target &t, bool down) {
        bool ok = true;
        if (t.button >= 0)
            ok = setButton(slot, gc, t.button, down) && ok;
        if (t.axis >= 0)
            ok = setAxis(slot, gc, t.axis, down ? 32767 : -32768) && ok;
        noteInput();
        return ok;
    }

    string padCommand(const PadScript::Step &s) {
        if (!Input::virtualPadsSupported())
            return "err virtual pads need SDL 2.24 or newer";
        Input &in = gui_.input();
        PadState &p = pads_[s.pad];
        auto arg = [&s](size_t i) { return i < s.args.size() ? s.args[i] : string(); };
        const string &v = s.verb;

        if (v == "profile") {
            PadScript::Profile profile;
            const string bus = arg(1);
            if (!PadScript::findProfile(arg(0), profile) || (!bus.empty() && bus != "usb" && bus != "bt"))
                return "err profile x360|ds4|generic [usb|bt]";
            if (bus == "bt" && !profile.bluetooth)
                return "err no such pad over Bluetooth";
            p.profile = profile.name;
            p.bluetooth = bus == "bt";
            if (!profile.hasBattery)
                p.level = -1;
            p.touched = true;
            unplugPad(s.pad);
            if (!plugPad(s.pad))
                return "err cannot plug in the pad";
            return syncBattery(s.pad);
        }
        if (v == "plug") {
            p.touched = true;
            if (!p.plugged && !plugPad(s.pad))
                return "err cannot plug in the pad";
            return syncBattery(s.pad);
        }
        if (v == "unplug") {
            p.touched = true;
            unplugPad(s.pad);
            return syncBattery(s.pad);
        }
        if (v == "battery") {
            if (arg(0) == "off") {
                p.level = -1;
            } else if (!profileOf(p).hasBattery) {
                return "err this pad is wired, it has no battery";
            } else if (arg(0).empty() || !isdigit(static_cast<unsigned char>(arg(0)[0]))) {
                return "err battery <0..100>|off";
            } else {
                p.level = std::max(0, std::min(100, atoi(arg(0).c_str())));
            }
            return syncBattery(s.pad);
        }
        if (v == "cable") {
            if (arg(0) != "in" && arg(0) != "out")
                return "err cable in|out";
            p.cable = arg(0) == "in";
            if (!p.bluetooth) {
                // a USB pad lives on its cable
                if (p.cable && !p.plugged && !plugPad(s.pad))
                    return "err cannot plug in the pad";
                if (!p.cable)
                    unplugPad(s.pad);
            }
            p.touched = true;
            return syncBattery(s.pad);
        }

        // the rest moves the pad: pad 1 comes plugged in as an x360 (as padsim's does)
        if (s.pad == 0 && !p.touched && !p.plugged && !plugPad(s.pad))
            return "err cannot plug in the pad";
        p.touched = true;
        if (!p.plugged)
            return "err the pad is unplugged";
        const PadScript::Profile profile = profileOf(p);
        const bool gc = profile.gameController;
        const VirtualPadSpec spec = PadScript::specFor(profile);

        if (v == "reset") {
            if (gc) {
                for (int b = 0; b < 15; b++)
                    setButton(s.pad, true, b, false);
                for (int a = 0; a < 6; a++)
                    setAxis(s.pad, true, a, a < 4 ? 0 : -32768); // the sticks centred, the triggers released
            } else {
                for (int b = 0; b < spec.buttons; b++)
                    in.setVirtualPadButton(s.pad, b, false);
                for (int a = 0; a < spec.axes; a++)
                    in.setVirtualPadAxis(s.pad, a, 0);
                for (int a : spec.triggerAxes) {
                    if (a >= 0)
                        in.setVirtualPadAxis(s.pad, a, -32768);
                }
                if (spec.hats > 0)
                    in.setVirtualPadHat(s.pad, 0);
            }
            noteInput();
            return "ok";
        }
        if (v == "press" || v == "release" || v == "hold" || v == "tap") {
            PadScript::Target t;
            if (!PadScript::buttonTarget(gc, arg(0), t))
                return "err unknown button " + arg(0);
            if (v == "press" || v == "release")
                return setTarget(s.pad, gc, t, v == "press") ? "ok" : "err the pad is gone";
            if (v == "hold" && arg(1).empty())
                return "err hold <btn> <ms>";
            const int ms = arg(1).empty() ? 120 : std::max(0, std::min(60000, atoi(arg(1).c_str())));
            if (!setTarget(s.pad, gc, t, true))
                return "err the pad is gone";
            sleepMs(ms);
            setTarget(s.pad, gc, t, false);
            return "ok";
        }
        if (v == "stick") {
            int ax = 0, ay = 0;
            if (!PadScript::stickAxes(gc, arg(0), ax, ay) || arg(2).empty())
                return "err stick left|right <x> <y>";
            setAxis(s.pad, gc, ax, atoi(arg(1).c_str()));
            setAxis(s.pad, gc, ay, atoi(arg(2).c_str()));
            noteInput();
            return "ok";
        }
        if (v == "trigger") {
            PadScript::Target t;
            if (!PadScript::triggerTarget(gc, arg(0), t) || arg(1).empty())
                return "err trigger l2|r2 <0..255>";
            const int value = atoi(arg(1).c_str());
            setAxis(s.pad, gc, t.axis, PadScript::triggerValue(value));
            if (t.button >= 0)
                in.setVirtualPadButton(s.pad, t.button, value > 0);
            noteInput();
            return "ok";
        }
        if (v == "dpad") {
            int hat = 0;
            if (!PadScript::dpadHat(arg(0), hat))
                return "err dpad up|down|left|right|up-left|...|center";
            if (gc) {
                bool down[4];
                PadScript::dpadButtons(hat, down);
                for (int i = 0; i < 4; i++)
                    setButton(s.pad, true, 11 + i, down[i]);
            } else {
                in.setVirtualPadHat(s.pad, hat);
            }
            noteInput();
            return "ok";
        }
        return "err unknown pad command " + v;
    }

    //*******************************
    // padsim's keyboard words
    //*******************************
    string kbdCommand(istringstream &in) {
        string sub;
        in >> sub;
        if (sub == "plug" || sub == "unplug" || sub == "reset")
            return "ok"; // the driver's keyboard is always there, and holds no key between commands
        if (sub == "type") {
            string text;
            getline(in, text);
            if (!text.empty() && text[0] == ' ')
                text.erase(0, 1);
            Event e;
            e.type = Event::Type::TextInput;
            e.text = text;
            gui_.input().inject(e);
            noteInput();
            return "ok";
        }
        string name;
        in >> name;
        if (sub == "combo") {
            // ctrl+alt+delete: the modifiers held with the last key
            unsigned mods = 0;
            string key;
            size_t start = 0;
            while (start <= name.size()) {
                const size_t plus = name.find('+', start);
                const string part = name.substr(start, plus == string::npos ? string::npos : plus - start);
                if (plus == string::npos) {
                    key = part;
                    break;
                }
                const unsigned m = modifierFor(part);
                if (!m)
                    return "err combo: <modifier>+...+<key> (shift ctrl alt meta)";
                mods |= m;
                start = plus + 1;
            }
            Event e;
            if (!keyEventFor(key, e))
                return "err unknown key " + key;
            e.mods = mods;
            injectKey(e, true);
            sleepMs(60);
            injectKey(e, false);
            return "ok";
        }
        Event e;
        if (!keyEventFor(name, e))
            return "err unknown key " + name;
        if (sub == "press" || sub == "release") {
            injectKey(e, sub == "press");
            return "ok";
        }
        if (sub == "tap") {
            int ms = 60;
            in >> ms;
            injectKey(e, true);
            sleepMs(std::max(0, std::min(60000, ms)));
            injectKey(e, false);
            return "ok";
        }
        return "err kbd plug|unplug|press|release <key>|tap <key> [ms]|combo <k>+<k>|type <text>|reset";
    }

    //*******************************
    // the waits
    //*******************************
    string waitScreen(const string &name, double seconds) {
        const Clock::time_point t0 = Clock::now();
        string now;
        for (;;) {
            now = DebugDriver::currentScreen();
            if (now == name)
                return "ok " + name;
            if (msSince(t0) > seconds * 1000)
                return "err screen " + name + " did not show (now: " + now + ")";
            sleepMs(20);
        }
    }

    string waitIdle(int ms, double seconds) {
        Renderer &r = gui_.renderer();
        const Clock::time_point t0 = Clock::now();
        Clock::time_point since = t0;
        bool haveHash = false;
        uint64_t lastHash = 0;
        vector<unsigned char> pixels;
        for (;;) {
            bool still;
            if (gui_.input().frameNeed() != Input::FrameNeed::Active) {
                still = true; // the screen itself says only ambient motion is left
                haveHash = false;
                sleepMs(20);
            } else {
                r.requestFrameCopy();
                sleepMs(40);
                int w = 0, h = 0, pitch = 0;
                const bool got = r.copyLastFrame(pixels, w, h, pitch) != 0;
                const uint64_t hash = got ? frameHash(pixels) : 0;
                still = got && haveHash && hash == lastHash;
                haveHash = got;
                lastHash = hash;
            }
            if (!still)
                since = Clock::now();
            else if (msSince(since) >= static_cast<unsigned>(ms))
                return "ok";
            if (msSince(t0) > seconds * 1000)
                return "err the screen did not rest for " + to_string(ms) + " ms";
        }
    }

    //*******************************
    // clips
    //*******************************
    string clipCommand(istringstream &in) {
        string sub, name;
        in >> sub;
        if (sub == "stop") {
            if (!clip_)
                return "err no clip is running";
            const string reply = clip_->finish();
            clip_.reset();
            return reply;
        }
        if (sub != "start")
            return "err clip start <name> | clip stop";
        getline(in, name);
        while (!name.empty() && name[0] == ' ')
            name.erase(0, 1);
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".mp4") == 0)
            name.erase(name.size() - 4);
        if (name.empty())
            return "err clip start <name>";
        if (clip_)
            return "err a clip is running (" + clip_->dir() + ")";
        const string dir = DebugDriver::outputPath(outDir_, name);
        if (!DirEntry::createDirs(dir))
            return "err cannot make " + dir;
        // a folder from an earlier clip of the same name: its frames go, the new ones are numbered from 0
        for (const string &f : DirEntry::listNames(dir)) {
            if ((f.size() > 5 && f[0] == 'f' && f.compare(f.size() - 4, 4, ".png") == 0) || f == "clip.ffconcat")
                DirEntry::removeFile(dir + "/" + f);
        }
        clip_.reset(new ClipRecorder(gui_.renderer(), dir));
        return "ok " + dir;
    }

    string handle(const string &line) {
        istringstream in(line);
        string cmd;
        in >> cmd;
        if (cmd.empty() || cmd == "ping")
            return "ok";
        {
            PadScript::Step step;
            string error;
            if (PadScript::parse(line, step, error))
                return error.empty() ? padCommand(step) : "err " + error;
        }
        if (cmd == "press" || cmd == "down" || cmd == "up") {
            string name;
            in >> name;
            Button button;
            bool dpad;
            if (!buttonFor(name, button, dpad))
                return "err unknown button " + name;
            if (cmd == "press") {
                int ms = 60;
                in >> ms;
                injectButton(button, dpad, true);
                sleepMs(ms);
                injectButton(button, dpad, false);
            } else {
                injectButton(button, dpad, cmd == "down");
            }
            return "ok";
        }
        if (cmd == "key") {
            string name;
            in >> name;
            unsigned mods;
            splitModifiers(name, mods);
            Key key = Key::Other;
            int code = 0;
            if (name.size() == 1 && name[0] > 32 && name[0] < 127)
                code = tolower(static_cast<unsigned char>(name[0])); // a character key: "ctrl+c"
            else if (!keyFor(name, key))
                return "err unknown key " + name;
            Event e;
            e.key = key;
            e.mods = mods;
            e.code = code;
            injectKey(e, true);
            injectKey(e, false);
            return "ok";
        }
        if (cmd == "kbd")
            return kbdCommand(in);
        if (cmd == "text") {
            string text;
            getline(in, text);
            if (!text.empty() && text[0] == ' ')
                text.erase(0, 1);
            Event e;
            e.type = Event::Type::TextInput;
            e.text = text;
            gui_.input().inject(e);
            noteInput();
            return "ok";
        }
        if (cmd == "wait") {
            int ms = 0;
            in >> ms;
            sleepMs(ms);
            return "ok";
        }
        if (cmd == "wait_screen") {
            string name;
            double seconds = 15;
            in >> name >> seconds;
            if (name.empty())
                return "err wait_screen <Name> [seconds]";
            return waitScreen(name, seconds);
        }
        if (cmd == "wait_idle") {
            int ms = -1;
            double seconds = 10;
            in >> ms >> seconds;
            if (ms < 0)
                return "err wait_idle <ms> [seconds]";
            return waitIdle(ms, seconds);
        }
        if (cmd == "clip")
            return clipCommand(in);
        if (cmd == "frames")
            return "ok " + to_string(gui_.renderer().frameCount());
        if (cmd == "screen")
            return "ok " + DebugDriver::currentScreen();
        if (cmd == "items") {
            string reply = "ok ";
            const vector<string> names = DebugDriver::items();
            for (size_t i = 0; i < names.size(); i++)
                reply += (i > 0 ? "|" : "") + names[i];
            return reply;
        }
        if (cmd == "shot") {
            string path;
            getline(in, path);
            if (!path.empty() && path[0] == ' ')
                path.erase(0, 1);
            if (path.empty())
                return "err no path";
            path = DebugDriver::outputPath(outDir_, path);
            const string dir = parentDir(path);
            if (!dir.empty())
                DirEntry::createDirs(dir);
            if (!awaitFreshFrame())
                return "err no frame";
            return gui_.renderer().saveLastFrame(path) ? "ok " + path : "err cannot write " + path;
        }
        if (cmd == "window") {
            string what;
            in >> what;
            if (what == "hide")
                gui_.platform().hideWindow();
            else if (what == "show")
                gui_.platform().showWindow();
            else if (what == "min")
                gui_.platform().minimizeWindow();
            else if (what == "restore")
                gui_.platform().restoreWindow();
            else
                return "err window hide|show|min|restore";
            return "ok";
        }
        if (cmd == "quit") {
            // requestQuit(), not inject(Quit): an injected event is a single item in the queue, consumed by
            // whichever screen's own poll() loop happens to read it first - on a nested screen (a PSC-Bios
            // wizard under the System Menu's Network & Controllers hub, say) that only closes the innermost
            // one and nothing above it ever sees a reason to unwind, so the process is left running with
            // nothing left to do (TOOLS-8: measured as a ~13s-and-up stall on `ab_drive.py stop`, ending in
            // its own force-kill rather than a clean exit). requestQuit() is the same persistent condition
            // the real Power button uses (Input::poll() keeps handing back Quit on every call from then on),
            // so every nested screen's own event loop sees it in turn and closes, the way a physical
            // power-off already does.
            gui_.input().requestQuit();
            return "ok";
        }
        return "err unknown command " + cmd;
    }

    GuiBase &gui_;
    int port_;
    string bindAddress_;
    string token_;
    string outDir_; // AB_DEBUG_OUT
    sock_t listener_ = INVALID_SOCKET;
    unsigned long lastInputFrame_ = 0;
    unique_ptr<ClipRecorder> clip_;
};

} // namespace

//*******************************
// DebugDriver::pushScreen / popScreen / currentScreen
//*******************************
void DebugDriver::pushScreen(const char *typeName) {
    lock_guard<mutex> lock(screenMutex);
    screenStack.push_back(plainName(typeName));
}

void DebugDriver::popScreen() {
    lock_guard<mutex> lock(screenMutex);
    if (!screenStack.empty())
        screenStack.pop_back();
}

string DebugDriver::currentScreen() {
    lock_guard<mutex> lock(screenMutex);
    return screenStack.empty() ? string("none") : screenStack.back();
}

//*******************************
// DebugDriver::setItems / items
//*******************************
void DebugDriver::setItems(const vector<string> &names) {
    lock_guard<mutex> lock(screenMutex);
    screenItems = names;
}

vector<string> DebugDriver::items() {
    lock_guard<mutex> lock(screenMutex);
    return screenItems;
}

//*******************************
// DebugDriver::allowedToStart
//*******************************
bool DebugDriver::allowedToStart(const string &bindAddress, const string &token) {
    bool loopback = bindAddress.empty() || bindAddress == "127.0.0.1";
    return loopback || !token.empty();
}

//*******************************
// DebugDriver::tokensMatch
//*******************************
bool DebugDriver::tokensMatch(const string &configured, const string &attempt) {
    if (configured.empty())
        return false; // nothing to match against - callers only compare when a token was actually set
    // constant-time: touch every byte of both strings regardless of where they first differ, so a wrong
    // guess cannot be timed byte by byte
    size_t n = std::max(configured.size(), attempt.size());
    unsigned char diff = static_cast<unsigned char>(configured.size() != attempt.size());
    for (size_t i = 0; i < n; i++) {
        unsigned char a = i < configured.size() ? static_cast<unsigned char>(configured[i]) : 0;
        unsigned char b = i < attempt.size() ? static_cast<unsigned char>(attempt[i]) : 0;
        diff = static_cast<unsigned char>(diff | (a ^ b));
    }
    return diff == 0;
}

//*******************************
// DebugDriver::grabHeader
//*******************************
string DebugDriver::grabHeader(size_t byteCount) {
    return "ok " + to_string(byteCount);
}

//*******************************
// DebugDriver::outputPath
//*******************************
string DebugDriver::outputPath(const string &outDir, const string &path) {
    const bool absolute = (!path.empty() && (path[0] == '/' || path[0] == '\\')) ||
                          (path.size() > 1 && path[1] == ':'); // C:\... or C:/...
    if (outDir.empty() || absolute)
        return path;
    const char last = outDir[outDir.size() - 1];
    return outDir + (last == '/' || last == '\\' ? "" : "/") + path;
}

//*******************************
// DebugDriver::clipConcat
//*******************************
string DebugDriver::clipConcat(const vector<pair<unsigned, string>> &frames, unsigned endMs) {
    string out = "ffconcat version 1.0\n";
    for (size_t i = 0; i < frames.size(); i++) {
        const unsigned until = i + 1 < frames.size() ? frames[i + 1].first : std::max(endMs, frames[i].first);
        const unsigned ms = until > frames[i].first ? until - frames[i].first : 0;
        char duration[32];
        snprintf(duration, sizeof(duration), "%u.%03u", ms / 1000, ms % 1000);
        out += "file '" + frames[i].second + "'\nduration " + duration + "\n";
    }
    if (!frames.empty())
        out += "file '" + frames.back().second + "'\n";
    return out;
}

//*******************************
// DebugDriver::start
//*******************************
bool DebugDriver::start(GuiBase &gui, int port, const string &bindAddress, const string &token) {
    if (!allowedToStart(bindAddress, token)) {
        PLOG_ERROR << "DebugDriver: refusing to bind " << (bindAddress.empty() ? "127.0.0.1" : bindAddress)
                   << " without AB_DEBUG_TOKEN - a LAN driver must be authenticated";
        return false;
    }
    static unique_ptr<Server> server; // lives as long as the process: the thread never ends
    server = make_unique<Server>(gui, port, bindAddress, token);
    if (!server->listen()) {
        PLOG_ERROR << "DebugDriver: cannot listen on " << (bindAddress.empty() ? "127.0.0.1" : bindAddress) << ":"
                   << port;
        server.reset();
        return false;
    }
    PLOG_INFO << "DebugDriver listening on " << (bindAddress.empty() ? "127.0.0.1" : bindAddress) << ":" << port
              << (token.empty() ? "" : " (token required)");
    Server *s = server.get();
    thread([s]() { s->run(); }).detach();
    return true;
}

} // namespace ableem
