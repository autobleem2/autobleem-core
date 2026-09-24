#include "ableem/engine/config_file_editor.h"
#include "ableem/engine/environment.h"
#include "ableem/engine/filesystem.h"
#include "ableem/engine/strings.h"

#include <fstream>
#include <iostream>
#include <vector>
#include "ableem/engine/log.h"

using namespace std;

namespace ableem {

namespace {

// Does this line set `property`? Both are already lower-cased. The key must be the whole word: followed by
// whitespace, '=' or the end of the line. Matching it as a bare prefix used to make "input_overlay" claim
// "input_overlay_enable" and "input_overlay_opacity" too (and "pcsx_rearmed_frameskip" the newer
// "pcsx_rearmed_frameskip_type"), so a replace clobbered its neighbours and the replaces meant for them
// then found nothing.
bool lineSetsProperty(const string &lcaseline, const string &lcasepattern) {
    if (lcaseline.rfind(lcasepattern, 0) != 0)
        return false;
    if (lcaseline.size() == lcasepattern.size())
        return true;
    char next = lcaseline[lcasepattern.size()];
    return next == ' ' || next == '\t' || next == '=' || next == '\r';
}

} // namespace

//*******************************
// ConfigFileEditor::replaceProperties
//*******************************
// A key the file does not have is appended (since 2026-09-20): a pcsx.cfg copied from an older default
// has no line for an option added later (SlowBoot), and the editor's change must still land. The file is
// written once for the whole batch, and not at all when every line already says what it should
// (DirEntry::writeFileIfChanged) - a launch or an editor key that changed nothing leaves it alone.
void ConfigFileEditor::replaceProperties(const string &fullCfgFilePath, const CfgLines &properties) {
    string text;
    if (!DirEntry::readFile(fullCfgFilePath, text)) {
        PLOG_DEBUG << "  cfg file doesn't exist: " << fullCfgFilePath;
        return;
    }

    vector<string> lines;
    string::size_type start = 0;
    while (start < text.size()) {
        string::size_type end = text.find('\n', start);
        if (end == string::npos)
            end = text.size();
        string line = text.substr(start, end - start);
        // a CRLF file: the file is rewritten as pure LF, and no line may carry a stray \r into the emulator
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }

    vector<bool> found(properties.size(), false);
    vector<string> kept;
    for (auto &line : lines) {
        string lcaseline = line;
        lcase(lcaseline);
        bool removed = false;
        for (size_t i = 0; i < properties.size(); i++) {
            string lcasepattern = properties[i].first;
            lcase(lcasepattern);
            if (lineSetsProperty(lcaseline, lcasepattern)) {
                line = properties[i].second;
                removed = line.empty();
                found[i] = true;
                break;
            }
        }
        if (!removed)
            kept.push_back(line);
    }
    lines.swap(kept);
    for (size_t i = 0; i < properties.size(); i++) {
        if (!found[i] && !properties[i].second.empty())
            lines.push_back(properties[i].second);
    }

    // a plain "\n": pcsx-ab/pcsx-abnxt reject a CRLF pcsx.cfg (fread != ftell in text mode, and a trailing
    // '\r' spoils "Bios = SET_BY_PCSX"). CLAUDE.md: cfg files stay LF.
    string out;
    for (const auto &line : lines)
        out += line + "\n";
    if (DirEntry::writeFileIfChanged(fullCfgFilePath, out) == DirEntry::WriteResult::Written) {
        PLOG_INFO << "Wrote " << fullCfgFilePath << " (" << properties.size() << " setting(s))";
    }
}

//*******************************
// ConfigFileEditor::valueIn
//*******************************
bool ConfigFileEditor::valueIn(const string &text, const string &property, string *value) {
    string lcasepattern = property;
    lcase(lcasepattern);
    string::size_type start = 0;
    while (start < text.size()) {
        string::size_type end = text.find('\n', start);
        if (end == string::npos)
            end = text.size();
        string line = text.substr(start, end - start);
        start = end + 1;
        string lcaseline = line;
        lcase(lcaseline);
        if (!lineSetsProperty(lcaseline, lcasepattern))
            continue;
        string::size_type eq = line.find('=');
        string v = eq == string::npos ? "" : line.substr(eq + 1);
        trim(v);
        if (!v.empty() && v.back() == '\r')
            v.pop_back();
        trim(v);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            v = v.substr(1, v.size() - 2);
        *value = v;
        return true;
    }
    return false;
}

//*******************************
// ConfigFileEditor::getValueFromCfgFile
//*******************************
string ConfigFileEditor::getValueFromCfgFile(string fullCfgFilePath, string property) {
    fstream file(fullCfgFilePath, ios::in);
    vector<string> lines;
    lines.clear();

    if (file.is_open()) {
        string line;
        while (getline(file, line)) {
            string lcaseline = line;
            string lcasepattern = property;
            lcase(lcaseline);
            lcase(lcasepattern);

            if (lineSetsProperty(lcaseline, lcasepattern)) {
                string value = line.substr(lcaseline.find("=") + 1);
                if (!value.empty() && value.back() == '\r') {
                    value.pop_back(); // remove the trailing /r
                }
                trim(value); // remove leading and trailing spaces
                PLOG_DEBUG << "  return: '" << value << "'";
                return value;
            }
        }
        file.close();
    }
    PLOG_DEBUG << "  return: ''";
    return "";
}

//*******************************
// ConfigFileEditor::getValue
// example gamePath = "/media/Games/!SaveStates/7"
// example gamePath = "/media/Games/!SaveStates/Driver 2" or
// example gamePath = "/media/Games/Racing/Driver 2"
//*******************************
string ConfigFileEditor::getValue(string gamePath, string property) {
    string fullCfgFilePath = gamePath + sep + PCSX_CFG;
    if (!DirEntry::exists(fullCfgFilePath)) {
        PLOG_DEBUG << "  cfg file doesn't exist";
        PLOG_DEBUG << "  return: ''";
        return "";
    }

    return getValueFromCfgFile(fullCfgFilePath, property);
}

//*******************************
// ConfigFileEditor::replaceInternal
// example gamePathInSaveStates = "/media/Games/!SaveStates/12"
//*******************************
void ConfigFileEditor::replaceInternal(string gamePathInSaveStates, string property, string newline) {
    string realCfgPath = gamePathInSaveStates + sep + PCSX_CFG;
    replaceProperties(realCfgPath, {{property, newline}});
}

//*******************************
// ConfigFileEditor::replaceUsb
// example entry = "Driver 2"
// example gamePath = "/media/Games/Racing"
//*******************************
void ConfigFileEditor::replaceUsb(string entry, string gamePath, string property, string newline) {
    string realCfgPath = gamePath + sep + entry + sep + PCSX_CFG;
    replaceProperties(realCfgPath, {{property, newline}}); // in the game dir pcsx.cfg

    realCfgPath = Environment::getPathToSaveStatesDir() + sep + entry + sep + PCSX_CFG;
    replaceProperties(realCfgPath, {{property, newline}}); // in the !SaveStates/game/pcsx.cfg
}

//*******************************
// ConfigFileEditor::replace
// example entry = "Driver 2"
// example gamePath = "/media/Games/Racing/Driver 2", internal = false
// example gamePath = "/media/Games/!SaveStates/12", internal = true
//*******************************
void ConfigFileEditor::replace(string entry, string gamePath, string property, string newline, bool internal) {
    if (internal)
        replaceInternal(gamePath, property, newline);
    else
        replaceUsb(entry, DirEntry::getDirNameFromPath(gamePath), property, newline);
}

//*******************************
// ConfigFileEditor::replaceInFile
//*******************************
void ConfigFileEditor::replaceInFile(std::string fullCfgFilePath, std::string property, std::string newline) {
    replaceProperties(fullCfgFilePath, {{property, newline}});
}

} // namespace ableem
