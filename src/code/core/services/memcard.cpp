//
// Created by lifting the memcardIn/memcardOut halves the PCSX and RetroArch interceptors each had a copy
// of, and reuniting PsGame::setMemCard's two halves.
//

#include "memcard.h"
#include "environment.h"
#include "../main.h"

#include <fstream>
#include <iostream>
#include <ableem/engine/log.h>

using namespace std;

const char *const MemcardService::SonyCard = "SONY";

//*******************************
// MemcardService::manager
//*******************************
ableem::MemcardManager MemcardService::manager() const {
    return ableem::MemcardManager(Env::getPathToGamesDir());
}

//*******************************
// MemcardService::activeCardName
//*******************************
string MemcardService::activeCardName(const PsGame &game) const {
    if (game.internal)
        return SonyCard; // the console's built-in games always use its own card

    IniFile ini;
    ini.load(game.folder + sep + GAME_INI);
    return ini.values["memcard"];
}

//*******************************
// MemcardService::setCardForGame
//*******************************
void MemcardService::setCardForGame(PsGame &game, const string &name) {
    if (game.foreign)
        return; // a RetroArch or App entry has neither a Game.ini nor a database row

    game.memcard = name;

    IniFile ini;
    ini.load(game.folder + sep + GAME_INI);
    ini.values["memcard"] = name;
    ini.save(game.folder + sep + GAME_INI);

    library_.usbGames().updateMemcard(game.gameId, name);
}

//*******************************
// MemcardService::swapInForLaunch
//*******************************
void MemcardService::swapInForLaunch(PsGame &game) {
    if (activeCardName(game) == SonyCard)
        return;

    // The name checked and swapped in is the record's, not the Game.ini's just read. setCardForGame writes
    // both together so they agree.
    if (!manager().swapIn(game.ssFolder, game.memcard)) {
        // the set is gone: fall back to the stock card rather than run on whatever is there
        PLOG_WARNING << "Memory card set " << game.memcard << " could not be swapped in, falling back to SONY";
        setCardForGame(game, SonyCard);
        return;
    }
    // only now: a crash while the set was still being copied in leaves no journal, and recovery never copies a
    // half-copied card back over the set
    writeJournal(game.ssFolder, game.memcard, "");
}

//*******************************
// MemcardService::noteSessionCard
//*******************************
void MemcardService::noteSessionCard(PsGame &game, const string &file) {
    string folder, set, card1;
    readJournal(folder, set, card1); // the set swapInForLaunch noted, if any
    writeJournal(game.ssFolder, set, file);
}

//*******************************
// MemcardService::journalPath / writeJournal / readJournal / recoverAfterCrash
//*******************************
// three lines, as they are (a folder name may hold anything an ini would read as a comment): the game's save-state
// folder, the set ("" none), the session card file ("" none)
string MemcardService::journalPath() {
    return Env::getPathToSaveStatesDir() + sep + "memcard-swap.txt";
}

void MemcardService::writeJournal(const string &folder, const string &set, const string &card1) {
    DirEntry::writeFileIfChanged(journalPath(), folder + "\n" + set + "\n" + card1 + "\n");
}

bool MemcardService::readJournal(string &folder, string &set, string &card1) {
    ifstream in(journalPath());
    if (!in.good())
        return false;
    Strings::getlineRemoveCR(in, folder);
    Strings::getlineRemoveCR(in, set);
    Strings::getlineRemoveCR(in, card1);
    return true;
}

void MemcardService::recoverAfterCrash() {
    const string path = journalPath();
    string folder, set, card1;
    if (!readJournal(folder, set, card1))
        return;
    PLOG_WARNING << "The last game did not hand its memory cards back (a crash): " << folder << " set '" << set << "'"
                 << (card1.empty() ? "" : " session card " + card1) << " - putting them back";
    if (!folder.empty() && DirEntry::exists(folder)) {
        // RetroArch's copy holds the session's saves: onto the game's card first, its own .srm back (raMemcardOut)
        if (!card1.empty() && DirEntry::exists(card1)) {
            DirEntry::copy(card1, folder + sep + "memcards" + sep + "card1.mcd");
            if (DirEntry::exists(card1 + ".bak")) {
                DirEntry::removeFile(card1);
                DirEntry::renameFile(card1 + ".bak", card1);
            }
        }
        // the cards back into the set, the game's own back in its folder (MemcardManager::swapOut)
        if (!set.empty() && set != SonyCard)
            manager().swapOut(folder, set);
    }
    DirEntry::removeFile(path);
}

//*******************************
// MemcardService::swapOutAfterLaunch
//*******************************
std::string MemcardService::setDirForLaunch(PsGame &game) {
    if (activeCardName(game) == SonyCard)
        return "";
    const string dir = Env::getPathToMemCardsDir() + sep + game.memcard;
    if (game.memcard.empty() || !DirEntry::exists(dir + sep + "card1.mcd")) {
        PLOG_WARNING << "Memory card set " << game.memcard << " is not there, falling back to SONY";
        setCardForGame(game, SonyCard);
        return "";
    }
    return dir;
}

void MemcardService::swapOutAfterLaunch(PsGame &game) {
    if (activeCardName(game) != SonyCard)
        manager().swapOut(game.ssFolder, game.memcard);
    // the cards are back (a RetroArch session card was copied back before this - raMemcardOut)
    if (DirEntry::exists(journalPath()))
        DirEntry::removeFile(journalPath());
}

//*******************************
// MemcardService:: the sets
//*******************************
vector<string> MemcardService::listCards() const {
    return manager().list();
}
void MemcardService::createCard(const string &name) {
    manager().create(name);
}
void MemcardService::removeCard(const string &name) {
    manager().remove(name);
}

void MemcardService::renameCard(const string &oldName, const string &newName) {
    manager().rename(oldName, newName); // also rewrites every Game.ini that named the old set
}

void MemcardService::storeGameCardsAsSet(const string &gameMemcardsPath, const string &name) {
    manager().storeToRepo(gameMemcardsPath, name);
}
