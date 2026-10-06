//
// AppSettings - see the header.
//
#include "app_settings.h"
#include "../main.h"

#include <algorithm>
#include <sstream>

using namespace std;

namespace {

const char *const PadModeKey = "padmode";

// the key of an "Key=value" line, lower-cased and trimmed; "" for any other line
string keyOf(const string &line) {
    const size_t eq = line.find('=');
    if (eq == string::npos)
        return "";
    return ableem::toLowerCopy(Strings::trim(line.substr(0, eq)));
}

string valueOf(const string &line) {
    return Strings::trim(line.substr(line.find('=') + 1));
}

vector<string> linesOf(const string &text) {
    vector<string> lines;
    istringstream in(text);
    string line;
    while (getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

} // namespace

const char *const AppSettings::FileName = "ab_settings.ini";

//*******************************
// AppSettings::padModes / normalizePadMode
//*******************************
const vector<string> &AppSettings::padModes() {
    static const vector<string> modes = {"psc", "x360", "psc-kernel", "x360-kernel"};
    return modes;
}

string AppSettings::normalizePadMode(const string &value) {
    const string v = ableem::toLowerCopy(Strings::trim(value));
    const vector<string> &modes = padModes();
    return find(modes.begin(), modes.end(), v) != modes.end() ? v : "";
}

//*******************************
// AppSettings::storedValue / storeValue
//*******************************
string AppSettings::storedValue(const string &appFolder, const string &key) {
    string text;
    if (appFolder.empty() || !DirEntry::readFile(appFolder + sep + FileName, text))
        return "";
    const string wanted = ableem::toLowerCopy(key);
    string value;
    for (const string &line : linesOf(text)) {
        if (keyOf(line) == wanted)
            value = valueOf(line); // the last one wins
    }
    return value;
}

bool AppSettings::storeValue(const string &appFolder, const string &key, const string &value) {
    if (appFolder.empty())
        return false;
    const string path = appFolder + sep + FileName;
    string old;
    DirEntry::readFile(path, old);

    // everything else in the file stays as it is; only this key's line is replaced
    const string wanted = ableem::toLowerCopy(key);
    string text;
    bool others = false;
    for (const string &line : linesOf(old)) {
        if (keyOf(line) == wanted)
            continue;
        text += line + "\n";
        others = others || !Strings::trim(line).empty();
    }
    if (!value.empty())
        text += key + "=" + value + "\n";
    if (value.empty() && !others) {
        // nothing chosen and nothing else in it: no file
        return !DirEntry::exists(path) || DirEntry::removeFile(path);
    }
    return DirEntry::writeFileIfChanged(path, text) != DirEntry::WriteResult::Failed;
}

//*******************************
// AppSettings::padModeOverride / setPadModeOverride
//*******************************
string AppSettings::padModeOverride(const string &appFolder) {
    return normalizePadMode(storedValue(appFolder, PadModeKey));
}

bool AppSettings::setPadModeOverride(const string &appFolder, const string &mode) {
    return storeValue(appFolder, "PadMode", normalizePadMode(mode));
}

//*******************************
// AppSettings - the d-pad / stick flags
//*******************************
const char *const AppSettings::Dpad2AnalogKey = "Dpad2Analog";
const char *const AppSettings::Analog2DpadKey = "Analog2Dpad";

string AppSettings::normalizeFlag(const string &value) {
    const string v = ableem::toLowerCopy(Strings::trim(value));
    if (v == "1" || v == "true" || v == "yes" || v == "on")
        return "1";
    if (v == "0" || v == "false" || v == "no" || v == "off")
        return "0";
    return "";
}

string AppSettings::flagOverride(const string &appFolder, const string &key) {
    return normalizeFlag(storedValue(appFolder, key));
}

bool AppSettings::setFlagOverride(const string &appFolder, const string &key, const string &value) {
    return storeValue(appFolder, key, normalizeFlag(value));
}

string AppSettings::effectiveFlag(const string &overrideValue, const string &appIniValue) {
    const string own = normalizeFlag(overrideValue);
    return own.empty() ? normalizeFlag(appIniValue) : own;
}

//*******************************
// AppSettings::lastPackage / setLastPackage
//*******************************
string AppSettings::lastPackage(const string &appFolder) {
    return storedValue(appFolder, "lastpackage");
}

bool AppSettings::setLastPackage(const string &appFolder, const string &id) {
    const string value = Strings::trim(id);
    if (lastPackage(appFolder) == value)
        return true; // nothing to write
    return storeValue(appFolder, "LastPackage", value);
}

//*******************************
// AppSettings::effectivePadMode
//*******************************
string AppSettings::effectivePadMode(const string &overrideMode, const string &appIniMode) {
    const string own = normalizePadMode(overrideMode);
    return own.empty() ? normalizePadMode(appIniMode) : own;
}
