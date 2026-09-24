// lib_ableem - engine: line-oriented "property = value" editing of pcsx.cfg / RetroArch .cfg files.
// A property is matched case-insensitively as the whole key at the start of a line (followed by whitespace or
// '='), and the whole line is replaced.
#pragma once

#include <string>

namespace ableem {

//******************
// ConfigFileEditor
//******************
class ConfigFileEditor {
    void replaceProperty(std::string fullCfgFilePath, std::string property, std::string newline);

public:
    // example gamePath = "/media/Games/!SaveStates/7"
    // example gamePath = "/media/Games/!SaveStates/Driver 2" or
    // example gamePath = "/media/Games/Racing/Driver 2"
    std::string getValue(std::string gamePath, std::string property); // from gamePath/pcsx.cfg
    // from one cfg file; "" when it is missing or has no such line
    std::string getValueFromCfgFile(std::string fullCfgFilePath, std::string property);

    // example gamePathInSaveStates = "/media/Games/!SaveStates/12"
    void replaceInternal(std::string gamePathInSaveStates, std::string property, std::string newline);

    // example entry = "Driver 2"
    // example gamePath = "/media/Games/Racing"
    // replaces in the game dir pcsx.cfg and the !SaveStates/<entry>/pcsx.cfg. Not in the per-disc
    // !SaveStates/<entry>/cfg/*.cfg any more (2026-09-24): the emulators no longer read those, a game's own
    // config is its pcsx.custom.cfg, which only the emulators write (the launcher's PcsxConfig)
    void replaceUsb(std::string entry, std::string gamePath, std::string property, std::string newline);

    void replace(std::string entry, std::string gamePath, std::string property, std::string newline, bool internal);

    // one arbitrary cfg file (RetroArch's retroarch.cfg, a core override, ...)
    void replaceInFile(std::string fullCfgFilePath, std::string property, std::string newline);
};

} // namespace ableem
