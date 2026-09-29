#include "ableem/ui/input.h"
#include "ableem/ui/keyboard_map.h"
#include "ableem/engine/keyboard_presence.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "ableem/ui/platform.h"
#include "sdl_common.h"
#include "perf_overlay.h"
#include "psc_event_filter.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <ableem/engine/log.h>

#ifndef _WIN32
#include <signal.h>
#endif

namespace ableem {

namespace {

//******************
// SIGTERM / SIGINT
//******************
// SDL turns both into one SDL_QUIT - a window's close button - which the innermost screen's loop eats and which
// the launcher, off a dev host, takes for a display that went away (it rebuilds it and carries on). So a
// handler of ours takes SDL's place, and poll() turns the flag into requestQuit(): every screen unwinds and the
// program leaves the way a power off or the DebugDriver's `quit` does. SDL leaves a handler that is not its own
// alone (it installs its own only over SIG_DFL, and restores only its own), so this one stays across the
// display hand-offs.
volatile sig_atomic_t termSignal = 0;

#ifndef _WIN32
void onTermSignal(int) {
    termSignal = 1;
}

void installTermHandler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onTermSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
}
#else
void installTermHandler() {}
#endif

// a piece of SDL work another thread (the DebugDriver's) hands to the thread that polls Input - see
// Input::Impl::onMain
struct MainTask {
    std::function<bool()> fn;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false, cancelled = false, result = false;
};

// one of the DebugDriver's virtual pads, not a device the machine has (what AB_INPUT_ISOLATED keeps)
bool isVirtualDevice(int joystickIndex) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    return SDL_JoystickIsVirtual(joystickIndex) == SDL_TRUE;
#else
    (void)joystickIndex;
    return false;
#endif
}

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
    const bool isolated = Input::isolationRequested(); // AB_INPUT_ISOLATED
    std::thread::id mainThread = std::this_thread::get_id();
    bool keyboardAsPad = true; // the PC-style map (keyboard_map.h), every platform
    bool devKeyMap;            // a dev host's letter map as well (translateKeyboardToPad)
    bool keySeen = false;      // a key went down this session: a keyboard is here, whatever detect() says
    bool powerKeyAsKey = false;
    bool rawKeyboard = false;   // setRawKeyboard(): Esc is a key, not the power button
    bool quitRequested = false; // requestQuit(): poll() returns Quit on every other call from then on

    // the frame pacer (Input::frameDue)
    std::atomic<FrameNeed> need{FrameNeed::Active}; // read by the DebugDriver's `wait_idle` from its thread
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
        if (!barrierReleases.empty())
            return true;
        std::lock_guard<std::mutex> lock(injectedMutex);
        return !injected.empty();
    }

    // CONSOLE-13, the busy rule (see flushInputEvents in the header): what poll() has handed out as pressed and
    // not yet as released - a press is a ButtonDown/DpadDown/KeyDown, told apart as flushInputEvents' InputId
    // does. The end of a busy job releases all of them (barrierReleases, handed out first), and from then on a
    // release or a key repeat of anything not in here is not handed out: its press was made before or during
    // the job. Only the thread that polls touches these.
    std::vector<Event> held;
    std::deque<Event> barrierReleases;
    bool keyRepeat = false;   // the SDL key event the last poll read was the key's own repeat
    bool swallowText = false; // a key repeat was just kept back: the text it types comes next
    void admit(Event &out);
    void releaseHeld();
    std::vector<std::string> mappingPaths;
    std::string currentMappingPath;
    std::vector<std::unique_ptr<Pad>> pads;

    explicit Impl(Platform &p) : platform(p), devKeyMap(p.isDevHost()) {}

    //*******************************
    // work handed to the polling thread
    //*******************************
    std::mutex tasksMutex;
    std::deque<std::shared_ptr<MainTask>> tasks;

    bool tasksPending() {
        std::lock_guard<std::mutex> lock(tasksMutex);
        return !tasks.empty();
    }

    void runTasks() {
        for (;;) {
            std::shared_ptr<MainTask> task;
            {
                std::lock_guard<std::mutex> lock(tasksMutex);
                if (tasks.empty())
                    return;
                task = tasks.front();
                tasks.pop_front();
            }
            std::unique_lock<std::mutex> lock(task->mutex);
            if (task->cancelled)
                continue; // its caller gave up waiting: never done late
            task->result = task->fn();
            task->done = true;
            task->cv.notify_all();
        }
    }

    // fn on the thread that polls (at once when that is this one), false when it did not run within 3 s - the
    // program was busy with something that reads no input
    bool onMain(std::function<bool()> fn) {
        if (std::this_thread::get_id() == mainThread)
            return fn();
        auto task = std::make_shared<MainTask>();
        task->fn = std::move(fn);
        {
            std::lock_guard<std::mutex> lock(tasksMutex);
            tasks.push_back(task);
        }
        std::unique_lock<std::mutex> lock(task->mutex);
        if (!task->cv.wait_for(lock, std::chrono::seconds(3), [&task] { return task->done; })) {
            task->cancelled = true;
            return false;
        }
        return task->result;
    }

    //*******************************
    // virtual pads (see Input::plugVirtualPad)
    //*******************************
    struct VirtualSlot {
        bool plugged = false;
        VirtualPadSpec spec;
        SDL_Joystick *joystick = nullptr;         // our own handle, to set its state through
        SDL_GameController *controller = nullptr; // a game controller's: what its mapping binds each control to
        SDL_JoystickID instance = -1;
        Uint8 hat = 0; // the hat's bits as set so far (a d-pad bound to a hat: each direction one bit)
    };
    VirtualSlot vslots[Input::VirtualPadSlots];
    std::mutex vmutex; // guards each slot's joystick handle: the setters run on the DebugDriver's thread

    // the slot's pad made in SDL (it is plugged); true also when the joystick subsystem is down - probePads()
    // plugs it in then
    bool attachVirtual(int slot) {
#if SDL_VERSION_ATLEAST(2, 24, 0)
        VirtualSlot &v = vslots[slot];
        if (v.joystick || !SDL_WasInit(SDL_INIT_JOYSTICK))
            return true;
        SDL_VirtualJoystickDesc d;
        SDL_zero(d);
        d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        d.type = v.spec.gameController ? SDL_JOYSTICK_TYPE_GAMECONTROLLER : SDL_JOYSTICK_TYPE_UNKNOWN;
        d.naxes = static_cast<Uint16>(v.spec.axes);
        d.nbuttons = static_cast<Uint16>(v.spec.buttons);
        d.nhats = static_cast<Uint16>(v.spec.hats);
        d.vendor_id = v.spec.vendor;
        d.product_id = v.spec.product;
        if (v.spec.gameController) {
            // which of SDL's standard buttons and axes the pad has, in their enum order - what SDL builds the
            // pad's mapping from
            d.button_mask = v.spec.buttons >= 32 ? 0xffffffffu : ((1u << v.spec.buttons) - 1);
            d.axis_mask = v.spec.axes >= 32 ? 0xffffffffu : ((1u << v.spec.axes) - 1);
        }
        d.name = v.spec.name.c_str();
        // a test drives a window that may never have the focus (a hidden or headless one): without this SDL
        // drops every pad's input while no window of ours is focused
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        const int index = SDL_JoystickAttachVirtualEx(&d);
        if (index < 0) {
            PLOG_WARNING << "Virtual pad " << (slot + 1) << ": " << SDL_GetError();
            return false;
        }
        SDL_Joystick *js = SDL_JoystickOpen(index);
        if (!js) {
            PLOG_WARNING << "Virtual pad " << (slot + 1) << " cannot be opened: " << SDL_GetError();
            SDL_JoystickDetachVirtual(index);
            return false;
        }
        // SDL picks the mapping itself - for an Xbox pad's ids its xpad-shaped default, for the others one from
        // the declared layout - so the controls are set through what it binds them to (setControl below)
        SDL_GameController *controller =
            v.spec.gameController && SDL_IsGameController(index) ? SDL_GameControllerOpen(index) : nullptr;
        std::lock_guard<std::mutex> lock(vmutex);
        v.joystick = js;
        v.controller = controller;
        v.instance = SDL_JoystickInstanceID(js);
        v.hat = 0;
        // the triggers at rest (a full-range axis: -32768 is released)
        if (controller) {
            setControlAxis(v, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
            setControlAxis(v, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
        } else {
            for (int axis : v.spec.triggerAxes) {
                if (axis >= 0)
                    SDL_JoystickSetVirtualAxis(js, axis, -32768);
            }
        }
        PLOG_INFO << "Virtual pad " << (slot + 1) << " plugged in: " << v.spec.name;
        return true;
#else
        (void)slot;
        return false;
#endif
    }

    // the slot's pad taken out of SDL (its `plugged` stays as it is)
    void detachVirtual(int slot) {
#if SDL_VERSION_ATLEAST(2, 24, 0)
        VirtualSlot &v = vslots[slot];
        std::lock_guard<std::mutex> lock(vmutex);
        if (!v.joystick)
            return;
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_JoystickGetDeviceInstanceID(i) == v.instance) {
                SDL_JoystickDetachVirtual(i);
                break;
            }
        }
        if (v.controller)
            SDL_GameControllerClose(v.controller);
        SDL_JoystickClose(v.joystick);
        v.joystick = nullptr;
        v.controller = nullptr;
        v.instance = -1;
#else
        (void)slot;
#endif
    }

#if SDL_VERSION_ATLEAST(2, 24, 0)
    // a game controller's button / axis on whatever the pad's mapping binds it to; vmutex held
    static bool setControl(VirtualSlot &v, int button, bool down) {
        if (!v.controller)
            return false;
        const SDL_GameControllerButtonBind b =
            SDL_GameControllerGetBindForButton(v.controller, static_cast<SDL_GameControllerButton>(button));
        switch (b.bindType) {
        case SDL_CONTROLLER_BINDTYPE_BUTTON:
            return SDL_JoystickSetVirtualButton(v.joystick, b.value.button, down ? SDL_PRESSED : SDL_RELEASED) == 0;
        case SDL_CONTROLLER_BINDTYPE_AXIS:
            return SDL_JoystickSetVirtualAxis(v.joystick, b.value.axis, down ? 32767 : -32768) == 0;
        case SDL_CONTROLLER_BINDTYPE_HAT:
            v.hat = static_cast<Uint8>(down ? (v.hat | b.value.hat.hat_mask) : (v.hat & ~b.value.hat.hat_mask));
            return SDL_JoystickSetVirtualHat(v.joystick, b.value.hat.hat, v.hat) == 0;
        default:
            return false; // the mapping has no such control
        }
    }

    static bool setControlAxis(VirtualSlot &v, int axis, int value) {
        if (!v.controller)
            return false;
        const SDL_GameControllerButtonBind b =
            SDL_GameControllerGetBindForAxis(v.controller, static_cast<SDL_GameControllerAxis>(axis));
        const Sint16 raw = static_cast<Sint16>(std::max(-32768, std::min(32767, value)));
        switch (b.bindType) {
        case SDL_CONTROLLER_BINDTYPE_AXIS:
            return SDL_JoystickSetVirtualAxis(v.joystick, b.value.axis, raw) == 0;
        case SDL_CONTROLLER_BINDTYPE_BUTTON: // a digital trigger
            return SDL_JoystickSetVirtualButton(v.joystick, b.value.button, raw > 0 ? SDL_PRESSED : SDL_RELEASED) == 0;
        default:
            return false;
        }
    }
#endif

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

    // true when the pad is ours now (it is a game controller, not ignored, and was not already registered)
    bool registerPad(int joystickIndex) {
        if (isolated && !isVirtualDevice(joystickIndex))
            return false; // AB_INPUT_ISOLATED: the machine's own pads are someone else's
#if SDL_VERSION_ATLEAST(2, 0, 6)
        // probePads() registers what is there, and SDL still queues a device-added event for each of them
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(joystickIndex);
        for (const auto &pad : pads) {
            if (SDL_JoystickInstanceID(pad->joystick) == id)
                return false;
        }
#endif
        SDL_Joystick *js = SDL_JoystickOpen(joystickIndex);
        if (!js)
            return false;
        SDL_JoystickGUID guid = SDL_JoystickGetGUID(js);
        char guidStr[64];
        SDL_JoystickGetGUIDString(guid, guidStr, sizeof(guidStr));
        if (!SDL_IsGameController(joystickIndex)) {
            SDL_JoystickClose(js);
            return false;
        }
        SDL_GameController *controller = SDL_GameControllerOpen(joystickIndex);
        if (!controller)
            return false;

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
        return true;
    }

    // true when it was one of ours
    bool removePad(int joystickInstanceId) {
        for (size_t i = 0; i < pads.size(); i++) {
            if (SDL_JoystickInstanceID(pads[i]->joystick) == joystickInstanceId) {
                PLOG_INFO << "Pad disconnected: " << pads[i]->index << ":" << pads[i]->name;
                SDL_GameControllerClose(pads[i]->controller);
                pads.erase(pads.begin() + i);
                return true;
            }
        }
        return false;
    }

    // SIGTERM/SIGINT seen: the same as requestQuit()
    void takeTermSignal() {
        if (termSignal && !quitRequested) {
            PLOG_INFO << "Termination signal - leaving";
            quitRequested = true;
        }
    }
};

Input::Input(Platform &platform) : impl(new Impl(platform)) {
    reinstallEventFilter();
    installTermHandler();
    if (impl->isolated) {
        PLOG_INFO << "AB_INPUT_ISOLATED: the machine's own input devices are ignored";
    }
}

bool Input::isolationRequested() {
    static const bool isolated = [] {
        const char *v = getenv("AB_INPUT_ISOLATED");
        return v != nullptr && strcmp(v, "1") == 0;
    }();
    return isolated;
}

bool Input::isolated() const {
    return impl->isolated;
}

//*******************************
// virtual pads
//*******************************
bool Input::virtualPadsSupported() {
#if SDL_VERSION_ATLEAST(2, 24, 0)
    SDL_version v;
    SDL_GetVersion(&v);
    return SDL_VERSIONNUM(v.major, v.minor, v.patch) >= SDL_VERSIONNUM(2, 24, 0);
#else
    return false;
#endif
}

bool Input::plugVirtualPad(int slot, const VirtualPadSpec &spec) {
    if (!virtualPadsSupported() || slot < 0 || slot >= VirtualPadSlots)
        return false;
    Impl *d = impl;
    return impl->onMain([d, slot, spec]() {
        d->detachVirtual(slot);
        d->vslots[slot].spec = spec;
        d->vslots[slot].plugged = true;
        return d->attachVirtual(slot);
    });
}

bool Input::unplugVirtualPad(int slot) {
    if (!virtualPadsSupported() || slot < 0 || slot >= VirtualPadSlots)
        return false;
    Impl *d = impl;
    return impl->onMain([d, slot]() {
        d->detachVirtual(slot);
        d->vslots[slot].plugged = false;
        return true;
    });
}

bool Input::virtualPadPlugged(int slot) const {
    if (!virtualPadsSupported() || slot < 0 || slot >= VirtualPadSlots)
        return false;
    Impl *d = impl;
    return impl->onMain([d, slot]() { return d->vslots[slot].plugged; });
}

bool Input::setVirtualPadButton(int slot, int button, bool down) {
    if (!virtualPadsSupported() || slot < 0 || slot >= VirtualPadSlots)
        return false;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    // straight from the calling thread: SDL locks its joysticks for these, and vmutex keeps our handle alive
    std::lock_guard<std::mutex> lock(impl->vmutex);
    SDL_Joystick *js = impl->vslots[slot].joystick;
    return js && SDL_JoystickSetVirtualButton(js, button, down ? SDL_PRESSED : SDL_RELEASED) == 0;
#else
    (void)button;
    (void)down;
    return false;
#endif
}

bool Input::setVirtualPadAxis(int slot, int axis, int value) {
    if (!virtualPadsSupported() || slot < 0 || slot >= VirtualPadSlots)
        return false;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    const Sint16 v = static_cast<Sint16>(std::max(-32768, std::min(32767, value)));
    std::lock_guard<std::mutex> lock(impl->vmutex);
    SDL_Joystick *js = impl->vslots[slot].joystick;
    return js && SDL_JoystickSetVirtualAxis(js, axis, v) == 0;
#else
    (void)axis;
    (void)value;
    return false;
#endif
}

bool Input::setVirtualPadHat(int slot, int value) {
    if (!virtualPadsSupported() || slot < 0 || slot >= VirtualPadSlots)
        return false;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    std::lock_guard<std::mutex> lock(impl->vmutex);
    SDL_Joystick *js = impl->vslots[slot].joystick;
    return js && SDL_JoystickSetVirtualHat(js, 0, static_cast<Uint8>(value)) == 0;
#else
    (void)value;
    return false;
#endif
}

bool Input::setVirtualPadControl(int slot, int controllerButton, bool down) {
    if (!virtualPadsSupported() || slot < 0 || slot >= VirtualPadSlots)
        return false;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    std::lock_guard<std::mutex> lock(impl->vmutex);
    Impl::VirtualSlot &v = impl->vslots[slot];
    return v.joystick && Impl::setControl(v, controllerButton, down);
#else
    (void)controllerButton;
    (void)down;
    return false;
#endif
}

bool Input::setVirtualPadControlAxis(int slot, int controllerAxis, int value) {
    if (!virtualPadsSupported() || slot < 0 || slot >= VirtualPadSlots)
        return false;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    std::lock_guard<std::mutex> lock(impl->vmutex);
    Impl::VirtualSlot &v = impl->vslots[slot];
    return v.joystick && Impl::setControlAxis(v, controllerAxis, value);
#else
    (void)controllerAxis;
    (void)value;
    return false;
#endif
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
        impl->takeTermSignal();
        if (impl->quitRequested || impl->injectedPending() || impl->tasksPending())
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

//*******************************
// Input::poll
//*******************************
// pollEvent() is the event itself; poll() hands it out under the busy rule (CONSOLE-13): the releases the end of
// a busy job made come first, and Impl::admit() keeps back a release or key repeat of what was pressed before
// or during the job
bool Input::poll(Event &out) {
    if (!impl->barrierReleases.empty()) {
        out = impl->barrierReleases.front();
        impl->barrierReleases.pop_front();
        impl->eventSinceDraw = true;
        if (out.type == Event::Type::DpadUp)
            impl->setDpad(out.button, false);
        return true;
    }
    impl->keyRepeat = false;
    if (!pollEvent(out))
        return false;
    impl->admit(out);
    return true;
}

bool Input::pollEvent(Event &out) {
    out = Event();
    impl->takeTermSignal();
    impl->runTasks(); // the DebugDriver's virtual-pad work, on this thread
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
        const bool ours = impl->registerPad(e.jdevice.which);
        // an ignored device (AB_INPUT_ISOLATED) comes and goes unseen
        if (ours || !impl->isolated)
            out.type = Event::Type::PadAdded;
        return true;
    }
    if (e.type == SDL_JOYDEVICEREMOVED) {
        const bool ours = impl->removePad(e.jdevice.which);
        if (ours || !impl->isolated)
            out.type = Event::Type::PadRemoved;
        return true;
    }

    if (impl->isolated) {
        switch (e.type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_TEXTEDITING:
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
            return true; // consumed: the machine's own keyboard and mouse are someone else's
        default:
            break;
        }
    }

    if (e.type == SDL_KEYDOWN) {
        impl->keySeen = true;
        impl->keyRepeat = e.key.repeat != 0;
    }

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

bool sameInput(const Event &a, const Event &b) {
    InputId ia{}, ib{};
    bool pa = false, pb = false;
    return pressOrRelease(a, ia, pa) && pressOrRelease(b, ib, pb) && ia == ib;
}

} // namespace

//*******************************
// Input::Impl::admit / releaseHeld
//*******************************
// the busy rule (CONSOLE-13): `out` is what pollEvent() read; a press is noted as held, the release of a held
// press ends it, and anything a screen must not see - the release of a press it was never handed (made during
// a busy job, or ended by releaseHeld() at the end of one), the repeat of a key it does not hold, and the text
// such a repeat types - becomes a consumed event (Type::None), as a swallowed key repeat already was
void Input::Impl::admit(Event &out) {
    if (out.type == Event::Type::TextInput) {
        if (swallowText) {
            swallowText = false;
            out = Event();
        }
        return;
    }
    InputId id{};
    bool isPress = false;
    if (!pressOrRelease(out, id, isPress))
        return;
    swallowText = false;
    auto it = std::find_if(held.begin(), held.end(), [&out](const Event &down) { return sameInput(down, out); });
    if (isPress) {
        if (keyRepeat) {
            if (it == held.end()) { // a key held since before the job: its repeats and their text stay back
                out = Event();
                swallowText = true;
            }
            return; // a repeat is not a press of its own
        }
        if (it == held.end())
            held.push_back(out);
        return;
    }
    if (it == held.end()) {
        out = Event(); // nothing handed out is let go of
        return;
    }
    held.erase(it);
}

// the end of a busy job: every press handed out is released now, whether or not the player has let go
void Input::Impl::releaseHeld() {
    for (const Event &down : held) {
        Event up = down;
        up.type = down.type == Event::Type::ButtonDown ? Event::Type::ButtonUp
                  : down.type == Event::Type::DpadDown ? Event::Type::DpadUp
                                                       : Event::Type::KeyUp;
        barrierReleases.push_back(up);
    }
    held.clear();
    swallowText = false;
}

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
    // CONSOLE-13: and nothing a screen was handed stays held - every open press is released, first thing the
    // next poll() hands out; the real releases (the ones kept above, or the player's own later) are then the
    // releases of nothing held, which poll() keeps back
    impl->releaseHeld();
}

void Input::requestQuit() {
    impl->quitRequested = true;
}

bool Input::quitRequested() const {
    return impl->quitRequested;
}

// SDL_FilterEvents' callback: 0 removes the event from the queue - a key's own repeat, nothing else
static int dropKeyRepeat(void *, SDL_Event *e) {
    return e->type == SDL_KEYDOWN && e->key.repeat != 0 ? 0 : 1;
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
        // fast-forward loop is waiting for; without this the loop never sees the release and repeats forever.
        // A held key's own repeats are not "another event" (poll() swallows them for a mapped key): counted, they
        // ended the loop ~0.5 s into every hold on a keyboard, and the list stopped scrolling - so they leave
        // the queue here, before it is looked at
        SDL_FilterEvents(dropKeyRepeat, nullptr);
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
    if (impl->isolated)
        return impl->keySeen; // only what the DebugDriver typed
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
    index = sdlJoystickIndex(index); // Joystick's numbering (AB_INPUT_ISOLATED: the virtual pads only)
    if (index < 0)
        return "";
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

    // the DebugDriver's virtual pads that were plugged in before flushPads() took them away
    for (int slot = 0; slot < VirtualPadSlots; slot++) {
        if (impl->vslots[slot].plugged)
            impl->attachVirtual(slot);
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
    // the virtual pads go too (they stay "plugged": probePads() brings them back)
    for (int slot = 0; slot < VirtualPadSlots; slot++)
        impl->detachVirtual(slot);
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

int Input::activePadCount() const {
    return static_cast<int>(impl->pads.size());
}
int Input::joystickCount() const {
    return visibleJoystickCount();
}

int visibleJoystickCount() {
    if (!Input::isolationRequested())
        return SDL_NumJoysticks();
    int n = 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (isVirtualDevice(i))
            n++;
    }
    return n;
}

int sdlJoystickIndex(int visibleIndex) {
    if (!Input::isolationRequested())
        return visibleIndex;
    for (int i = 0, seen = 0; i < SDL_NumJoysticks(); i++) {
        if (isVirtualDevice(i) && seen++ == visibleIndex)
            return i;
    }
    return -1;
}

std::vector<PadInfo> Input::pads() const {
    std::vector<PadInfo> result;
    for (const auto &pad : impl->pads) {
        result.push_back(PadInfo{pad->name, pad->guid, pad->index, pad->serial});
    }
    return result;
}

} // namespace ableem
