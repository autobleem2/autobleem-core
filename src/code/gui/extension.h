//
// The AutoBleem SDK's extension interface (docs/extensions-plan.md in the launcher). An extension is a plugin:
// a shared library the launcher loads into its own process. It draws with the launcher's own copy of the SDK
// (this library, ab_core, lib_ableem) - the screens are classic GuiScreens built on the host's Gui, in the
// user's theme - and may work in the background while the carousel is up.
//
// An extension implements Extension and ends one source file with AB_EXTENSION(MyExtension). It is built with
// ab_add_extension() (cmake/ab_extension.cmake), which sets what this header checks for: plog instance 1 for
// its logging, and nothing of the SDK linked in (the symbols are the launcher's).
//
#pragma once

#include <cstdint>
#include <string>

#include <ableem/engine/log.h>

class AppBase;

//******************
// The SDK's ABI stamp
//******************
// A plugin calls the launcher's C++ classes directly, so both must be built the same way. The stamp says
// how: AB_SDK_ABI (bumped by any change to the layout of a class or the signature of a function an extension
// may use), the compiler family and major version, the C++11 string ABI and the target. The launcher compares
// the plugin's ab_extension_abi() with its own AB_SDK_STAMP before calling anything else.
//
// A macro and a string literal on purpose: an inline function here would, on Linux, bind to the launcher's
// own copy when the plugin is loaded, and the plugin would report the launcher's stamp as its own.
// 2: ableem::Event gained mods and code; 3: GuiKeyboard rebuilt, Input::keyboardAsPad()/rawKeyboard() (2026-09-24);
// 4: Extension::runEntry(), extension.ini's Provides= (2026-09-26)
#define AB_SDK_ABI 4

#define AB_SDK_STR2(x) #x
#define AB_SDK_STR(x) AB_SDK_STR2(x)
#if defined(__clang__)
#define AB_SDK_CXX "clang-" AB_SDK_STR(__clang_major__)
#elif defined(__GNUC__)
#define AB_SDK_CXX "gcc-" AB_SDK_STR(__GNUC__)
#else
#define AB_SDK_CXX "cxx"
#endif
#if defined(_GLIBCXX_USE_CXX11_ABI)
#define AB_SDK_STRING_ABI AB_SDK_STR(_GLIBCXX_USE_CXX11_ABI)
#else
#define AB_SDK_STRING_ABI "-"
#endif
#if defined(AB_PLATFORM_PSC)
#define AB_SDK_TARGET "psc"
#elif defined(AB_PLATFORM_RPI) && defined(__aarch64__)
#define AB_SDK_TARGET "rpi64"
#elif defined(AB_PLATFORM_RPI)
#define AB_SDK_TARGET "rpi"
#elif defined(AB_PLATFORM_PCUSB)
#define AB_SDK_TARGET "pcusb"
#elif defined(AB_PLATFORM_WIN)
#define AB_SDK_TARGET "win"
#else
#define AB_SDK_TARGET "dev"
#endif
#define AB_SDK_STAMP                                                                                                   \
    "sdk=" AB_SDK_STR(AB_SDK_ABI) ";cxx=" AB_SDK_CXX ";cxx11abi=" AB_SDK_STRING_ABI ";target=" AB_SDK_TARGET

//******************
// ExtensionHost
//******************
// What the launcher offers an extension. One per loaded extension: the folder, the tag on its log lines and
// its state directory are its own.
class ExtensionHost {
public:
    virtual ~ExtensionHost() = default;

    virtual AppBase &app() = 0;                      // config, theme, language, audio; the Gui is Gui::getInstance()
    virtual const std::string &name() const = 0;     // the folder's name, Extensions/<name>/
    virtual const std::string &folder() const = 0;   // Extensions/<name>/
    virtual const std::string &stateDir() const = 0; // System/Extensions/<name>/ - its own files go here
    virtual bool networkUp() = 0;                    // a default route (always true on Windows)

    virtual void requestRescan() = 0; // games were added or removed: the launcher's scan runs
    virtual void reloadApps() = 0;    // the Apps set changed
    virtual void reloadConfig() = 0;  // config.ini changed (theme, language)
    // a line in the launcher's notification bubble, with a progress bar when total > 0; shown until the
    // next call or clearNotification()
    virtual void notify(const std::string &title, const std::string &detail, uint64_t done, uint64_t total) = 0;
    virtual void clearNotification() = 0;

    // the launcher's log, every line tagged [<name>], and its level - what AB_EXTENSION chains plog
    // instance 1 into
    virtual plog::IAppender *logAppender() = 0;
    virtual plog::Severity logSeverity() = 0;
};

//******************
// Extension
//******************
// What an extension implements. Every call comes from the launcher's main thread; real work belongs on the
// extension's own threads (System::lowerCurrentThreadPriority()), handed over in poll().
class Extension {
public:
    virtual ~Extension() = default;
    // the Extensions list's Cross: show its screens (stack GuiScreens on *Gui::getInstance()), return when done
    virtual void run() = 0;
    // once a frame from the launcher's loop, for an extension with Background=true - keep it cheap
    virtual void poll() {}
    // a game is about to start: the display, the audio and the pads go. Free every Texture and Font kept
    // outside a screen, and pause the threads - the emulator gets the machine
    virtual void suspend() {}
    // the game ended and the display is back
    virtual void resume() {}
    // the launcher is leaving (power off, RetroArch, an update): join the threads, save what must survive
    virtual void shutdown() {}
    // a launcher item that opens the extension at one of its pages: `entry` is one of the names its
    // extension.ini lists in Provides= (e.g. "network" - the System menu's Network & Controllers). Show that
    // page like run() shows the first one and return true; false = not handled (the launcher says so)
    virtual bool runEntry(const std::string &entry) {
        (void)entry;
        return false;
    }
};

#ifdef _WIN32
#define AB_EXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
#define AB_EXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// The two C functions the launcher looks up. The extension's logging goes through plog instance 1, chained
// into the launcher's (instance 0) - chaining instance 0 into itself recursed on Linux, where a plugin's
// instance 0 *is* the launcher's.
#define AB_EXTENSION(ExtensionClass)                                                                                   \
    static_assert(PLOG_DEFAULT_INSTANCE_ID != 0, "build an extension with ab_add_extension() - it logs through "       \
                                                 "plog instance 1 (PLOG_DEFAULT_INSTANCE_ID=1)");                      \
    AB_EXTENSION_EXPORT const char *ab_extension_abi() {                                                               \
        return AB_SDK_STAMP;                                                                                           \
    }                                                                                                                  \
    AB_EXTENSION_EXPORT Extension *ab_extension_create(ExtensionHost &host) {                                          \
        plog::init<PLOG_DEFAULT_INSTANCE_ID>(host.logSeverity(), host.logAppender());                                  \
        return new ExtensionClass(host);                                                                               \
    }

// what the launcher looks the two up as
typedef const char *(*AbExtensionAbiFunction)();
typedef Extension *(*AbExtensionCreateFunction)(ExtensionHost &);
