//
// DiscGameService: a disc in a drive as a game whose saves belong to its serial (docs/disc-drive-plan.md, piece 4).
//
#pragma once

#include "../model/ps_game.h"

#include <ableem/engine/game_library.h>
#include <ableem/engine/metadata_lookup.h>

#include <string>
#include <vector>

//******************
// DiscGame
//******************
// The PsGame a launch reads, and what only a disc has. The "physical disc" flag sits beside the PsGame rather
// than in it - LaunchService reads it from here once it takes discs (not part of this piece).
struct DiscGame {
    PsGamePtr game;
    bool physicalDisc = true;
    int sgIndex = -1;           // the drive's /dev/sgN
    std::vector<char> cover;    // the covers db's PNG for the serial; empty when it has none
    std::string keySerial;     // the set's: the first disc's serial when the disc sets say, else the disc's own
    bool sharesLibraryGame = false; // game->ssFolder is that of a game already in the library
    bool inDatabase = false;        // the databases know the serial: the title is theirs, not the volume label

    // Creates game->ssFolder (every level). Called at launch, never at construction: a scan or a boot writes
    // nothing to the stick. True when the folder exists afterwards.
    // A disc no library game shares also gets its pcsx.cfg (the shipped default, SlowBoot = 1), Game.ini (title,
    // serial, memcard=SONY) and cover.png, each only when missing - what the game options menu and the emulator
    // read. ensureFolder() calls it.
    void seedFiles() const;

    bool ensureFolder() const;
};

//******************
// DiscGameService
//******************
// Disc sets are not recorded anywhere in the databases (regional.db has no serial, a DISC row is a file name), so
// the set is found from the RetroArch rdb's names: "Tekken 3 (Europe) (Disc 2)" is the same name with "(Disc 1)"
// in the record of the first disc. Without an rdb, or for a disc it does not list as Disc N, the key is the
// serial itself.
// A library game is matched by its Game.ini "serial" (the first disc's, as the scanner wrote it; an internal game's
// own serial), passed through the same key - so any disc of a library game finds it.
class DiscGameService {
public:
    DiscGameService(ableem::GameLibrary &library, ableem::MetadataLookup &metadata)
        : library_(library), metadata_(metadata) {}

    // serial: "SLUS-01234" (may be ""); region: "U"/"P"/"J" or "US"/... as the caller has it; volumeLabel: the
    // title when nothing knows the serial. Reads the databases; writes nothing.
    DiscGame make(const std::string &serial, const std::string &region, const std::string &volumeLabel, int sgIndex);

    // the set's serial: the first disc's, or `serial` itself
    std::string keySerial(const std::string &serial) const;

    // <saveStates dir>/disc-<KEY-SERIAL>/ - the folder of a disc no library game shares
    static std::string folderForKey(const std::string &keySerial);

private:
    ableem::GameLibrary &library_;
    ableem::MetadataLookup &metadata_;
};
