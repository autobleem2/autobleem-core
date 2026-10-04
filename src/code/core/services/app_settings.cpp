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
// AppSettings::padModeOverride
//*******************************
string AppSettings::padModeOverride(const string &appFolder) {
    string text;
    if (appFolder.empty() || !DirEntry::readFile(appFolder + sep + FileName, text))
        return "";
    string mode;
    for (const string &line : linesOf(text)) {
        if (keyOf(line) == PadModeKey)
            mode = valueOf(line); // the last one wins
    }
    return normalizePadMode(mode);
}

//*******************************
// AppSettings::setPadModeOverride
//*******************************
bool AppSettings::setPadModeOverride(const string &appFolder, const string &mode) {
    if (appFolder.empty())
        return false;
    const string path = appFolder + sep + FileName;
    string old;
    DirEntry::readFile(path, old);

    // everything else in the file stays as it is; only the PadMode line is replaced
    string text;
    bool others = false;
    for (const string &line : linesOf(old)) {
        if (keyOf(line) == PadModeKey)
            continue;
        text += line + "\n";
        others = others || !Strings::trim(line).empty();
    }
    const string chosen = normalizePadMode(mode);
    if (!chosen.empty())
        text += "PadMode=" + chosen + "\n";
    if (chosen.empty() && !others) {
        // nothing chosen and nothing else in it: no file
        return !DirEntry::exists(path) || DirEntry::removeFile(path);
    }
    return DirEntry::writeFileIfChanged(path, text) != DirEntry::WriteResult::Failed;
}

//*******************************
// AppSettings::effectivePadMode
//*******************************
string AppSettings::effectivePadMode(const string &overrideMode, const string &appIniMode) {
    const string own = normalizePadMode(overrideMode);
    return own.empty() ? normalizePadMode(appIniMode) : own;
}
