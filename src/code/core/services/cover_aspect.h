//
// CoverAspectTable: the typical box-art aspect of each RetroArch system, read from
// resources/platform/cover_aspects.cfg. A game with no art of its own gets its system's shape from it (the
// carousel's two-layer placeholder); a game with art never asks - its art gives the aspect. SDL-free.
//
#pragma once

#include <map>
#include <string>

//******************
// CoverAspect
//******************
// width : height of a box, whole numbers (5:7 is a tall NES box). Default 1:1.
struct CoverAspect {
    int w = 1;
    int h = 1;

    float ratio() const { return static_cast<float>(w) / static_cast<float>(h); }
};

//******************
// CoverAspectTable
//******************
// The file is `#` comments and `<database name>=<w>:<h>` lines, the database name spelled as RetroArch's
// playlists and databases spell it ("Nintendo - Nintendo Entertainment System", PsGame::db_name). A line that
// is not that (no `=`, an empty name, a shape that is not two whole numbers from 1 to MaxSide) is ignored; a
// later line for the same system wins. A system the file does not list - and any empty name, an App's -
// is 1:1.
class CoverAspectTable {
public:
    static const int MaxSide = 99;

    // <resourcesDir>/platform/cover_aspects.cfg
    static std::string pathFor(const std::string &resourcesDir);

    // reads the file; a missing one gives an empty table (everything 1:1)
    static CoverAspectTable load(const std::string &path);
    // the same from the text of a file
    static CoverAspectTable parse(const std::string &text);

    // the system's box aspect; a playlist's own ".lpl" suffix on the name is ignored
    CoverAspect aspectFor(const std::string &dbName) const;

    size_t size() const { return aspects_.size(); }

private:
    std::map<std::string, CoverAspect> aspects_;
};
