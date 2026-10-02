//
// ThemeInstaller: a theme dropped into the themes directory as <name>.zip becomes the folder <name>/.
//
#pragma once

#include "../main.h"

#include <string>
#include <vector>

//******************
// ThemeInstaller
//******************
// Used by tools/theme_convert, not by the launcher: the launcher leaves a theme zip a zip (ThemeZipCache).
// installZips() unpacks every mytheme.zip next to the theme folders to mytheme/ and deletes the zip. The
// archive may hold the theme's files at its root or inside one folder (the way most zips are made;
// "__MACOSX" and other dot/underscore folders are ignored when looking for it). Either layout of theme is
// fine - an old theme.ini one converts afterwards like any other folder.
//
// A zip that replaces an existing folder of the same name is an update: the folder goes. A zip that is
// not an archive, or holds no theme, is renamed <name>.zip.bad so it is not tried again on every boot.
class ThemeInstaller {
public:
    // every <themesDir>/<name>.zip; returns the names of the themes installed
    static std::vector<std::string> installZips(const std::string &themesDir);

    // one archive. False (and the zip renamed .bad) when it could not be installed.
    static bool installZip(const std::string &zipPath, const std::string &themesDir);

    // the theme name a zip installs as: its file name without ".zip"
    static std::string themeName(const std::string &zipPath);

    // where the theme's files are inside an unpacked archive: `dir` itself, or its one real sub-folder
    static std::string findThemeRoot(const std::string &dir);
};
