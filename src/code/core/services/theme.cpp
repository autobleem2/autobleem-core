//
// Theme: the UI theme's theme.json and the directory it is read from.
//

#include "theme.h"
#include "environment.h"
#include "theme_converter.h"
#include "theme_zip_cache.h"

#include <iostream>
#include <ableem/engine/log.h>

using namespace std;

namespace {
const char *THEME_JSON = "theme.json";
} // namespace

//*******************************
// Theme::defaultsPath
//*******************************
string Theme::defaultsPath() {
    return Env::getPathToThemesDir() + sep + "default";
}

//*******************************
// Theme::path
//*******************************
string Theme::path() {
    const string themes = Env::getPathToThemesDir();
    const string name = config_.inifile.values["theme"];
    string path = themes + sep + name;
    if (!DirEntry::exists(path)) {
        // a zip theme that load() unpacked
        path = DirEntry::isDirectory(ThemeZipCache::cacheDir(themes, name)) ? ThemeZipCache::cacheDir(themes, name)
                                                                            : defaultsPath();
    }
    return path;
}

//*******************************
// Theme::load
//*******************************
void Theme::load() {
    // a picked <name>.zip is unpacked into themes/.cache/<name>/ (and converted there) first; whatever else
    // the cache holds - the previous zip theme, a leftover of a power cut - is removed
    ThemeZipCache::prepare(Env::getPathToThemesDir(), config_.inifile.values["theme"]);

    const string defaultsDir = defaultsPath();
    loadedPath_ = path();

    PLOG_INFO << "Loading UI theme:" << loadedPath_;
    if (!ThemeConverter::isThemeFolder(loadedPath_)) {
        loadedPath_ = defaultsDir;
        config_.inifile.values["theme"] = "default";
        config_.save();
    }

    if (ThemeConverter::needsConversion(defaultsDir))
        ThemeConverter::convert(defaultsDir);
    if (loadedPath_ != defaultsDir && ThemeConverter::needsConversion(loadedPath_))
        ThemeConverter::convert(loadedPath_);

    ThemeSpec defaults;
    defaults.load(defaultsDir + sep + THEME_JSON);

    spec_ = ThemeSpec();
    spec_.load(loadedPath_ + sep + THEME_JSON);
    spec_.mergeOver(defaults);
    spec_.resolveFiles(loadedPath_, defaults, defaultsDir);
}
