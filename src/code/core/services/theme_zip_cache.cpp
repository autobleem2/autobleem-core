//
// ThemeZipCache: <themes>/<name>.zip -> <themes>/.cache/<name>/, only for the picked theme
//

#include "theme_zip_cache.h"
#include "theme_converter.h"
#include "theme_installer.h"

#include <algorithm>
#include <limits>
#include <set>

#include <ableem/engine/log.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/statvfs.h>
#endif

using namespace std;

namespace {

const char *CACHE_DIR = ".cache";
const char *SOURCE_STAMP = ".source";             // in the cached theme: the size of the zip it was unpacked from
const uint64_t SPACE_MARGIN = 4ull * 1024 * 1024; // what the converter and the file system need on top

ThemeZipCache::FreeSpaceProbe &probe() {
    static ThemeZipCache::FreeSpaceProbe p;
    return p;
}

uint64_t realFreeSpace(const string &dir) {
#ifdef _WIN32
    ULARGE_INTEGER available;
    return GetDiskFreeSpaceExA(dir.c_str(), &available, nullptr, nullptr) ? available.QuadPart
                                                                          : numeric_limits<uint64_t>::max();
#else
    struct statvfs st;
    return statvfs(dir.c_str(), &st) == 0 ? static_cast<uint64_t>(st.f_bavail) * st.f_frsize
                                          : numeric_limits<uint64_t>::max();
#endif
}

uint64_t freeSpace(const string &dir) {
    return probe() ? probe()(dir) : realFreeSpace(dir);
}

// a name config.ini can hold that is a plain file name: no path, nothing hidden
bool isPlainName(const string &name) {
    return !name.empty() && name[0] != '.' && name.find_first_of("/\\:") == string::npos;
}

string zipPathOf(const string &themesDir, const string &name) {
    return themesDir + sep + name + ".zip";
}

bool isJunkFolder(const string &name) {
    return name.empty() || name[0] == '.' || name.compare(0, 2, "__") == 0;
}

bool isThemeFile(const string &name) {
    return name == "theme.json" || name == "theme.ini";
}

// the archive's names say it holds a theme the way ThemeInstaller::findThemeRoot() would find one
bool namesHoldTheme(const vector<string> &names) {
    set<string> folders; // the real top-level folders
    for (const string &name : names) {
        if (isThemeFile(name))
            return true;
        const size_t slash = name.find('/');
        if (slash == string::npos)
            continue;
        const string top = name.substr(0, slash);
        if (!isJunkFolder(top))
            folders.insert(top);
    }
    if (folders.size() != 1)
        return false; // none, or two candidates: not a theme zip we understand
    const string &only = *folders.begin();
    return find_if(names.begin(), names.end(), [&](const string &n) {
               return n.compare(0, only.size() + 1, only + "/") == 0 && isThemeFile(n.substr(only.size() + 1));
           }) != names.end();
}

// the zip as it is told to be bad: renamed, so it is not tried on every start
bool giveUp(const string &name, const string &zip, const string &staging, const string &why) {
    PLOG_INFO << "Theme zip " << name << " not usable: " << why;
    DirEntry::removeDirAndContents(staging);
    DirEntry::renameFile(zip, zip + ".bad");
    return false;
}

// the cache for `name` is there and complete: true; unpacked and converted now: true; else false
bool ensureCache(const string &themesDir, const string &name) {
    const string zip = zipPathOf(themesDir, name);
    const string cache = ThemeZipCache::cacheDir(themesDir, name);
    const string staging = ThemeZipCache::cacheRoot(themesDir) + sep + "." + name + ".unzip";
    const string zipSize = to_string(DirEntry::fileSize(zip));

    if (DirEntry::isDirectory(cache)) {
        string stamp;
        DirEntry::readFile(cache + sep + SOURCE_STAMP, stamp);
        if (stamp == zipSize)
            return true; // unpacked and converted by an earlier run: nothing to do
        PLOG_INFO << "Theme zip " << name << " changed since it was unpacked";
        DirEntry::removeDirAndContents(cache);
    }

    vector<ableem::ZipEntry> entries;
    if (!ableem::ZipArchive::listEntries(zip, entries))
        return giveUp(name, zip, staging, "not a zip archive");
    vector<string> names;
    uint64_t needed = 0;
    for (const ableem::ZipEntry &entry : entries) {
        names.push_back(entry.name);
        needed += entry.size;
    }
    if (!namesHoldTheme(names))
        return giveUp(name, zip, staging, "no theme.json or theme.ini in it");

    const string root = ThemeZipCache::cacheRoot(themesDir);
    if (!DirEntry::createDirs(root))
        return false;
    const uint64_t room = freeSpace(root);
    if (room < needed + SPACE_MARGIN) {
        PLOG_WARNING << "Theme zip " << name << " needs " << needed << " bytes, " << room << " are free";
        return false; // the zip is fine, the stick is full
    }

    DirEntry::removeDirAndContents(staging); // a previous run that did not get to the end
    if (!ableem::ZipArchive::extract(zip, staging)) {
        if (freeSpace(root) < SPACE_MARGIN) { // ran out of room on the way: not the zip's fault
            DirEntry::removeDirAndContents(staging);
            return false;
        }
        return giveUp(name, zip, staging, "could not unpack it");
    }

    const string themeRoot = ThemeInstaller::findThemeRoot(staging);
    if (themeRoot.empty())
        return giveUp(name, zip, staging, "no theme.json or theme.ini in it");

    // the conversion happens here, in the staging folder: the cache is complete or it is not there
    if (ThemeConverter::needsConversion(themeRoot) && !ThemeConverter::convert(themeRoot)) {
        DirEntry::removeDirAndContents(staging);
        return false;
    }
    if (DirEntry::writeFileIfChanged(themeRoot + sep + SOURCE_STAMP, zipSize) == DirEntry::WriteResult::Failed) {
        DirEntry::removeDirAndContents(staging);
        return false;
    }

    if (!DirEntry::renameFile(themeRoot, cache)) {
        DirEntry::removeDirAndContents(staging);
        return false;
    }
    if (themeRoot != staging)
        DirEntry::removeDirAndContents(staging);
    PLOG_INFO << "Theme zip " << name << " unpacked into " << cache;
    return true;
}

// everything in .cache except `keep`; .cache itself goes when nothing is left
void cleanCache(const string &themesDir, const string &keep) {
    const string root = ThemeZipCache::cacheRoot(themesDir);
    if (!DirEntry::isDirectory(root))
        return;
    bool left = false;
    for (const DirEntry &entry : DirEntry::dir(root)) { // dir(), not diru(): the leftovers are dot folders
        if (entry.name == "." || entry.name == "..")
            continue;
        if (!keep.empty() && entry.name == keep) {
            left = true;
            continue;
        }
        PLOG_INFO << "Removing cached theme " << entry.name;
        if (entry.isDir)
            DirEntry::removeDirAndContents(root + sep + entry.name);
        else
            DirEntry::removeFile(root + sep + entry.name);
    }
    if (!left)
        DirEntry::removeDirAndContents(root);
}

} // namespace

//*******************************
// ThemeZipCache::cacheRoot / cacheDir
//*******************************
string ThemeZipCache::cacheRoot(const string &themesDir) {
    return themesDir + sep + CACHE_DIR;
}

string ThemeZipCache::cacheDir(const string &themesDir, const string &name) {
    return cacheRoot(themesDir) + sep + name;
}

//*******************************
// ThemeZipCache::setFreeSpaceProbe
//*******************************
void ThemeZipCache::setFreeSpaceProbe(FreeSpaceProbe p) {
    probe() = std::move(p);
}

//*******************************
// ThemeZipCache::holdsTheme
//*******************************
bool ThemeZipCache::holdsTheme(const string &zipPath) {
    vector<string> names;
    return ableem::ZipArchive::list(zipPath, names) && namesHoldTheme(names);
}

//*******************************
// ThemeZipCache::listZipThemes
//*******************************
vector<string> ThemeZipCache::listZipThemes(const string &themesDir) {
    vector<string> themes;
    for (const DirEntry &entry : DirEntry::diru_FilesOnly(themesDir)) {
        if (!DirEntry::matchExtension(entry.name, "zip"))
            continue;
        const string name = ThemeInstaller::themeName(entry.name);
        if (!isPlainName(name) || DirEntry::isDirectory(themesDir + sep + name))
            continue; // a folder of the same name wins
        if (holdsTheme(themesDir + sep + entry.name))
            themes.push_back(name);
    }
    sort(themes.begin(), themes.end());
    return themes;
}

//*******************************
// ThemeZipCache::prepare
//*******************************
string ThemeZipCache::prepare(const string &themesDir, const string &picked) {
    const bool zipTheme = isPlainName(picked) && !DirEntry::isDirectory(themesDir + sep + picked) &&
                          DirEntry::fileSize(zipPathOf(themesDir, picked)) >= 0;
    cleanCache(themesDir, zipTheme ? picked : "");
    if (!zipTheme || !ensureCache(themesDir, picked))
        return "";
    return cacheDir(themesDir, picked);
}
