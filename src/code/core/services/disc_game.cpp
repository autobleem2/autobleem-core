//
// DiscGameService - see the header.
//
#include "disc_game.h"

#include <ableem/engine/config_file_editor.h>
#include <ableem/engine/environment.h>
#include <ableem/engine/filesystem.h>
#include <ableem/engine/ini_file.h>
#include <ableem/engine/serial_scanner.h>

#include <algorithm>

using namespace std;

namespace {

// "Tekken 3 (Europe) (Disc 2)" -> "Tekken 3 (Europe) (Disc 1)"; "" when the name has no "(Disc N)" with N > 1
string firstDiscName(const string &name) {
    size_t at = name.rfind("(Disc ");
    if (at == string::npos)
        return "";
    size_t digits = at + 6;
    size_t end = digits;
    while (end < name.size() && isdigit(static_cast<unsigned char>(name[end])))
        end++;
    if (end == digits || end >= name.size() || name[end] != ')')
        return "";
    if (name.substr(digits, end - digits) == "1")
        return "";
    return name.substr(0, digits) + "1" + name.substr(end);
}

string upper(string s) {
    for (char &c : s)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

//*******************************
// DiscGame::ensureFolder
//*******************************
bool DiscGame::ensureFolder() const {
    if (!game || game->ssFolder.empty())
        return false;
    if (!ableem::DirEntry::createDirs(game->ssFolder))
        return false;
    if (!sharesLibraryGame)
        seedFiles();
    return true;
}

//*******************************
// DiscGame::seedFiles
//*******************************
// A disc no library game shares has no pcsx.cfg, Game.ini or cover in its folder: without them the game options
// menu has nothing to edit (ConfigFileEditor skips a missing file) and pcsx-abnxt boots with its own defaults
// (no BIOS logo). Seeded on first sight; a file that is there is the user's and stays as it is.
void DiscGame::seedFiles() const {
    const string folder = ableem::DirEntry::removeSeparatorFromEndOfPath(game->folder);

    const string cfg = folder + ableem::sep + ableem::PCSX_CFG;
    if (!ableem::DirEntry::exists(cfg)) {
        string text;
        if (!ableem::DirEntry::readFile(ableem::Environment::getWorkingPath() + ableem::sep + ableem::PCSX_CFG, text))
            text = "Bios = SET_BY_PCSX\n";
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        ableem::DirEntry::writeFileIfChanged(cfg, text);
        ableem::ConfigFileEditor().replaceInFile(cfg, "SlowBoot", "SlowBoot = 1"); // the BIOS logo shown
    }

    const string iniPath = folder + ableem::sep + ableem::GAME_INI;
    if (!ableem::DirEntry::exists(iniPath)) {
        ableem::IniFile ini;
        ini.section = "Game";
        ini.values["title"] = game->title;
        ini.values["serial"] = game->serial;
        ini.values["memcard"] = "SONY";
        ini.values["publisher"] = game->publisher;
        ini.values["year"] = to_string(game->year);
        ini.values["players"] = to_string(game->players);
        ini.save(iniPath);
    }

    const string png = folder + ableem::sep + "cover.png";
    if (!cover.empty() && !ableem::DirEntry::exists(png))
        ableem::DirEntry::writeFileIfChanged(png, string(cover.begin(), cover.end()));
}

//*******************************
// DiscGameService::keySerial
//*******************************
string DiscGameService::keySerial(const string &serial) const {
    string own = upper(serial);
    if (own.empty())
        return own;
    const ableem::RdbReader::Record *rec = metadata_.rdb().findBySerial(own);
    if (rec == nullptr)
        return own;
    string firstName = firstDiscName(rec->name);
    if (firstName.empty())
        return own;
    const ableem::RdbReader::Record *first = metadata_.rdb().findByName(firstName);
    if (first == nullptr || first->serial.empty())
        return own;
    return upper(first->serial);
}

//*******************************
// DiscGameService::folderForKey
//*******************************
string DiscGameService::folderForKey(const string &key) {
    return ableem::Environment::getPathToSaveStatesDir() + ableem::sep + "disc-" + key + "/";
}

//*******************************
// DiscGameService::make
//*******************************
DiscGame DiscGameService::make(const string &serial, const string &region, const string &volumeLabel, int sgIndex) {
    DiscGame disc;
    disc.sgIndex = sgIndex;
    disc.keySerial = keySerial(serial);

    // the library game of this set, if any
    const ableem::GameRecord *shared = nullptr;
    ableem::GameRecords usb, internal;
    if (!disc.keySerial.empty()) {
        usb = library_.usbGames().loadUsbGames();
        for (const ableem::GameRecord &rec : usb) {
            ableem::IniFile ini;
            ini.load(rec.folder + ableem::sep + ableem::GAME_INI);
            auto it = ini.values.find("serial");
            if (it != ini.values.end() && keySerial(it->second) == disc.keySerial) {
                shared = &rec;
                break;
            }
        }
        if (shared == nullptr) {
            internal = library_.internalGames().loadInternalGames();
            for (const ableem::GameRecord &rec : internal) {
                if (!rec.serial.empty() && keySerial(rec.serial) == disc.keySerial) {
                    shared = &rec;
                    break;
                }
            }
        }
    }

    auto game = make_shared<PsGame>();
    if (shared != nullptr) {
        static_cast<ableem::GameRecord &>(*game) = *shared; // folder, ssFolder, memcard, ids: its cards, config, resume points
        disc.sharesLibraryGame = true;
    } else {
        game->ssFolder = folderForKey(disc.keySerial.empty() ? volumeLabel : disc.keySerial);
        game->folder = game->ssFolder;
        game->memcard = "SONY";
    }
    game->physicalDisc = true;
    game->serial = serial;
    game->region = !region.empty() ? region : ableem::SerialScanner::serialToRegion(serial);

    ableem::GameMetadata md;
    if (!serial.empty() && metadata_.findBySerial(serial, md) && !md.title.empty()) {
        game->title = md.title;
        game->publisher = md.publisher;
        game->year = md.year;
        game->players = md.players;
        game->recordName = md.recordName;
        disc.cover = md.bytes;
    } else {
        game->title = volumeLabel;
    }
    disc.game = game;
    return disc;
}
