//
// PcsxConfig: which file speaks for a game's PCSX configuration - see pcsx_config.h.
//
#include "pcsx_config.h"
#include "../main.h"

#include <ableem/engine/config_file_editor.h>
#include <ableem/engine/log.h>

#include <sys/stat.h>
#include <vector>

using namespace std;

const char *const PcsxConfig::CustomName = "pcsx.custom.cfg";

namespace {

const char *const LegacyHandOff = "autobleem.cfg"; // the old "Save AutoBleem config"
const char *const LegacyGameDir = "cfg";           // upstream's per-disc cfg/<label>-<id>.cfg

// when the file was written, 0 if that cannot be told - only ever compared between files of one folder
long long modifiedAt(const string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? static_cast<long long>(st.st_mtime) : 0;
}

} // namespace

//*******************************
// PcsxConfig::launcherFile / customFile / isCustom
//*******************************
string PcsxConfig::launcherFile(const PsGame &game) {
    return (game.internal ? game.ssFolder : game.folder) + sep + PCSX_CFG;
}

string PcsxConfig::customFile(const PsGame &game) {
    return game.ssFolder + sep + CustomName;
}

bool PcsxConfig::isCustom(const PsGame &game) {
    return !game.foreign && !game.ssFolder.empty() && DirEntry::exists(customFile(game));
}

//*******************************
// PcsxConfig::value
//*******************************
string PcsxConfig::value(const PsGame &game, const string &key) {
    ConfigFileEditor processor;
    if (isCustom(game)) {
        string v = processor.getValueFromCfgFile(customFile(game), key);
        if (!v.empty()) {
            return v;
        }
    }
    return processor.getValueFromCfgFile(launcherFile(game), key);
}

//*******************************
// PcsxConfig::unlock
//*******************************
bool PcsxConfig::unlock(const PsGame &game) {
    if (!isCustom(game)) {
        return true;
    }
    PLOG_INFO << "unlocking " << game.title << ": removing " << customFile(game);
    DirEntry::removeFile(customFile(game));
    return !DirEntry::exists(customFile(game));
}

//*******************************
// PcsxConfig::migrateLegacy
//*******************************
void PcsxConfig::migrateLegacy(const PsGame &game) {
    if (game.foreign || game.ssFolder.empty()) {
        return;
    }

    // every candidate, the newest first to take the custom config's place if there is none yet
    vector<string> legacy;
    string handOff = game.ssFolder + sep + LegacyHandOff;
    if (DirEntry::exists(handOff)) {
        legacy.push_back(handOff);
    }
    string gameDir = game.ssFolder + sep + LegacyGameDir;
    for (const DirEntry &entry : DirEntry::diru_FilesOnly(gameDir)) {
        if (DirEntry::matchExtension(entry.name, ".cfg")) {
            legacy.push_back(gameDir + sep + entry.name);
        }
    }
    if (legacy.empty()) {
        return;
    }

    string newest;
    long long newestAt = -1;
    for (const string &path : legacy) {
        long long at = modifiedAt(path);
        if (at > newestAt) {
            newest = path;
            newestAt = at;
        }
    }

    if (!isCustom(game)) {
        PLOG_INFO << "the game's own config from " << newest << " -> " << customFile(game);
        if (DirEntry::copy(newest, customFile(game))) {
            // what the old copy-back did: the emulator saved the BIOS file it resolved
            ConfigFileEditor().replaceInFile(customFile(game), "Bios", "Bios = SET_BY_PCSX");
        }
    }
    if (!isCustom(game)) {
        PLOG_WARNING << "could not write " << customFile(game) << " - the old configs stay for now";
        return;
    }
    for (const string &path : legacy) {
        DirEntry::removeFile(path);
    }
}
