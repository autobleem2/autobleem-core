#include "ableem/ui/input.h"
#include "ableem/ui/keyboard_map.h"
#include "ableem/engine/keyboard_presence.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <vector>
#include "ableem/ui/platform.h"
#include "sdl_common.h"
#include "perf_overlay.h"
#include "psc_event_filter.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <ableem/engine/log.h>

namespace ableem {

namespace {

//******************
// translateKeyboardToPad
//******************
// development machines usually have no gamepad. rewrite a key event into the pad event the screens expect.
// the letters used here are not used by any screen (the on-screen keyboard navigates with arrows). Only on a
// dev host, next to the PC-style map every platform has (keyboard_map.h), which leaves Space to this one.
//   X = cross     O = circle    S = square    T = triangle
//   I J K L = d-pad up left down right         Space = Start    B = Select (Back)
//   Q = L1        E = R1        1 = L2        2 = R2
// returns true if the event was rewritten in place.
bool translateKeyboardToPad(SDL_Event &event) {
    if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP)
        return false;
    if (event.key.repeat)
        return false;
    bool down = (event.type == SDL_KEYDOWN);
    int button = -1;
    bool dpad = false;
    switch (event.key.keysym.sym) {
    case SDLK_x:
        button = SDL_BTN_CROSS;
        break;
    case SDLK_o:
        button = SDL_BTN_CIRCLE;
        break;
    case SDLK_s:
        button = SDL_BTN_SQUARE;
        break;
    case SDLK_t:
        button = SDL_BTN_TRIANGLE;
        break;
    case SDLK_SPACE:
        button = SDL_BTN_START;
        break;
    case SDLK_b:
        button = SDL_BTN_SELECT;
        break;
    case SDLK_q:
        button = SDL_BTN_L1;
        break;
    case SDLK_e:
        button = SDL_BTN_R1;
        break;
    case SDLK_1:
        button = SDL_BTN_L2;
        break;
    case SDLK_2:
        button = SDL_BTN_R2;
        break;
    case SDLK_i:
        button = SDL_BTN_DUP;
        dpad = true;
        break;
    case SDLK_k:
        button = SDL_BTN_DDOWN;
        dpad = true;
        break;
    case SDLK_j:
        button = SDL_BTN_DLEFT;
        dpad = true;
        break;
    case SDLK_l:
        button = SDL_BTN_DRIGHT;
        dpad = true;
        break;
    default:
        return false;
    }
    event.type = dpad ? (down ? SDL_CONTROLLERHATMOTIONDOWN : SDL_CONTROLLERHATMOTIONUP)
                      : (down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP);
    event.cbutton.button = button;
    event.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
    return true;
}

Button toButton(int b) {
    if (b == SDL_BTN_CROSS)
        return Button::Cross;
    if (b == SDL_BTN_CIRCLE)
        return Button::Circle;
    if (b == SDL_BTN_SQUARE)
        return Button::Square;
    if (b == SDL_BTN_TRIANGLE)
        return Button::Triangle;
    if (b == SDL_BTN_START)
        return Button::Start;
    if (b == SDL_BTN_SELECT)
        return Button::Select;
    if (b == SDL_BTN_L1)
        return Button::L1;
    if (b == SDL_BTN_R1)
        return Button::R1;
    if (b == SDL_BTN_L2)
        return Button::L2;
    if (b == SDL_BTN_R2)
        return Button::R2;
    return Button::None;
}

Button toDpadButton(int b) {
    if (b == SDL_BTN_DUP)
        return Button::DpadUp;
    if (b == SDL_BTN_DDOWN)
        return Button::DpadDown;
    if (b == SDL_BTN_DLEFT)
        return Button::DpadLeft;
    if (b == SDL_BTN_DRIGHT)
        return Button::DpadRight;
    return Button::None;
}

// the console's front buttons come in as keyboard scancodes with no keycode of their own
Key toKey(SDL_Scancode scancode, SDL_Keycode sym) {
    if (scancode == SDL_SCANCODE_SLEEP)
        return Key::Sleep;
    if (scancode == SDL_SCANCODE_AUDIOPLAY)
        return Key::Reset;
    if (scancode == SDL_SCANCODE_EJECT)
        return Key::Open;
    switch (sym) {
    case SDLK_ESCAPE:
        return Key::Escape;
    case SDLK_RETURN:
        return Key::Return;
    case SDLK_UP:
        return Key::Up;
    case SDLK_DOWN:
        return Key::Down;
    case SDLK_LEFT:
        return Key::Left;
    case SDLK_RIGHT:
        return Key::Right;
    case SDLK_PAGEUP:
        return Key::PageUp;
    case SDLK_PAGEDOWN:
        return Key::PageDown;
    case SDLK_HOME:
        return Key::Home;
    case SDLK_END:
        return Key::End;
    case SDLK_TAB:
        return Key::Tab;
    case SDLK_BACKSPACE:
        return Key::Backspace;
    case SDLK_DELETE:
        return Key::Delete;
    case SDLK_KP_ENTER:
        return Key::Return;
    case SDLK_INSERT:
        return Key::Insert;
    default:
        if (sym >= SDLK_F1 && sym <= SDLK_F12)
            return static_cast<Key>(static_cast<int>(Key::F1) + (sym - SDLK_F1));
        return Key::Other;
    }
}

unsigned toMods(Uint16 mod) {
    unsigned mods = 0;
    if (mod & KMOD_SHIFT)
        mods |= KeyMod::Shift;
    if (mod & KMOD_CTRL)
        mods |= KeyMod::Ctrl;
    if (mod & KMOD_ALT)
        mods |= KeyMod::Alt;
    if (mod & KMOD_GUI)
        mods |= KeyMod::Gui;
    return mods;
}

// the character a key is labelled with: SDL's keycode for every key that types one is that character
int toCode(SDL_Keycode sym) {
    return (sym >= 32 && sym < 127) ? static_cast<int>(sym) : 0;
}

bool fileExists(const std::string &path) {
    std::ifstream f(path);
    return f.good();
}

enum { DUP = 0, DDOWN = 1, DLEFT = 2, DRIGHT = 3 };

struct Pad {
    SDL_GameController *controller = nullptr;
    SDL_Joystick *joystick = nullptr;
    std::string name, guid, serial;
    int index = 0;
};

} // namespace

struct Input::Impl {
    Platform &platform;
    bool keyboardAsPad = true; // the PC-style map (keyboard_map.h), every platform
    bool devKeyMap;            // a dev host's letter map as well (translateKeyboardToPad)
    bool keySeen = false;      // a key went down this session: a keyboard is here, whatever detect() says
    bool powerKeyAsKey = false;
    bool rawKeyboard = false;   // setRawKeyboard(): Esc is a key, not the power button
    bool quitRequested = false; // requestQuit(): poll() returns Quit on every other call from then on

    // the frame pacer (Input::frameDue)
    FrameNeed need = FrameNeed::Active;
    std::vector<FrameNeed> needStack;
    bool eventSinceDraw = true;
    Uint32 lastDraw = 0;
    Uint64 ambientDue = 0;  // the ambient timeline: when its next frame is due (performance counter), 0 = not running
    bool quitArmed = false; // ... and false in between, so a "while (poll(e))" drain loop ends
    bool dpadState[4] = {false, false, false, false};
    std::mutex injectedMutex;
    std::deque<Event> injected; // what inject() queued, handed out ahead of SDL's events
    bool takeInjected(Event &out) {
        std::lock_guard<std::mutex> lock(injectedMutex);
        if (injected.empty())
            return false;
        out = injected.front();
        injected.pop_front();
        return true;
    }
    bool injectedPending() {
        std::lock_guard<std::mutex> lock(injectedMutex);
        return !injected.empty();
    }
    std::vector<std::string> mappingPaths;
    std::string currentMappingPath;
    std::vector<std::unique_ptr<Pad>> pads;

    explicit Impl(Platform &p) : platform(p), devKeyMap(p.isDevHost()) {}

    void setDpad(Button b, bool down) {
        if (b == Button::DpadUp)
            dpadState[DUP] = down;
        else if (b == Button::DpadDown)
            dpadState[DDOWN] = down;
        else if (b == Button::DpadLeft)
            dpadState[DLEFT] = down;
        else if (b == Button::DpadRight)
            dpadState[DRIGHT] = down;
    }

    // a KeyDown/KeyUp in `out` made the pad event the PC-style map says (keyboard_map.h); false when the key
    // is not one of the map's, or the map is off. A key held down repeats as a KeyDown with repeat set: a
    // mapped one is swallowed (swallow = true) - the screens have their own hold logic, and a repeated Esc
    // must not fall through to the power off.
    bool mapKey(Event &out, bool repeat, bool &swallow) {
        swallow = false;
        if (!keyboardAsPad || (out.type != Event::Type::KeyDown && out.type != Event::Type::KeyUp))
            return false;
        if (rawKeyboard && out.key == Key::Escape)
            return false;
        const KeyboardMap::Mapped m = KeyboardMap::toPad(out.key, out.code, platform.isDevHost());
        if (!m.mapped())
            return false;
        if (repeat) {
            swallow = true;
            return false;
        }
        const bool down = out.type == Event::Type::KeyDown;
        Event pad;
        if (m.systemChord) {
            // L2 and R2 together, let go of in the other order
            Event second;
            pad.type = second.type = down ? Event::Type::ButtonDown : Event::Type::ButtonUp;
            pad.button = down ? Button::L2 : Button::R2;
            second.button = down ? Button::R2 : Button::L2;
            std::lock_guard<std::mutex> lock(injectedMutex);
            injected.push_front(second);
        } else if (m.button == Button::DpadUp || m.button == Button::DpadDown || m.button == Button::DpadLeft ||
                   m.button == Button::DpadRight) {
            pad.type = down ? Event::Type::DpadDown : Event::Type::DpadUp;
            pad.button = m.button;
            setDpad(m.button, down);
        } else {
            pad.type = down ? Event::Type::ButtonDown : Event::Type::ButtonUp;
            pad.button = m.button;
        }
        out = pad;
        return true;
    }

    void registerPad(int joystickIndex) {
        SDL_Joystick *js = SDL_JoystickOpen(joystickIndex);
        if (!js)
            return;
        SDL_JoystickGUID guid = SDL_JoystickGetGUID(js);
        char guidStr[64];
        SDL_JoystickGetGUIDString(guid, guidStr, sizeof(guidStr));
        if (!SDL_IsGameController(joystickIndex)) {
            SDL_JoystickClose(js);
            return;
        }
        SDL_GameController *controller = SDL_GameControllerOpen(joystickIndex);
        if (!controller)
            return;

        std::unique_ptr<Pad> pad = std::make_unique<Pad>();
        pad->controller = controller;
        pad->joystick = SDL_GameControllerGetJoystick(controller);
        pad->guid = guidStr;
        pad->name = SDL_GameControllerName(controller);
        pad->index = joystickIndex;
#if SDL_VERSION_ATLEAST(2, 0, 14)
        // a Bluetooth DualShock 4 / DualSense read through Linux's hidraw backend reports its own MAC
        // here (see PadInfo::serial); nullptr on anything else - a wired pad, evdev, an older SDL build,
        // or simply a controller whose driver never set one.
        if (const char *serial = SDL_JoystickGetSerial(pad->joystick))
            pad->serial = serial;
#endif
        PLOG_INFO << "New GameController: " << pad->name << " GUID: " << pad->guid;
        pads.push_back(std::move(pad));
    }

    void removePad(int joystickInstanceId) {
        for (size_t i = 0; i < pads.size(); i++) {
            if (SDL_JoystickInstanceID(pads[i]->joystick) == joystickInstanceId) {
                PLOG_INFO << "Pad disconnected: " << pads[i]->index << ":" << pads[i]->name;
                SDL_GameControllerClose(pads[i]->controller);
                pads.erase(pads.begin() + i);
                return;
            }
        }
    }
};

Input::Input(Platform &platform) : impl(new Impl(platform)) {
    reinstallEventFilter();
}

void Input::reinstallEventFilter() {
    SDL_SetEventFilter(&playstation_event_filter, nullptr);
}

Input::~Input() {
    flushPads();
    delete impl;
}

void Input::inject(const Event &e) {
    std::lock_guard<std::mutex> lock(impl->injectedMutex);
    impl->injected.push_back(e);
}

//*******************************
// the frame pacer
//*******************************
void Input::setFrameNeed(FrameNeed need) {
    impl->need = need;
}

Input::FrameNeed Input::frameNeed() const {
    return impl->need;
}

void Input::pushFrameNeed() {
    impl->needStack.push_back(impl->need);
    impl->need = FrameNeed::Active;
    impl->eventSinceDraw = true; // a new screen draws its first frame at once
}

void Input::popFrameNeed() {
    if (!impl->needStack.empty()) {
        impl->need = impl->needStack.back();
        impl->needStack.pop_back();
    }
    impl->eventSinceDraw = true; // and the screen underneath draws again at once
}

/// The ambient rate, 30 fps (AB_AMBIENT_FPS): its frames follow a timeline 1/30 s apart, not "an interval after
// the last one began". A display that does not wait for vsync in present() (the console's) then gets its
// frames evenly; one that does (a Pi) shows each frame one vsync after its time, the same every frame. Counted
// from the last frame instead, the frames drifted against the screen's 60 Hz and came 33 and 50 (or 17) ms
// apart by turns - a judder even in motion that runs on time.
static int ambientFps() {
    static const int fps = [] {
        const char *v = getenv("AB_AMBIENT_FPS");
        const int n = v && *v ? atoi(v) : 0;
        return n > 0 ? n : 30;
    }();
    return fps;
}

bool Input::frameDue() {
    const Uint32 now = SDL_GetTicks();
    if (impl->need == FrameNeed::Active || impl->eventSinceDraw || impl->quitRequested) {
        impl->eventSinceDraw = false;
        impl->lastDraw = now;
        impl->ambientDue = 0; // the timeline starts afresh when the screen rests again
        return true;
    }
    if (impl->need == FrameNeed::Ambient) {
        const Uint64 freq = SDL_GetPerformanceFrequency();
        const Uint64 period = freq / static_cast<Uint64>(ambientFps());
        const Uint64 t = SDL_GetPerformanceCounter();
        if (impl->ambientDue == 0 || t > impl->ambientDue + period)
            impl->ambientDue = t; // starting, or far behind: from here
        if (t < impl->ambientDue) {
            const int ms = static_cast<int>((impl->ambientDue - t) * 1000 / freq);
            // (under a millisecond early at most: the timeline, not this frame's start, sets the next one)
            if (ms > 0 && waitForEvent(ms))
                return false; // input came: the loop polls it, and the next call draws
        }
        impl->ambientDue += period;
        impl->lastDraw = SDL_GetTicks();
        return true;
    }
    const int interval = 250; // Idle: only after input, else four times a second
    const int since = static_cast<int>(now - impl->lastDraw);
    if (since >= interval || waitForEvent(interval - since) == false) {
        impl->lastDraw = SDL_GetTicks();
        return true; // the frame is due (or became due while waiting)
    }
    return false; // input came: the loop polls it, and the next call draws
}

bool Input::waitForEvent(int timeoutMs) {
    const Uint32 start = SDL_GetTicks();
    const Uint64 waitStart = SDL_GetPerformanceCounter();
    struct Noted { // however the wait ends
        Uint64 from;
        ~Noted() { noteIdleWait(SDL_GetPerformanceCounter() - from); }
    } noted{waitStart};
    for (;;) {
        if (impl->quitRequested || impl->injectedPending())
            return true;
        const int elapsed = static_cast<int>(SDL_GetTicks() - start);
        if (elapsed >= timeoutMs)
            return false;
        // in slices, so the DebugDriver's injected events (not SDL events - nothing wakes SDL for them)
        // are seen within 10 ms
        if (SDL_WaitEventTimeout(nullptr, std::min(10, timeoutMs - elapsed)) == 1)
            return true;
    }
}

bool Input::poll(Event &out) {
    out = Event();
    if (impl->quitRequested) {
        // a Quit, then "nothing queued", then a Quit again: every screen drains its events with
        // while (poll(e)) and closes on a Quit - a Quit on every call would never let that loop end
        // (seen on the console, 2026-09-22: the launcher hung on "POWERING OFF")
        impl->quitArmed = !impl->quitArmed;
        if (!impl->quitArmed)
            return false;
        out.type = Event::Type::Quit;
        return true;
    }
    if (impl->takeInjected(out)) {
        impl->eventSinceDraw = true; // the frame pacer draws after input
        if (out.type == Event::Type::DpadDown || out.type == Event::Type::DpadUp)
            impl->setDpad(out.button, out.type == Event::Type::DpadDown);
        // a key the DebugDriver typed goes through the map as a real one would
        if (out.type == Event::Type::KeyDown)
            impl->keySeen = true;
        bool swallow = false;
        impl->mapKey(out, false, swallow);
        return true;
    }
    SDL_Event e;
    if (!SDL_PollEvent(&e))
        return false;
    // motion nobody draws from (a VM's mouse, a stick's jitter) must not wake a resting screen: an Idle
    // Options page ran at 20 fps on the VM from them
    switch (e.type) {
    case SDL_MOUSEMOTION:
    case SDL_JOYAXISMOTION:
    case SDL_JOYBALLMOTION:
    case SDL_CONTROLLERAXISMOTION:
    case SDL_FINGERMOTION:
    case SDL_SYSWMEVENT:
        break;
    default:
        impl->eventSinceDraw = true;
    }

    if (e.type == SDL_JOYDEVICEADDED) {
        impl->registerPad(e.jdevice.which);
        out.type = Event::Type::PadAdded;
        return true;
    }
    if (e.type == SDL_JOYDEVICEREMOVED) {
        impl->removePad(e.jdevice.which);
        out.type = Event::Type::PadRemoved;
        return true;
    }

    if (e.type == SDL_KEYDOWN)
        impl->keySeen = true;

    if (impl->keyboardAsPad && impl->devKeyMap) {
        translateKeyboardToPad(e); // mutates e in place; falls through to the normal handling below
    }

    // the PC-style map, ahead of the power button's check: Esc is Circle there (but on a dev host)
    if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && e.key.keysym.scancode != SDL_SCANCODE_SLEEP) {
        Event key;
        key.type = e.type == SDL_KEYDOWN ? Event::Type::KeyDown : Event::Type::KeyUp;
        key.key = toKey(e.key.keysym.scancode, e.key.keysym.sym);
        key.mods = toMods(e.key.keysym.mod);
        key.code = toCode(e.key.keysym.sym);
        bool swallow = false;
        if (impl->mapKey(key, e.key.repeat != 0, swallow)) {
            out = key;
            return true;
        }
        if (swallow)
            return true; // out stays Type::None: consumed
    }

    if (e.type == SDL_QUIT) {
        out.type = Event::Type::Quit;
        return true;
    }

    if (e.type == SDL_KEYDOWN &&
        (e.key.keysym.scancode == SDL_SCANCODE_SLEEP || (e.key.keysym.sym == SDLK_ESCAPE && !impl->rawKeyboard))) {
        if (impl->powerKeyAsKey) {
            out.type = Event::Type::KeyDown;
            out.key = Key::Sleep;
            return true;
        }
        impl->platform.invokePowerOffHandler();
        return true; // swallowed: the app decides what powering off means, we just report it happened
    }

    if (e.type == SDL_RENDER_TARGETS_RESET || e.type == SDL_RENDER_DEVICE_RESET) {
        out.type = Event::Type::RenderReset;
        return true;
    }

    switch (e.type) {
    case SDL_KEYDOWN:
        out.type = Event::Type::KeyDown;
        out.key = toKey(e.key.keysym.scancode, e.key.keysym.sym);
        out.mods = toMods(e.key.keysym.mod);
        out.code = toCode(e.key.keysym.sym);
        return true;
    case SDL_KEYUP:
        out.type = Event::Type::KeyUp;
        out.key = toKey(e.key.keysym.scancode, e.key.keysym.sym);
        out.mods = toMods(e.key.keysym.mod);
        out.code = toCode(e.key.keysym.sym);
        return true;
    case SDL_TEXTINPUT:
        out.type = Event::Type::TextInput;
        out.text = e.text.text;
        return true;
    case SDL_CONTROLLERBUTTONDOWN:
        out.type = Event::Type::ButtonDown;
        out.button = toButton(e.cbutton.button);
        return true;
    case SDL_CONTROLLERBUTTONUP:
        out.type = Event::Type::ButtonUp;
        out.button = toButton(e.cbutton.button);
        return true;
    case SDL_CONTROLLERHATMOTIONDOWN:
        if (e.cbutton.button == SDL_BTN_DUP)
            impl->dpadState[DUP] = true;
        else if (e.cbutton.button == SDL_BTN_DDOWN)
            impl->dpadState[DDOWN] = true;
        else if (e.cbutton.button == SDL_BTN_DLEFT)
            impl->dpadState[DLEFT] = true;
        else if (e.cbutton.button == SDL_BTN_DRIGHT)
            impl->dpadState[DRIGHT] = true;
        out.type = Event::Type::DpadDown;
        out.button = toDpadButton(e.cbutton.button);
        return true;
    case SDL_CONTROLLERHATMOTIONUP:
        if (e.cbutton.button == SDL_BTN_DUP)
            impl->dpadState[DUP] = false;
        else if (e.cbutton.button == SDL_BTN_DDOWN)
            impl->dpadState[DDOWN] = false;
        else if (e.cbutton.button == SDL_BTN_DLEFT)
            impl->dpadState[DLEFT] = false;
        else if (e.cbutton.button == SDL_BTN_DRIGHT)
            impl->dpadState[DRIGHT] = false;
        out.type = Event::Type::DpadUp;
        out.button = toDpadButton(e.cbutton.button);
        return true;
    default:
        return true; // event consumed from the queue; nothing translatable in it
    }
}

void Input::flushEvents() {
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}

//*******************************
// Input::flushInputEvents
//*******************************
// See the header comment (CONSOLE-11, CONSOLE-12). Keep-list, not a drop-list: device hotplug, Quit and the
// release of a button/key/direction pressed before the job survive, so a custom event type is dropped by
// default rather than needing to be named here - the PSC event filter's synthesized hat-motion events are
// named, as the d-pad's press and release.
namespace {

// what a press and its release have in common: which device, which button/key/hat (kind tells them apart)
struct InputId {
    int kind;
    Sint32 which;
    int code;
    bool operator==(const InputId &o) const { return kind == o.kind && which == o.which && code == o.code; }
};

// true when e is a press (isPress) or a release of something; id says of what
bool pressOrRelease(const SDL_Event &e, InputId &id, bool &isPress) {
    switch (e.type) {
    case SDL_KEYDOWN:
        if (e.key.repeat)
            return false; // a held key's repeat: dropped, and not the press its release belongs to
        // fall through
    case SDL_KEYUP:
        id = {0, 0, static_cast<int>(e.key.keysym.scancode)};
        isPress = e.type == SDL_KEYDOWN;
        return true;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        id = {1, e.cbutton.which, e.cbutton.button};
        isPress = e.type == SDL_CONTROLLERBUTTONDOWN;
        return true;
    case SDL_CONTROLLERHATMOTIONDOWN:
    case SDL_CONTROLLERHATMOTIONUP:
        id = {2, e.cbutton.which, e.cbutton.button};
        isPress = e.type == SDL_CONTROLLERHATMOTIONDOWN;
        return true;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        id = {3, e.jbutton.which, e.jbutton.button};
        isPress = e.type == SDL_JOYBUTTONDOWN;
        return true;
    case SDL_JOYHATMOTION:
        id = {4, e.jhat.which, e.jhat.hat};
        isPress = e.jhat.value != SDL_HAT_CENTERED;
        return true;
    default:
        return false;
    }
}

bool pressOrRelease(const Event &e, InputId &id, bool &isPress) {
    switch (e.type) {
    case Event::Type::ButtonDown:
    case Event::Type::ButtonUp:
    case Event::Type::DpadDown:
    case Event::Type::DpadUp:
        id = {5, 0, static_cast<int>(e.button)};
        isPress = e.type == Event::Type::ButtonDown || e.type == Event::Type::DpadDown;
        return true;
    case Event::Type::KeyDown:
    case Event::Type::KeyUp:
        id = {6, static_cast<Sint32>(e.key), e.code};
        isPress = e.type == Event::Type::KeyDown;
        return true;
    default:
        return false;
    }
}

// a release is kept unless its press was among the events dropped by this same flush (then the whole
// press happened during the job, and neither half of it means anything afterwards)
template <typename E> bool keepRelease(const E &e, std::vector<InputId> &droppedPresses) {
    InputId id{};
    bool isPress = false;
    if (!pressOrRelease(e, id, isPress))
        return false;
    if (isPress) {
        droppedPresses.push_back(id);
        return false;
    }
    for (auto it = droppedPresses.begin(); it != droppedPresses.end(); ++it) {
        if (*it == id) {
            droppedPresses.erase(it);
            return false;
        }
    }
    return true;
}

} // namespace

void Input::flushInputEvents() {
    SDL_PumpEvents();
    std::vector<SDL_Event> keep;
    std::vector<InputId> droppedPresses;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_JOYDEVICEADDED || e.type == SDL_JOYDEVICEREMOVED || e.type == SDL_CONTROLLERDEVICEADDED ||
            e.type == SDL_CONTROLLERDEVICEREMOVED || e.type == SDL_CONTROLLERDEVICEREMAPPED || e.type == SDL_QUIT ||
            keepRelease(e, droppedPresses)) {
            keep.push_back(e);
        }
        // everything else - a press, keyboard/mouse/axis/ball motion, text input/editing - is dropped here.
    }
    for (SDL_Event &kept : keep) {
        SDL_PushEvent(&kept);
    }
    {
        // the DebugDriver's synthetic queue is always input (button/dpad/key/text - see inject()'s call
        // sites), never a device or quit event: only its releases can survive, by the same rule.
        std::lock_guard<std::mutex> lock(impl->injectedMutex);
        std::deque<Event> injectedKeep;
        std::vector<InputId> droppedInjected;
        for (const Event &queued : impl->injected) {
            if (keepRelease(queued, droppedInjected))
                injectedKeep.push_back(queued);
        }
        impl->injected.swap(injectedKeep);
    }
    // a direction half-seen before the flush must not keep reading as held afterwards (a kept release
    // says the same again when it is read)
    impl->dpadState[DUP] = impl->dpadState[DDOWN] = impl->dpadState[DLEFT] = impl->dpadState[DRIGHT] = false;
}

void Input::requestQuit() {
    impl->quitRequested = true;
}

bool Input::quitRequested() const {
    return impl->quitRequested;
}

bool Input::padEventPending() const {
    if (impl->injectedPending())
        return true;
    SDL_PumpEvents();
    SDL_Event e;
    int n = SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERDEVICEREMAPPED);
    n += SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, SDL_CONTROLLERHATMOTIONUP, SDL_CONTROLLERHATMOTIONDOWN);
    if (impl->keyboardAsPad) {
        // on a dev host the pad is the keyboard, so a key going up is the "another event" a screen's
        // fast-forward loop is waiting for; without this the loop never sees the release and repeats forever
        n += SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, SDL_KEYDOWN, SDL_KEYUP);
    }
    return n > 0;
}

bool Input::dpadUp() const {
    return impl->dpadState[DUP];
}
bool Input::dpadDown() const {
    return impl->dpadState[DDOWN];
}
bool Input::dpadLeft() const {
    return impl->dpadState[DLEFT];
}
bool Input::dpadRight() const {
    return impl->dpadState[DRIGHT];
}
bool Input::dpadCentered() const {
    return !impl->dpadState[DUP] && !impl->dpadState[DDOWN] && !impl->dpadState[DLEFT] && !impl->dpadState[DRIGHT];
}

bool Input::keyboardPresent() const {
    return impl->keySeen || KeyboardPresence::detect();
}

void Input::setKeyboardAsPad(bool enabled) {
    impl->keyboardAsPad = enabled;
}

bool Input::keyboardAsPad() const {
    return impl->keyboardAsPad;
}

void Input::setPowerKeyAsKey(bool enabled) {
    impl->powerKeyAsKey = enabled;
}

void Input::setRawKeyboard(bool enabled) {
    impl->rawKeyboard = enabled;
}

bool Input::rawKeyboard() const {
    return impl->rawKeyboard;
}

void Input::loadMappings(const std::vector<std::string> &gameControllerDbPaths) {
    impl->mappingPaths = gameControllerDbPaths;
}

std::string Input::currentMappingPath() const {
    return impl->currentMappingPath;
}

bool Input::addMapping(const std::string &line) {
    int result = SDL_GameControllerAddMapping(line.c_str());
    if (result < 0) {
        PLOG_WARNING << "SDL refused the pad mapping: " << SDL_GetError();
        return false;
    }
    return true;
}

std::string Input::mappingForDeviceIndex(int index) const {
    // by GUID rather than SDL_GameControllerMappingForDeviceIndex: that one is SDL 2.0.6, the console has 2.0.4
    char *mapping = SDL_GameControllerMappingForGUID(SDL_JoystickGetDeviceGUID(index));
    if (!mapping)
        return "";
    std::string result = mapping;
    SDL_free(mapping);
    return result;
}

void Input::probePads() {
    // (SDL_HINT_JOYSTICK_HIDAPI=0 was tried here, 2026-09-21, against the Pi 400's pad re-enumerating for
    // three seconds after every open - through evdev the same pad came up with another GUID and a mapping
    // with Triangle and Square swapped; reverted, the delay is still open)
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    if (!impl->pads.empty()) {
        flushPads();
    }

    bool mappingsLoaded = false;
    for (const std::string &path : impl->mappingPaths) {
        if (fileExists(path)) {
            int loaded = SDL_GameControllerAddMappingsFromFile(path.c_str());
            PLOG_INFO << "Loaded pad mappings " << loaded << " from " << path;
            mappingsLoaded = true;
            impl->currentMappingPath = path;
            break;
        }
    }
    if (!mappingsLoaded) {
        PLOG_WARNING << "default mapping db in use - no gamecontrollerdb.txt file found";
    }

    std::string zeroGuid = "00000000000000000000000000000000";
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
        char guidStr[64];
        SDL_JoystickGetGUIDString(guid, guidStr, sizeof(guidStr));
        if (zeroGuid == guidStr) {
            continue; // invalid gamepad
        }
        if (SDL_IsGameController(i)) {
            impl->registerPad(i);
        }
    }
}

void Input::flushPads() {
    for (auto &pad : impl->pads) {
        SDL_GameControllerClose(pad->controller);
    }
    impl->pads.clear();
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

int Input::activePadCount() const {
    return static_cast<int>(impl->pads.size());
}
int Input::joystickCount() const {
    return SDL_NumJoysticks();
}

std::vector<PadInfo> Input::pads() const {
    std::vector<PadInfo> result;
    for (const auto &pad : impl->pads) {
        result.push_back(PadInfo{pad->name, pad->guid, pad->index, pad->serial});
    }
    return result;
}

} // namespace ableem
