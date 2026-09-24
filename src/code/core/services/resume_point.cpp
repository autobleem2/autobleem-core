//
// Created by lifting PsGame's resume-point methods and PcsxInterceptor::prepare/saveResumePoint out of the
// model and the interceptor.
//

#include "resume_point.h"
#include "../main.h"

#include <fstream>
#include <iostream>
#include <ableem/engine/log.h>

using namespace std;

namespace {

string filenameFile(const PsGame &game) {
    return game.ssFolder + sep + "filename.txt";
}
string keptFilenameFile(const PsGame &game) {
    return game.ssFolder + sep + "filename.txt.res";
}
string slotFilenameFile(const PsGame &game, int slot) {
    return game.ssFolder + sep + "filename." + to_string(slot) + ".txt.res";
}
string statesDir(const PsGame &game) {
    return game.ssFolder + sep + "sstates";
}
string shotsDir(const PsGame &game) {
    return game.ssFolder + sep + "screenshots";
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
    return !pictureForSlot(game, slot).empty();
}

//*******************************
// ResumePointService::pictureForSlot
//*******************************
string ResumePointService::pictureForSlot(const PsGame &game, int slot) const {
    if (game.foreign)
        return "";
    string name;
    if (!readStateNameForSlot(game, slot, &name))
        return "";
    string picture = keptPictureFile(game, name, slot);
    return DirEntry::exists(picture) ? picture : "";
}

//*******************************
// ResumePointService::lastPicture
//*******************************
// Whichever slot the game last stopped in - the first one that has a filename file of its own, else the
// shared one. Note it then looks for slot 0's picture name whatever slot it found, which is how this has
// always worked.
string ResumePointService::lastPicture(const PsGame &game) const {
    if (game.foreign)
        return "";

    string path = keptFilenameFile(game);
    for (int slot = 0; slot < SlotCount; slot++) {
        if (DirEntry::exists(slotFilenameFile(game, slot))) {
            path = slotFilenameFile(game, slot);
            break;
        }
    }

    string name;
    if (!readStateName(path, &name))
        return "";
    string picture = keptPictureFile(game, name, 0);
    return DirEntry::exists(picture) ? picture : "";
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
    if (game.foreign)
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
