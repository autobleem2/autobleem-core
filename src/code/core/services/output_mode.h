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

    // Is the mode really on the screen - the full-screen window the launcher got is `winW` x `winH`? Auto is
    // whatever the display gives. The console's Weston can keep running in the old mode when its restart did
    // not take (CRT 4:3 round 1: a window of 1280x720 while 720x480 was asked), and a confirm for a mode the
    // player is not in would keep it in config.ini.
    bool shownAt(int winW, int winH) const { return isAuto() || (w == winW && h == winH); }
    // The CRT 4:3 mode: 720x480 (480p, 27 MHz) - shown as "CRT 4:3": the tube - anamorphic (pixel aspect 8:9) and with
    // the safe margin. Every 4:3 output (is43) shows the launcher's 4:3 layout; only this one is the tube. While a 4:3
    // output runs the launcher offers only the
    // themes that have a 4:3 layout (ThemeSpec::supports4x3), and a theme without one is replaced by the default
    // theme once the keep-mode confirm has said the mode works.
    bool isCrt() const { return w == 720 && h == 480; }
    // A 4:3 output, square pixels or not: the Renderer's rule (w*2 <= h*3) - 720x480, 640x480, 1024x768, 1280x1024.
    // The launcher's 4:3 layout runs on all of them, and the default theme is forced on them
    bool is43() const { return w > 0 && h > 0 && w * 2 <= h * 3; } // ableem::isFourByThreeOutput, ab_core cannot link it
    // the CRT mode's config.ini / emulator / boot.sh token
    static const char *CrtToken() { return "720x480"; }

    // `tokens` with the CRT mode moved right after the last of "720" / "1080" (a display's list is smallest-first
    // by 16:9 and then the rest, so 720x480 would come last). Without those two it stays where it is; a list
    // without the CRT mode is unchanged.
    static std::vector<std::string> placeCrt(std::vector<std::string> tokens);
    // After the keep-mode confirm: true when the theme must be replaced by the default one - the CRT mode is
    // running and the theme has no 4:3 layout
    static bool needsDefaultTheme(const OutputMode &kept, bool themeSupports4x3) {
        return kept.is43() && !themeSupports4x3;
    }
    // The theme to switch to for the mode in use (after the keep-mode confirm, or at the start with the mode
    // config.ini already holds): `defaultTheme` when the rule above says so, the theme is not the default itself
    // and the default is installed; "" - no switch - otherwise
    static std::string themeToSwitchTo(const OutputMode &inUse, const std::string &theme, bool themeSupports4x3,
                                       const std::string &defaultTheme, bool defaultInstalled) {
        if (theme == defaultTheme || !defaultInstalled || !needsDefaultTheme(inUse, themeSupports4x3))
            return "";
        return defaultTheme;
    }
    // The theme picker's list: all of `themes` outside the CRT mode; in it only those `supports` says have a 4:3
    // layout - and if none has, the whole list (an empty picker would be worse than a letterboxed theme)
    static std::vector<std::string> themesFor(const OutputMode &running, const std::vector<std::string> &themes,
                                              const std::function<bool(const std::string &)> &supports);

    static OutputMode parse(const std::string &token); // anything unknown is auto
    std::string token() const;                         // "auto", "720", "1080" or "<w>x<h>"
    std::string label() const; // "1080p", "2160p", "1280x1024", "CRT 4:3" (translated); "" for auto

    static const char *MarginKey;                  // "crtmargin": the CRT mode's safe margin, percent per side
    // the margin config.ini's value means: a whole number clamped to 0..20, 5 for none or nonsense (see canvas.h)
    static int crtMargin(const std::string &value);
    // The square-pixel 4:3 modes (640x480, 800x600, 1024x768, 1280x1024 ...) have a margin of their own, "vgamargin":
    // the tube's 5 % default would be wrong for a monitor, so it is 0 for none or nonsense (CONSOLE-17 round 2)
    static const char *VgaMarginKey;
    // The picture height adjust (Options -> Display -> Picture height), one value for every 4:3 output: -40..40 (even) output
    // pixels, 0 for none or nonsense (config.ini "vsize43"); the sign is part of the value
    static const char *VsizeKey;
    static int vsize(const std::string &value);
    static int vgaMargin(const std::string &value);
    // the config.ini key the margin of the mode shown is kept in: the tube's, or the VGA one
    static const char *marginKeyFor(const OutputMode &shown) { return shown.isCrt() ? MarginKey : VgaMarginKey; }
    // the margin (percent per side) a 4:3 output of this mode gets: the tube's setting on 720x480, the VGA one on any
    // other 4:3 size, 0 on a wide one
    static int safeMarginFor(const OutputMode &shown, const std::string &crtValue, const std::string &vgaValue) {
        if (shown.isCrt())
            return crtMargin(crtValue);
        return shown.is43() ? vgaMargin(vgaValue) : 0;
    }
    static const char *ConfigKey;                  // "outputmode"
    static std::string defaultToken();             // the console: "720"; everywhere else "auto"
    static std::string pendingFile();              // <runtime>/outputmode.pending
    static std::string emulatorFile();             // <runtime>/outputmode
    static bool readToken(const std::string &file, std::string &token); // one line, trimmed; false if none
};
