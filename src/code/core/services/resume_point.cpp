//
// Created by lifting PsGame's resume-point methods and PcsxInterceptor::prepare/saveResumePoint out of the
// model and the interceptor.
//

#include "resume_point.h"
#include "environment.h"
#include "../main.h"

#include <fstream>
#include <iostream>
#include <ableem/engine/log.h>
#include <algorithm>
#include <sys/stat.h>

using namespace std;

namespace {

// a RetroArch playlist entry (not an App)
bool isRa(const PsGame &game) {
    return game.foreign && !game.app;
}
// the slots exist for our own games and for RetroArch's, never for an App
bool hasSlots(const PsGame &game) {
    return !game.foreign || isRa(game);
}
string withoutExtension(const string &path) {
    const string name = DirEntry::getFileNameFromPath(path);
    const size_t dot = name.find_last_of('.');
    return dot == string::npos || dot == 0 ? name : name.substr(0, dot);
}
// RetroArch names its state files after the content file without its last extension
string raBaseOf(const PsGame &game) {
    return withoutExtension(game.image_path);
}
// where a game's slots live: its !SaveStates folder, or for a RetroArch game one folder per core and game
string folderOf(const PsGame &game) {
    if (!isRa(game))
        return game.ssFolder;
    return Env::getPathToRetroarchDir() + sep + "ab-states" + sep + withoutExtension(game.core_path) + sep +
           raBaseOf(game);
}
string filenameFile(const PsGame &game) {
    return folderOf(game) + sep + "filename.txt";
}
string keptFilenameFile(const PsGame &game) {
    return folderOf(game) + sep + "filename.txt.res";
}
string slotFilenameFile(const PsGame &game, int slot) {
    return folderOf(game) + sep + "filename." + to_string(slot) + ".txt.res";
}
string statesDir(const PsGame &game) {
    return folderOf(game) + sep + "sstates";
}
string shotsDir(const PsGame &game) {
    return folderOf(game) + sep + "screenshots";
}

string keptStateFile(const PsGame &game, const string &name, int slot) {
    return statesDir(game) + sep + name + ".00" + to_string(slot) + ".res";
}
string freshStateFile(const PsGame &game, const string &name) {
    return statesDir(game) + sep + name + ".000";
}
// slot 0's picture has no number in it, which is why every caller special-cases it
string keptPictureFile(const PsGame &game, const string &name, int slot) {
    return slot == 0 ? shotsDir(game) + sep + name + ".png.res"
                     : shotsDir(game) + sep + name + "." + to_string(slot) + ".png.res";
}

// The second line of a filename file is the base name every other path is built from. This one read was
// written out five times over in PsGame and twice more in the interceptor.
bool readStateName(const string &path, string *name) {
    if (!DirEntry::exists(path))
        return false;
    ifstream is(path.c_str());
    if (!is.is_open())
        return false;
    string line;
    std::getline(is, line);
    if (!std::getline(is, line))
        return false;
    *name = line;
    return true;
}

// the filename file a slot should be read through: its own if it has one, otherwise the shared kept copy
bool readStateNameForSlot(const PsGame &game, int slot, string *name) {
    string path = keptFilenameFile(game);
    if (DirEntry::exists(slotFilenameFile(game, slot)))
        path = slotFilenameFile(game, slot);
    return readStateName(path, name);
}

void removeFilesWithExtensionIn(const string &dir, const string &extension) {
    for (const DirEntry &entry : DirEntry::diru(dir)) {
        if (DirEntry::getFileExtension(entry.name) == extension)
            DirEntry::removeFile(dir + sep + entry.name);
    }
}

} // namespace

//*******************************
// ResumePointService::fresh
//*******************************
string ResumePointService::fresh(const PsGame &game, const string &relative) const {
    if (!exitDir_.empty() && DirEntry::exists(exitDir_ + sep + relative))
        return exitDir_ + sep + relative;
    return game.ssFolder + sep + relative;
}

//*******************************
// ResumePointService::slotIsActive
//*******************************
bool ResumePointService::slotIsActive(const PsGame &game, int slot) const {
    if (isRa(game)) {
        // RetroArch's picture may be missing (a core whose frame cannot be read back): the state is the slot
        string name;
        return readStateNameForSlot(game, slot, &name) && DirEntry::exists(keptStateFile(game, name, slot));
    }
    return !pictureForSlot(game, slot).empty();
}

//*******************************
// ResumePointService::pictureForSlot
//*******************************
string ResumePointService::pictureForSlot(const PsGame &game, int slot) const {
    if (!hasSlots(game))
        return "";
    string name;
    if (!readStateNameForSlot(game, slot, &name))
        return "";
    string picture = keptPictureFile(game, name, slot);
    return DirEntry::exists(picture) ? picture : "";
}

//*******************************
// ResumePointService::timeForSlot
//*******************************
time_t ResumePointService::timeForSlot(const PsGame &game, int slot) const {
    if (!hasSlots(game))
        return 0;
    string name;
    if (!readStateNameForSlot(game, slot, &name))
        return 0;
    struct stat info;
    if (stat(keptStateFile(game, name, slot).c_str(), &info) != 0)
        return 0;
    return info.st_mtime;
}

//*******************************
// ResumePointService::newestSlot
//*******************************
int ResumePointService::newestSlot(const PsGame &game) const {
    int newest = -1;
    time_t newestTime = 0;
    for (int slot = 0; slot < SlotCount; slot++) {
        if (!slotIsActive(game, slot))
            continue;
        const time_t t = timeForSlot(game, slot);
        if (newest == -1 || t > newestTime) {
            newest = slot;
            newestTime = t;
        }
    }
    return newest;
}

//*******************************
// ResumePointService::lastPicture
//*******************************
// The picture of the first slot that has one - slot 0 when it is kept, else the next slot that is. (It used
// to read slot 0's picture name whatever slot it found a filename file for, so a game saved only in slots
// 1-2 had no picture: BUG-37.)
string ResumePointService::lastPicture(const PsGame &game) const {
    if (!hasSlots(game))
        return "";

    for (int slot = 0; slot < SlotCount; slot++) {
        string picture = pictureForSlot(game, slot);
        if (!picture.empty())
            return picture;
    }
    return "";
}

//*******************************
// ResumePointService::storePictureForSlot
//*******************************
// Moves the screenshot PCSX just wrote into place as this slot's picture.
void ResumePointService::storePictureForSlot(const PsGame &game, int slot) {
    if (game.foreign)
        return;
    string name;
    if (!readStateNameForSlot(game, slot, &name))
        return;

    string picture = fresh(game, "screenshots/" + name + ".png");
    if (!DirEntry::exists(picture))
        return;

    string kept = keptPictureFile(game, name, slot);
    DirEntry::removeFile(kept);
    DirEntry::copy(picture, kept);
    DirEntry::removeFile(picture);
}

//*******************************
// ResumePointService::removeSlot
//*******************************
void ResumePointService::removeSlot(const PsGame &game, int slot) {
    if (!hasSlots(game))
        return;
    string name;
    if (!readStateNameForSlot(game, slot, &name))
        return;

    DirEntry::removeFile(keptStateFile(game, name, slot));
    DirEntry::removeFile(keptPictureFile(game, name, slot));
}

//*******************************
// ResumePointService::exitedCleanly
//*******************************
bool ResumePointService::exitedCleanly(const PsGame &game) const {
    if (game.foreign)
        return true; // nothing to write one, so nothing to be missing

    bool clean = DirEntry::exists(fresh(game, "filename.txt"));
    if (!clean) {
        PLOG_WARNING << "'" << fresh(game, "filename.txt") << "' not found, previous run did not exit cleanly";
    }
    return clean;
}

//*******************************
// ResumePointService::prepareForLaunch
//*******************************
string ResumePointService::prepareForLaunch(const PsGame &game, int slot, bool loadInPlace) {
    // whatever a previous crash left behind: PCSX will not overwrite it (each only when it is there - a
    // remove is a write too)
    if (DirEntry::exists(filenameFile(game)))
        DirEntry::removeFile(filenameFile(game));
    removeFilesWithExtensionIn(statesDir(game), "000");
    removeFilesWithExtensionIn(shotsDir(game), "png");
    if (!exitDir_.empty())
        DirEntry::removeDirAndContents(exitDir_); // RAM: the last run's, which nobody kept

    if (slot == -1)
        return ""; // starting from the beginning

    string path = keptFilenameFile(game);
    if (DirEntry::exists(slotFilenameFile(game, slot)))
        path = slotFilenameFile(game, slot);
    if (!DirEntry::exists(path))
        return "";

    ifstream is(path.c_str());
    if (!is.is_open())
        return "";

    // the first line is the disc image the slot was playing, the second the state's base name
    string line;
    std::getline(is, line);
    string lastImageInfo = line;

    // the disc, where the game folder is now (it may have moved) - written only when that changed
    string lastCd = game.ssFolder + sep + "lastcdimg." + to_string(slot) + ".txt";
    DirEntry::writeFileIfChanged(lastCd, game.folder + sep + DirEntry::getFileNameFromPath(lastImageInfo) + "\n");

    if (!std::getline(is, line))
        return "";
    string kept = keptStateFile(game, line, slot);
    if (!DirEntry::exists(kept))
        return "";
    if (loadInPlace)
        return kept; // the emulator reads it where it is ($AB_LOAD_STATE)
    string state = freshStateFile(game, line);
    DirEntry::removeFile(state);
    DirEntry::copy(kept, state);
    return "";
}

//*******************************
// ResumePointService::saveAfterLaunch
//*******************************
// Keeps what the run just wrote as this slot: the state file, the filename file, and the disc image note.
void ResumePointService::saveAfterLaunch(const PsGame &game, int slot) {
    if (isRa(game)) {
        saveRaSlot(game, slot);
        return;
    }
    const string filename = fresh(game, "filename.txt");
    if (!DirEntry::exists(filename))
        return; // the run did not exit cleanly, so there is nothing to keep

    string name;
    if (readStateName(filename, &name)) {
        string kept = keptStateFile(game, name, slot);
        string state = fresh(game, "sstates/" + name + ".000");
        DirEntry::removeFile(kept);
        DirEntry::createDirs(statesDir(game));
        DirEntry::copy(state, kept);
        DirEntry::removeFile(state);
    }

    // a copy, not a rename: the run's file may be in RAM (the exit dir), another filesystem
    string text;
    DirEntry::readFile(filename, text);
    DirEntry::writeFileIfChanged(keptFilenameFile(game), text);
    DirEntry::writeFileIfChanged(slotFilenameFile(game, slot), text);
    DirEntry::removeFile(filename);

    string lastCd = fresh(game, "lastcdimg.txt");
    string keptLastCd = game.ssFolder + sep + "lastcdimg." + to_string(slot) + ".txt";
    if (DirEntry::exists(lastCd)) {
        DirEntry::readFile(lastCd, text);
        DirEntry::writeFileIfChanged(keptLastCd, text);
        if (lastCd.compare(0, game.ssFolder.size(), game.ssFolder) == 0 || exitDir_.empty())
            DirEntry::removeFile(lastCd); // the save-state folder's one (the exit dir's goes with the dir)
    }
}

//*******************************
// ResumePointService::raStatesDir / raAutoState / raStateWritten
//*******************************
string ResumePointService::raStatesDir() {
    return Env::getPathToRetroarchDir() + sep + "savestates";
}

string ResumePointService::raAutoState(const PsGame &game) const {
    return isRa(game) ? raStatesDir() + sep + raBaseOf(game) + ".state.auto" : "";
}

bool ResumePointService::raStateWritten(const PsGame &game) const {
    const string file = raAutoState(game);
    return !file.empty() && DirEntry::exists(file) && DirEntry::fileSize(file) > 0;
}

//*******************************
// ResumePointService::raAutoSaveFailed / logSaysAutoSaveFailed
//*******************************
bool ResumePointService::logSaysAutoSaveFailed(const string &logText, const string &stateFileName) {
    size_t pos = 0;
    while (pos < logText.size()) {
        size_t end = logText.find('\n', pos);
        if (end == string::npos)
            end = logText.size();
        const string line = logText.substr(pos, end - pos);
        pos = end + 1;
        if (line.find("Auto save state to") != string::npos && line.find("failed") != string::npos &&
            line.find(stateFileName) != string::npos)
            return true;
    }
    return false;
}

bool ResumePointService::raAutoSaveFailed(const PsGame &game) const {
    if (!isRa(game))
        return false;
    const string fileName = raBaseOf(game) + ".state.auto";
    for (const string &dir : {Env::getPathToLogsDir(), Env::getPathToPersistentLogsDir()}) {
        string text;
        if (DirEntry::readFile(dir + sep + "retroarch.log", text) && logSaysAutoSaveFailed(text, fileName))
            return true;
    }
    return false;
}

//*******************************
// ResumePointService::raSupportsStates
//*******************************
// the core's .info: savestate = "false" (ScummVM, DOSBox, Quake...) means RetroArch can neither save nor load
bool ResumePointService::raSupportsStates(const PsGame &game) const {
    if (!isRa(game))
        return false;
    const string info = Env::getPathToRetroarchDir() + sep + "info" + sep + withoutExtension(game.core_path) + ".info";
    ifstream is(info.c_str());
    string line;
    while (is.is_open() && std::getline(is, line)) {
        const size_t eq = line.find('=');
        if (eq == string::npos)
            continue;
        string key = line.substr(0, eq), value = line.substr(eq + 1);
        trim(key);
        trim(value);
        value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
        if (key == "savestate" && toLowerCopy(value) == "false")
            return false;
    }
    return true;
}

//*******************************
// ResumePointService::prepareRaLaunch
//*******************************
bool ResumePointService::prepareRaLaunch(const PsGame &game, int slot) {
    const string state = raAutoState(game);
    if (state.empty())
        return false;
    // whatever the last run (or a crash) left: only what this run writes may be offered as its state
    for (const string &file : {state, state + ".png"}) {
        if (DirEntry::exists(file))
            DirEntry::removeFile(file);
    }
    if (slot < 0)
        return false;
    const string kept = keptStateFile(game, raBaseOf(game), slot);
    if (!DirEntry::exists(kept))
        return false;
    DirEntry::createDirs(raStatesDir()); // only a resume writes here before the run (the quiet stick)
    return DirEntry::copy(kept, state);
}

//*******************************
// ResumePointService::saveRaSlot
//*******************************
// Keeps the state RetroArch just wrote (and its picture) as this slot, the way saveAfterLaunch keeps PCSX's.
void ResumePointService::saveRaSlot(const PsGame &game, int slot) {
    if (!raStateWritten(game))
        return;
    const string state = raAutoState(game);
    const string name = raBaseOf(game);
    DirEntry::createDirs(statesDir(game));
    DirEntry::createDirs(shotsDir(game));

    // copied beside the slot first: a full stick must not cost the slot it was meant to replace
    const string kept = keptStateFile(game, name, slot), partial = kept + ".new";
    DirEntry::removeFile(partial);
    if (!DirEntry::copy(state, partial) || !DirEntry::replaceFile(partial, kept)) {
        DirEntry::removeFile(partial);
        PLOG_WARNING << "cannot keep '" << state << "' as slot " << slot;
        return;
    }
    const string picture = keptPictureFile(game, name, slot);
    DirEntry::removeFile(picture);
    if (DirEntry::exists(state + ".png"))
        DirEntry::copy(state + ".png", picture);

    const string text = game.image_path + "\n" + name + "\n";
    DirEntry::writeFileIfChanged(keptFilenameFile(game), text);
    DirEntry::writeFileIfChanged(slotFilenameFile(game, slot), text);

    DirEntry::removeFile(state);
    DirEntry::removeFile(state + ".png");
}
