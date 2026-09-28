#pragma once

#include <string>

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

    static OutputMode parse(const std::string &token); // anything unknown is auto
    std::string token() const;                         // "auto", "720", "1080" or "<w>x<h>"
    std::string label() const;                         // "1080p", "2160p", "1280x1024"; "" for auto

    static const char *ConfigKey;                  // "outputmode"
    static std::string defaultToken();             // the console: "720"; everywhere else "auto"
    static std::string pendingFile();              // <runtime>/outputmode.pending
    static std::string emulatorFile();             // <runtime>/outputmode
    static bool readToken(const std::string &file, std::string &token); // one line, trimmed; false if none
};
