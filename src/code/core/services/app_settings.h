//
// AppSettings: what the player chose for one App in the launcher's "Game settings" menu - kept in the App's own
// folder, in a file the Store/App update never replaces (AppInstaller keeps it like pad.ini). For now one value:
// the pad mode.
//
#pragma once

#include <string>
#include <vector>

//******************
// AppSettings
//******************
// <App folder>/ab_settings.ini, one line per setting:
//     PadMode=psc-kernel
// No file, no line or an unknown value = no choice ("Automatic"). The file is removed when nothing is chosen, so an
// App with the default leaves no trace on the stick.
class AppSettings {
public:
    static const char *const FileName; // "ab_settings.ini"

    // the pad modes an App can be given (app.ini PadMode= or the player's choice), in the order the menu lists them
    static const std::vector<std::string> &padModes(); // {"psc", "x360", "psc-kernel", "x360-kernel"}
    // `value` lower-cased and trimmed when it is one of padModes(), else ""
    static std::string normalizePadMode(const std::string &value);

    // the player's choice for the App in `appFolder`: one of padModes(), or "" (Automatic)
    static std::string padModeOverride(const std::string &appFolder);
    // saves the choice ("" = Automatic: the line goes, and the file with it when it was the only one); false when
    // the folder cannot be written. An unknown value is saved as Automatic.
    static bool setPadModeOverride(const std::string &appFolder, const std::string &mode);

    // AB_APP_PAD_MODE: the player's choice if there is one, else the App's own app.ini PadMode=, else "" (the old
    // behaviour). `appIniMode` is the raw app.ini value; an unknown one counts as none.
    static std::string effectivePadMode(const std::string &overrideMode, const std::string &appIniMode);
};
