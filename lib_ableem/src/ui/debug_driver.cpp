//
// DebugDriver: the TCP line server that drives the program for automated UI tests. See the header.
//
#include "ableem/ui/debug_driver.h"
#include "ableem/ui/input.h"
#include "ableem/ui/platform.h"
#include "ableem/ui/renderer.h"

#include <ableem/engine/log.h>

#include <algorithm>
#include <chrono>
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
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

using namespace std;

namespace ableem {

namespace {
std::mutex screenMutex;
std::vector<std::string> screenStack;
std::vector<std::string> screenItems; // under screenMutex too

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

void sleepMs(int ms) {
    this_thread::sleep_for(chrono::milliseconds(ms));
}

class Server {
public:
    Server(GuiBase &gui, int port, string bindAddress, string token)
        : gui_(gui), port_(port), bindAddress_(std::move(bindAddress)), token_(std::move(token)) {}

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
        gui_.renderer().setFrameCache(true);
        for (;;) {
            sock_t client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                continue;
            serve(client);
            CLOSESOCK(client);
        }
    }

private:
    // true with the next line in `line`; false when the peer closed (nothing left to serve)
    static bool nextLine(sock_t client, string &buffer, string &line) {
        char chunk[512];
        size_t nl = buffer.find('\n');
        while (nl == string::npos) {
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

    static void sendLine(sock_t client, const string &reply) {
        string out = reply + "\n";
        send(client, out.c_str(), static_cast<int>(out.size()), 0);
    }

    void serve(sock_t client) {
        string buffer;
        // A configured token gates the whole connection: the first line must be `auth <token>` (a wrong or
        // missing one is one "err" reply and the socket closes) before any real command is read. Nothing
        // reachable beyond loopback runs without this, by construction of allowedToStart().
        if (!token_.empty()) {
            string line;
            if (!nextLine(client, buffer, line))
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
            sendLine(client, "ok");
        }
        for (;;) {
            string line;
            if (!nextLine(client, buffer, line))
                return;
            istringstream peek(line);
            string cmd;
            peek >> cmd;
            if (cmd == "grab") {
                handleGrab(client);
                continue;
            }
            sendLine(client, handle(line));
        }
    }

    void handleGrab(sock_t client) {
        // a frame newer than the last input, if one comes in time - the same rule `shot` uses
        for (int i = 0; i < 40 && gui_.renderer().frameCount() <= lastInputFrame_; i++)
            sleepMs(10);
        vector<unsigned char> png;
        if (!gui_.renderer().encodeLastFramePng(png)) {
            sendLine(client, "err no frame");
            return;
        }
        sendLine(client, DebugDriver::grabHeader(png.size()));
        size_t sent = 0;
        while (sent < png.size()) {
            int n =
                send(client, reinterpret_cast<const char *>(png.data() + sent), static_cast<int>(png.size() - sent), 0);
            if (n <= 0)
                return;
            sent += static_cast<size_t>(n);
        }
    }

    void injectButton(Button button, bool dpad, bool down) {
        Event e;
        e.button = button;
        if (dpad)
            e.type = down ? Event::Type::DpadDown : Event::Type::DpadUp;
        else
            e.type = down ? Event::Type::ButtonDown : Event::Type::ButtonUp;
        gui_.input().inject(e);
        lastInputFrame_ = gui_.renderer().frameCount();
    }

    string handle(const string &line) {
        istringstream in(line);
        string cmd;
        in >> cmd;
        if (cmd.empty() || cmd == "ping")
            return "ok";
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
            e.type = Event::Type::KeyDown;
            e.key = key;
            e.mods = mods;
            e.code = code;
            gui_.input().inject(e);
            e.type = Event::Type::KeyUp;
            gui_.input().inject(e);
            lastInputFrame_ = gui_.renderer().frameCount();
            return "ok";
        }
        if (cmd == "text") {
            string text;
            getline(in, text);
            if (!text.empty() && text[0] == ' ')
                text.erase(0, 1);
            Event e;
            e.type = Event::Type::TextInput;
            e.text = text;
            gui_.input().inject(e);
            lastInputFrame_ = gui_.renderer().frameCount();
            return "ok";
        }
        if (cmd == "wait") {
            int ms = 0;
            in >> ms;
            sleepMs(ms);
            return "ok";
        }
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
            // a frame newer than the last input, if one comes in time
            for (int i = 0; i < 40 && gui_.renderer().frameCount() <= lastInputFrame_; i++)
                sleepMs(10);
            return gui_.renderer().saveLastFrame(path) ? "ok " + path : "err no frame";
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
            Event e;
            e.type = Event::Type::Quit;
            gui_.input().inject(e);
            return "ok";
        }
        return "err unknown command " + cmd;
    }

    GuiBase &gui_;
    int port_;
    string bindAddress_;
    string token_;
    sock_t listener_ = INVALID_SOCKET;
    unsigned long lastInputFrame_ = 0;
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
