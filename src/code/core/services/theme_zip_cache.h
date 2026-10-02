//
// ThemeZipCache: a theme left in the themes directory as <name>.zip, unpacked only while it is the picked one.
//
#pragma once

#include "../main.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

//******************
// ThemeZipCache
//******************
// <themes>/<name>.zip stays a zip. It is listed by name without unpacking (the central directory is read, no
// file is decompressed: a theme.json or theme.ini at the archive's root, or inside its one real folder, makes
// it a theme), and when it is the theme config.ini names, prepare() unpacks it into <themes>/.cache/<name>/ -
// converting an old-layout one inside the cache, so the converter's deletion of theme.ini and colors.ini only
// ever touches the cache. The cache is built in <themes>/.cache/.<name>.unzip and renamed into place, so a
// folder that exists is complete, and what the converter made stays there: the next start finds it and does no
// work. Only one zip theme is ever unpacked: whatever else is in .cache goes first. A folder of the same name
// as a zip wins (the zip is neither listed nor used), and a stick that never picks a zip theme never gets a
// .cache folder (quiet stick).
//
// A zip that is no archive, holds no theme or is refused by the extractor is renamed <name>.zip.bad, as
// ThemeInstaller does. A zip that could not be unpacked for lack of room is left as it is.
class ThemeZipCache {
public:
    // the names (file name without ".zip") of the zips in `themesDir` that hold a theme and have no folder of
    // the same name, sorted. Reads nothing but each archive's directory; writes nothing.
    static std::vector<std::string> listZipThemes(const std::string &themesDir);

    // true when the archive's directory shows a theme (see the class comment)
    static bool holdsTheme(const std::string &zipPath);

    // makes the cache match `picked` (config.ini's theme): every .cache entry that is not `picked`'s - the
    // previous theme, a half-done unpack left by a power cut - is deleted (and .cache itself when that leaves
    // it empty); when `picked` is a zip theme with no folder, its cache is unpacked if it is not there (or
    // the zip's size changed). Returns the cache folder to load, or "" when `picked` is not a zip theme or
    // could not be unpacked.
    static std::string prepare(const std::string &themesDir, const std::string &picked);

    // <themes>/.cache and <themes>/.cache/<name>
    static std::string cacheRoot(const std::string &themesDir);
    static std::string cacheDir(const std::string &themesDir, const std::string &name);

    // the bytes free on the stick under `dir`; tests replace it. Unknown = the largest number.
    using FreeSpaceProbe = std::function<uint64_t(const std::string &dir)>;
    static void setFreeSpaceProbe(FreeSpaceProbe probe); // an empty one restores the real one
};
