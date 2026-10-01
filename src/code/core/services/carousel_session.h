#pragma once

#include "../model/game_set.h"
#include <string>

//*******************************
// CarouselSession
//*******************************
// The carousel's place - the set, the sub-set, the game, the playlist or folder - as a small text file, SDL-free
// (BUG-40). Session::launcher already carries it across a game's return and an in-process display change; this is
// the same GameSetSelection across a launcher that EXITS and is started over (a display change or "Restart
// launcher" on the console, where rc/boot.sh starts a new process, and on a Pi / PC stick, where the session
// script does). The launcher writes it as it leaves (AutoBleem::run) and the next start takes it ONCE: read, then
// deleted - so it never restores an old place after a crash. It sits in the runtime dir (RAM on the targets), not
// on the stick: nothing of the quiet stick's data root is written for it.
//
// The format is `key=value` lines, `version=1` first; an unknown key is ignored, a missing one keeps its default,
// a bad number or a file with another version is refused whole (false, the selection untouched).
struct CarouselSession {
    static constexpr int Version = 1;

    static std::string file(); // <runtime>/carousel.session

    static std::string serialize(const GameSetSelection &selection);
    // false (and `selection` unchanged) when the text is not a version-1 session
    static bool parse(const std::string &text, GameSetSelection &selection);

    static bool save(const std::string &path, const GameSetSelection &selection);
    // reads `path` into `selection`, then removes the file whether or not it parsed; false when there was none
    static bool take(const std::string &path, GameSetSelection &selection);
};
