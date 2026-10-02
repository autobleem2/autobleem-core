//
// DefaultTheme: the theme folder the package ships as its default - the one name the launcher's fallback
// (Config) and the installers (InstallerJob, WindowsInstallJob) agree on. The launcher's shipped config.ini says
// the same ("Theme="; the Pi / PC-stick install.sh reads it from there).
//
// An update keeps the stick's config.ini, so a stick on an older theme would stay on it although a new default
// exists. The rule (UIREV-51): when an update, or the conversion of an AutoBleem 1.0 stick, brings the default
// theme's folder to a stick that did not have it, the "theme" setting is switched to it once; a stick that had
// the folder already keeps the user's choice.
//
#pragma once

namespace DefaultTheme {

inline constexpr const char *Name = "ab2.0.0";

// packageShipsIt: the package carries Themes/<Name>; stickHadIt: that folder was on the stick before the install
inline bool switchesTo(bool packageShipsIt, bool stickHadIt) {
    return packageShipsIt && !stickHadIt;
}

} // namespace DefaultTheme
