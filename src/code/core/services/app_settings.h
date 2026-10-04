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

    // The d-pad / stick flags (the same keys in app.ini and in ab_settings.ini, values 1/0):
    //     Dpad2Analog=1   the d-pad also moves the left stick
    //     Analog2Dpad=1   the left stick also presses the d-pad
    // AB_APP_DPAD2ANALOG / AB_APP_ANALOG2DPAD = the player's choice, else the app.ini value, else "" (the pad output's
    // own default).
    static const char *const Dpad2AnalogKey; // "Dpad2Analog"
    static const char *const Analog2DpadKey; // "Analog2Dpad"
    // "1" / "0" for a value that says on / off (1, true, yes, on / 0, false, no, off; any case), else ""
    static std::string normalizeFlag(const std::string &value);
    // the player's choice for `key` (one of the two above): "1", "0" or "" (Automatic)
    static std::string flagOverride(const std::string &appFolder, const std::string &key);
    // saves it ("" = Automatic: the line goes); false when the folder cannot be written
    static bool setFlagOverride(const std::string &appFolder, const std::string &key, const std::string &value);
    static std::string effectiveFlag(const std::string &overrideValue, const std::string &appIniValue);

private:
    // `key`'s value in ab_settings.ini ("" when there is none); the last line wins
    static std::string storedValue(const std::string &appFolder, const std::string &key);
    // replaces `key`'s line with `key=value` (no line for an empty value); the file goes when nothing is left
    static bool storeValue(const std::string &appFolder, const std::string &key, const std::string &value);
};
