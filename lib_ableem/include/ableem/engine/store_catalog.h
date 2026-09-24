// lib_ableem - engine: what the AutoBleem Store offers (docs/store-plan.md in the launcher) - our catalog
// (<repo>/store/<platform>/catalog.json, JSON) and the user's TSV sources, both read into the same items.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ableem {

//******************
// StoreFile
//******************
// one file an item is made of: an App's package, a game's disc (or its archive)
struct StoreFile {
    std::string name;   // the file name to save it as ("" = the URL's last segment)
    std::string url;    // http:// or https://
    uint64_t size = 0;  // 0 = unknown
    std::string sha256; // lower-case hex; "" = unknown
    int disc = 0;       // a game's disc number (1, 2, ...); 0 = none given
};

//******************
// StoreItem
//******************
struct StoreItem {
    std::string id;   // unique per source: the catalog's id, or "<kind>/<title>" for a TSV line
    std::string kind; // "app", "ps1" (later "theme", "rom:<system>"); an unknown kind is kept - the Store skips it
    std::string title;
    std::string version, author, licence, description, serial;
    std::string image;                 // a picture's URL
    std::vector<StoreFile> files;      // in disc order
    std::vector<std::string> dependsOn; // what must be installed first ("pack/psc-libs")
    std::string source;                // the source's display name ("AutoBleem", "Acme Homebrew")

    uint64_t size() const; // the files' sizes added up (0 when one is unknown)
};

//******************
// StoreCatalog
//******************
// {"schema": 1, "platform": "psc", "date": "...", "items": [{"id", "kind", "title", "version", "author",
//  "licence", "description", "serial", "image", "files": [{"name", "url", "size", "sha256", "disc"}],
//  "requires": [...]}]} - an item without an id, a kind, a title or a file is skipped (and counted).
struct StoreCatalog {
    int schema = 0;
    std::string platform, date;
    std::vector<StoreItem> items;
    int skipped = 0;

    bool loadJson(const std::string &text, const std::string &sourceName, std::string &error);
    bool load(const std::string &path, const std::string &sourceName, std::string &error);
};

//******************
// StoreSourceTsv
//******************
// The TSV format (UTF-8, tab-separated, one file per line):
//
//   # autobleem-store 1                  optional: the format and its version
//   # name: Acme Homebrew                optional: the source's display name
//   kind  title  url  size  sha256  disc  serial  image  version  description ...   a header line names the
//                                        columns (the first line with a "url" field); any order, unknown
//                                        columns ignored
//   ps1   Some Game  https://...d1.chd  412334080    1  SLES-12345 ...
//
// Without a header a line is title<TAB>url[<TAB>size] of kind ps1. Lines with the same kind and title are one
// item, its files ordered by disc; version/serial/image/description come from whichever line has them. A
// line without a title or an http(s) url is skipped, and said so in `problems` - the rest still loads.
struct StoreSourceTsv {
    std::string name; // "# name:", else the fallback given
    std::vector<StoreItem> items;
    std::vector<std::string> problems; // "line 7: no url"

    static StoreSourceTsv parse(const std::string &text, const std::string &fallbackName);
    static bool load(const std::string &path, const std::string &fallbackName, StoreSourceTsv &out,
                     std::string &error);
};

} // namespace ableem
