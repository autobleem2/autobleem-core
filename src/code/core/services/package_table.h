//
// PackageTable: the table that tells the launcher which of the player's own files are which game - rc/packages.ini
// (shipped, replaced by every update) and Packages/packages.ini (the player's own, only ever read). autobleem-main
// docs/packages.md section 4. A small reader of its own - IniFile keeps one section, a table has many rows.
//
#pragma once

#include <string>
#include <utility>
#include <vector>

//******************
// PackageRow
//******************
// One row of the table: `[row-id]` and its keys (docs/packages.md 4.3). The row's id is the game's id.
struct PackageRow {
    std::string id;                 // [row-id], lower case, the grammar of a kind
    std::string kind;               // the content kind, lower case
    std::string title;              // the game's display name
    std::vector<std::string> match; // the files that must all exist, relative to the root, '/' separated
    std::string main;               // the main file (AB_PKG_FILE); "" = the first match
    long long size = -1;            // the exact size of the first match file; -1 = any
    std::string magic;              // up to 4 characters the main file starts with; "" = none
    std::string variant;            // free text for the picker's second line
    std::string licence;
    std::vector<std::pair<std::string, std::string>> starts;   // dos-game: FILE|Title
    std::vector<std::pair<std::string, std::string>> settings; // dos-game: set.<name>=<value> (name lower case)
    std::string mapper;                                        // dos-game: a mapper file, relative to the root

    // the main file the row names (the first match when there is no main=)
    std::string mainFile() const { return main.empty() && !match.empty() ? match.front() : main; }
};

//******************
// PackageTable
//******************
class PackageTable {
public:
    // the longest id/kind
    static const size_t MaxKindLength = 32;
    // true when `value` is a kind / id / row id: [a-z0-9]+(-[a-z0-9]+)*, at most `maxLength` characters (lower case
    // only - callers lower-case first)
    static bool validName(const std::string &value, size_t maxLength = MaxKindLength);
    // a relative path of the table or a descriptor: '/' (or '\') separated, no empty or ".." segment, not
    // absolute, no drive letter; the path with '/' separators, "" when it is not acceptable
    static std::string cleanRelativePath(const std::string &path);

    // appends the rows of `text` (UTF-8, LF or CRLF, a BOM ignored) to `rows`, in order. A malformed row is
    // skipped and described in `problems`; the rest still loads.
    static void parse(const std::string &text, std::vector<PackageRow> &rows, std::vector<std::string> &problems);
    // the same for a file; false when it cannot be read (nothing appended)
    static bool load(const std::string &path, std::vector<PackageRow> &rows, std::vector<std::string> &problems);
};
