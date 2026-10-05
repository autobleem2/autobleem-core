#pragma once

#include <functional>
#include <string>
#include <vector>

//*******************************
// OutputMode
//*******************************
// The display mode the launcher and the PS1 emulator run in (Options -> Display), SDL-free.
//
// config.ini's `outputmode` token: "auto" (the display's own mode), "720", "1080" (the two the emulator has
// always known) or "<w>x<h>" (any other mode the display lists - 2560x1440, 3840x2160). The same token goes to
// the emulator as AB_OUTPUT_MODE (abfeatures: outputmode) and comes back from it in <runtime>/outputmode when
// the player changes it in the emulator's menu.
//
// A new mode is tried before it is kept: it waits in <runtime>/outputmode.pending (RAM) while the launcher asks
// "Keep this display mode?", and only a Cross writes it into config.ini - so a mode the TV cannot show, a crash
// or a reboot all come back in the old one. On the console the mode is Weston's, set by rc/boot.sh before the
// launcher starts (the pending file first, then config.ini): a change there is a launcher restart, never a reboot.
struct OutputMode {
    int w = 0, h = 0; // 0x0 = auto

    bool isAuto() const { return w <= 0 || h <= 0; }
    bool operator==(const OutputMode &o) const { return (isAuto() && o.isAuto()) || (w == o.w && h == o.h); }
    bool operator!=(const OutputMode &o) const { return !(*this == o); }

    // The CRT 4:3 mode: 720x480 (480p, 27 MHz) - shown as "CRT 4:3". While it runs the launcher offers only the
    // themes that have a 4:3 layout (ThemeSpec::supports4x3), and a theme without one is replaced by the default
    // theme once the keep-mode confirm has said the mode works.
    bool isCrt() const { return w == 720 && h == 480; }
    // the CRT mode's config.ini / emulator / boot.sh token
    static const char *CrtToken() { return "720x480"; }

    // `tokens` with the CRT mode moved right after the last of "720" / "1080" (a display's list is smallest-first
    // by 16:9 and then the rest, so 720x480 would come last). Without those two it stays where it is; a list
    // without the CRT mode is unchanged.
    static std::vector<std::string> placeCrt(std::vector<std::string> tokens);
    // After the keep-mode confirm: true when the theme must be replaced by the default one - the CRT mode is
    // running and the theme has no 4:3 layout
    static bool needsDefaultTheme(const OutputMode &kept, bool themeSupports4x3) {
        return kept.isCrt() && !themeSupports4x3;
    }
    // The theme picker's list: all of `themes` outside the CRT mode; in it only those `supports` says have a 4:3
    // layout - and if none has, the whole list (an empty picker would be worse than a letterboxed theme)
    static std::vector<std::string> themesFor(const OutputMode &running, const std::vector<std::string> &themes,
                                              const std::function<bool(const std::string &)> &supports);

    static OutputMode parse(const std::string &token); // anything unknown is auto
    std::string token() const;                         // "auto", "720", "1080" or "<w>x<h>"
    std::string label() const; // "1080p", "2160p", "1280x1024", "CRT 4:3" (translated); "" for auto

    static const char *ConfigKey;                  // "outputmode"
    static std::string defaultToken();             // the console: "720"; everywhere else "auto"
    static std::string pendingFile();              // <runtime>/outputmode.pending
    static std::string emulatorFile();             // <runtime>/outputmode
    static bool readToken(const std::string &file, std::string &token); // one line, trimmed; false if none
};
