//
// The installers - see the header.
//
#include "content_installer.h"
#include "app_manifest.h"
#include "app_settings.h"
#include "system.h"
#include "../main.h"

#include <ableem/engine/seven_zip_archive.h>
#include <ableem/engine/tar_archive.h>
#include <ableem/engine/zip_archive.h>
#include <ableem/engine/log.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

using namespace std;

namespace {

string lowerName(const string &path) {
    return ableem::toLowerCopy(DirEntry::getFileNameFromPath(path));
}

bool endsWith(const string &s, const string &tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

enum class Kind { None, Zip, Tar, SevenZip };

Kind kindOf(const string &path) {
    const string n = lowerName(path);
    if (endsWith(n, ".zip"))
        return Kind::Zip;
    if (endsWith(n, ".tar.gz") || endsWith(n, ".tgz") || endsWith(n, ".tar"))
        return Kind::Tar;
    if (endsWith(n, ".7z"))
        return Kind::SevenZip;
    return Kind::None;
}

// folders an archiver leaves beside the content: macOS resource forks, hidden ones
bool isJunk(const string &name) {
    return name.empty() || name[0] == '.' || name.compare(0, 2, "__") == 0;
}

// an app.ini of the old kind (RetroBoot's Apps): no Exec at all, a Startup= script is the program
bool isOldKindApp(const map<string, string> &values) {
    for (const auto &v : values)
        if (v.first == "exec" || v.first.compare(0, 5, "exec.") == 0)
            return false;
    return true;
}

// every file under dir, recursively, as paths relative to it
void filesUnder(const string &dir, const string &relative, vector<string> &out) {
    for (const DirEntry &e : DirEntry::diru(relative.empty() ? dir : dir + sep + relative)) {
        const string rel = relative.empty() ? e.name : relative + sep + e.name;
        if (e.isDir)
            filesUnder(dir, rel, out);
        else
            out.push_back(rel);
    }
}

// moves `from` over `to` (a file), making its folder
bool moveFile(const string &from, const string &to) {
    DirEntry::createDirs(DirEntry::getDirNameFromPath(to));
    if (DirEntry::exists(to))
        DirEntry::removeFile(to);
    return DirEntry::renameFile(from, to) || (DirEntry::copy(from, to) && DirEntry::removeFile(from));
}

bool enoughSpaceFor(const string &path, uint64_t needed, string &error) {
    uint64_t freeBytes = 0, total = 0;
    if (!System::diskSpace(path, freeBytes, total))
        return true; // cannot be told: let the writing find out
    // a little room over what is needed, so the stick is never filled to the last byte
    const uint64_t margin = 16ull * 1024 * 1024;
    if (freeBytes >= needed + margin)
        return true;
    error = "not enough free space: " + to_string((needed + margin) / (1024 * 1024)) + " MB needed, " +
            to_string(freeBytes / (1024 * 1024)) + " MB free";
    return false;
}

// a staging folder of its own, empty
string freshStaging(const string &stagingDir, const string &name) {
    const string dir = stagingDir + sep + name;
    DirEntry::removeDirAndContents(dir);
    DirEntry::createDirs(dir);
    return dir;
}

} // namespace

//*******************************
// ArchiveUnpacker
//*******************************
bool ArchiveUnpacker::isArchive(const string &path) {
    return kindOf(path) != Kind::None;
}

bool ArchiveUnpacker::unpackedSize(const string &archive, uint64_t &bytes, string &error) {
    bytes = 0;
    switch (kindOf(archive)) {
    case Kind::Zip: {
        vector<ableem::ZipEntry> entries;
        if (!ableem::ZipArchive::listEntries(archive, entries)) {
            error = "not a readable zip: " + archive;
            return false;
        }
        for (const auto &e : entries)
            bytes += e.size;
        return true;
    }
    case Kind::Tar: {
        vector<ableem::TarEntry> entries;
        if (!ableem::TarArchive::list(archive, entries, error))
            return false;
        for (const auto &e : entries)
            bytes += e.size;
        return true;
    }
    case Kind::SevenZip: {
        vector<ableem::SevenZipEntry> entries;
        if (!ableem::SevenZipArchive::list(archive, entries)) {
            error = "not a readable 7z: " + archive;
            return false;
        }
        for (const auto &e : entries)
            bytes += e.size;
        return true;
    }
    case Kind::None:
        break;
    }
    error = "not an archive this system can open: " + archive;
    return false;
}

bool ArchiveUnpacker::unpack(const string &archive, const string &destDir, string &error) {
    DirEntry::createDirs(destDir);
    switch (kindOf(archive)) {
    case Kind::Zip:
        if (!ableem::ZipArchive::extract(archive, destDir)) {
            error = "the zip could not be unpacked: " + archive;
            return false;
        }
        return true;
    case Kind::Tar:
        return ableem::TarArchive::extract(archive, destDir, error);
    case Kind::SevenZip:
        return ableem::SevenZipArchive::extract(archive, destDir, error);
    case Kind::None:
        break;
    }
    error = "not an archive this system can open: " + archive;
    return false;
}

//*******************************
// AppInstaller::findAppRoot
//*******************************
string AppInstaller::findAppRoot(const string &root, const string &fallbackName, string &name) {
    if (DirEntry::exists(root + sep + "app.ini")) {
        name = fallbackName;
        return root;
    }
    // Apps/<name>/ - the layout of our packages - or the archive's one folder
    for (const string &base : {root + sep + "Apps", root}) {
        if (!DirEntry::isDirectory(base))
            continue;
        vector<string> folders;
        for (const DirEntry &e : DirEntry::diru_DirsOnly(base))
            if (!isJunk(e.name) && DirEntry::exists(base + sep + e.name + sep + "app.ini"))
                folders.push_back(e.name);
        if (folders.size() == 1) {
            name = folders[0];
            return base + sep + folders[0];
        }
    }
    return "";
}

//*******************************
// AppInstaller::install
//*******************************
InstallResult AppInstaller::install(const string &archive, const string &appsDir, const string &stagingDir,
                                    const vector<string> &keys) {
    InstallResult r;
    uint64_t size = 0;
    if (!ArchiveUnpacker::unpackedSize(archive, size, r.error) || !enoughSpaceFor(appsDir, size, r.error))
        return r;
    const string staged = freshStaging(stagingDir, "app");
    auto done = [&staged](InstallResult &result) -> InstallResult & {
        DirEntry::removeDirAndContents(staged);
        return result;
    };
    if (!ArchiveUnpacker::unpack(archive, staged, r.error))
        return done(r);

    // the archive's name up to its first "-" names an App that has its app.ini at the root
    string stem = DirEntry::getFileNameFromPath(archive);
    stem = stem.substr(0, stem.find_first_of("-."));
    const string root = findAppRoot(staged, stem, r.name);
    if (root.empty() || r.name.empty()) {
        r.error = "the archive holds no App (no app.ini)";
        return done(r);
    }
    AppManifest incoming = AppManifest::load(root, "app.ini", keys);
    if (!incoming.runnable()) {
        r.error = "the App cannot run on this system: " + incoming.problem;
        return done(r);
    }

    const string dest = appsDir + sep + r.name;
    if (DirEntry::exists(dest + sep + "app.ini")) {
        IniFile existing;
        existing.load(dest + sep + "app.ini");
        const string oldVersion = Strings::trim(existing.values["version"]);
        if (isOldKindApp(existing.values) && !incoming.legacyStartup) {
            // an App of the old kind (the RetroBoot build of the same App) is replaced whole: nothing of it is
            // kept - binaries, scripts, configs, saves - since none of it belongs to the new build (the owner,
            // 2026-09-25: its run.sh and binaries were left beside ours)
            if (!DirEntry::removeDirAndContents(dest)) {
                r.error = "cannot remove the old " + dest;
                return done(r);
            }
            PLOG_INFO << "App " << r.name << ": the old kind (Startup=) replaced whole by " << incoming.value("version");
        } else if (oldVersion != Strings::trim(incoming.value("version"))) {
            // a new version: every platform's binaries go, so no two versions ever mix
            DirEntry::removeDirAndContents(dest + sep + "bin");
            DirEntry::removeDirAndContents(dest + sep + "lib");
            PLOG_INFO << "App " << r.name << ": " << oldVersion << " -> " << incoming.value("version");
        }
    }
    vector<string> files;
    filesUnder(root, "", files);
    for (const string &f : files) {
        // the user's own pad profile and Game settings stay; the package's is taken when there is none
        const string lower = ableem::toLowerCopy(f);
        if ((lower == "pad.ini" || lower == AppSettings::FileName) && DirEntry::exists(dest + sep + f))
            continue;
        if (!moveFile(root + sep + f, dest + sep + f)) {
            r.error = "cannot write " + dest + sep + f;
            return done(r);
        }
    }
    r.ok = true;
    r.path = dest;
    PLOG_INFO << "App " << r.name << " installed from " << archive;
    return done(r);
}

bool AppInstaller::remove(const string &appFolder, string &error) {
    if (!DirEntry::exists(appFolder + sep + "app.ini")) {
        error = appFolder + " is not an App";
        return false;
    }
    if (!DirEntry::removeDirAndContents(appFolder)) {
        error = "cannot remove " + appFolder;
        return false;
    }
    return true;
}

//*******************************
// ModInstaller::install / remove
//*******************************
namespace {
// a PE package is a Debian archive: "!<arch>\n" first
bool looksLikeAr(const string &path) {
    ifstream in(path, ios::binary);
    char magic[8] = {};
    in.read(magic, sizeof(magic));
    return in.gcount() == 8 && string(magic, 8) == "!<arch>\n";
}

// the mods processor's marker for a package: Apps/.pe_state/<file>.ini
string peMarker(const string &appsDir, const string &modName) {
    return appsDir + sep + ".pe_state" + sep + modName + ".ini";
}
} // namespace

InstallResult ModInstaller::install(const string &mod, const string &modsDir, const string &appsDir,
                                    const string &replaces) {
    InstallResult r;
    const string name = DirEntry::getFileNameFromPath(mod);
    if (!endsWith(lowerName(mod), ".mod") || name.empty() || name[0] == '.') {
        r.error = name + " is not a PE package (.mod)";
        return r;
    }
    if (!looksLikeAr(mod)) {
        r.error = name + " is not a PE package (not a Debian archive)";
        return r;
    }
    if (!DirEntry::createDirs(modsDir)) {
        r.error = "cannot make " + modsDir;
        return r;
    }
    const string target = modsDir + sep + name;
    // the same filesystem: one rename, nothing half-written ever shows in Mods/
    if (DirEntry::exists(target))
        DirEntry::removeFile(target);
    if (!DirEntry::renameFile(mod, target)) {
        const string part = target + ".part";
        DirEntry::removeFile(part);
        if (!DirEntry::copyFile(mod, part) || !DirEntry::renameFile(part, target)) {
            DirEntry::removeFile(part);
            r.error = "cannot put " + name + " in " + modsDir;
            return r;
        }
        DirEntry::removeFile(mod);
    }
    // the version being updated: its package goes (the new one makes the App over), the Apps stay
    if (!replaces.empty() && replaces != target && DirEntry::exists(replaces)) {
        DirEntry::removeFile(replaces);
        DirEntry::removeFile(peMarker(appsDir, DirEntry::getFileNameFromPath(replaces)));
    }
    r.ok = true;
    r.path = target;
    r.name = name;
    return r;
}

bool ModInstaller::remove(const string &modFile, const string &appsDir, string &error) {
    const string name = DirEntry::getFileNameFromPath(modFile);
    if (DirEntry::exists(modFile) && !DirEntry::removeFile(modFile)) {
        error = "cannot remove " + modFile;
        return false;
    }
    // the Apps the processor made from it: its own, by the PeSource= it wrote - never an App of anyone else's
    for (const DirEntry &e : DirEntry::diru_DirsOnly(appsDir)) {
        if (e.name.compare(0, 3, "pe-") != 0)
            continue;
        const string folder = appsDir + sep + e.name;
        if (!DirEntry::exists(folder + sep + "app.ini"))
            continue;
        IniFile ini;
        ini.load(folder + sep + "app.ini");
        if (Strings::trim(ini.values["pesource"]) == name && !DirEntry::removeDirAndContents(folder)) {
            error = "cannot remove " + folder;
            return false;
        }
    }
    DirEntry::removeFile(peMarker(appsDir, name));
    return true;
}

//*******************************
// GameInstaller::folderNameFor / cueFor
//*******************************
string GameInstaller::folderNameFor(const string &title) {
    string out;
    for (char c : title) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || string("<>:\"/\\|?*").find(c) != string::npos)
            out += (c == ':' ? " -" : "");
        else
            out += c;
    }
    out = Strings::trim(out);
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    return out.empty() ? "Game" : out;
}

string GameInstaller::cueFor(const string &binName) {
    return "FILE \"" + binName + "\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
}

//*******************************
// GameInstaller::install
//*******************************
InstallResult GameInstaller::install(const vector<string> &files, const string &title, const string &gamesDir,
                                     const string &stagingDir) {
    InstallResult r;
    if (files.empty()) {
        r.error = "nothing was downloaded";
        return r;
    }
    uint64_t needed = 0;
    for (const string &f : files) {
        uint64_t size = 0;
        if (ArchiveUnpacker::isArchive(f)) {
            if (!ArchiveUnpacker::unpackedSize(f, size, r.error))
                return r;
            needed += size;
        }
    }
    if (!enoughSpaceFor(gamesDir, needed, r.error))
        return r;

    const string staged = freshStaging(stagingDir, "game");
    auto done = [&staged](InstallResult &result) -> InstallResult & {
        DirEntry::removeDirAndContents(staged);
        return result;
    };
    for (size_t i = 0; i < files.size(); i++) {
        const string &f = files[i];
        if (ArchiveUnpacker::isArchive(f)) {
            // each archive in a folder of its own, so two discs' same-named files cannot meet here
            if (!ArchiveUnpacker::unpack(f, staged + sep + to_string(i), r.error))
                return done(r);
        } else if (!moveFile(f, staged + sep + to_string(i) + sep + DirEntry::getFileNameFromPath(f))) {
            r.error = "cannot move " + f;
            return done(r);
        }
    }

    // the disc files, wherever they are in what arrived
    static const set<string> discExtensions{"chd", "pbp", "cue", "bin", "img", "iso", "ecm", "sbi", "m3u", "pkg"};
    vector<string> found;
    filesUnder(staged, "", found);
    vector<string> discs;
    set<string> cueTexts;
    bool anyImage = false;
    for (const string &f : found) {
        const string ext = ableem::toLowerCopy(DirEntry::getFileExtension(f));
        if (discExtensions.count(ext) == 0 || isJunk(DirEntry::getFileNameFromPath(f)))
            continue;
        discs.push_back(f);
        if (ext != "sbi" && ext != "m3u")
            anyImage = true;
        if (ext == "cue") {
            ifstream in(staged + sep + f);
            cueTexts.insert(string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>()));
        }
    }
    if (!anyImage) {
        r.error = "no PlayStation disc image in what was downloaded";
        return done(r);
    }

    // the folder: the title made safe, " (2)" and on when it is taken
    const string base = folderNameFor(title);
    string dest = gamesDir + sep + base;
    for (int n = 2; DirEntry::exists(dest); n++)
        dest = gamesDir + sep + base + " (" + to_string(n) + ")";
    for (const string &f : discs) {
        const string file = DirEntry::getFileNameFromPath(f);
        if (!moveFile(staged + sep + f, dest + sep + file)) {
            r.error = "cannot write " + dest + sep + file;
            DirEntry::removeDirAndContents(dest);
            return done(r);
        }
        // a .bin no .cue names gets one of its own (a bare-.bin source)
        if (ableem::toLowerCopy(DirEntry::getFileExtension(file)) == "bin") {
            bool named = false;
            for (const string &cue : cueTexts)
                named = named || cue.find(file) != string::npos;
            if (!named) {
                ofstream cue(dest + sep + DirEntry::getFileNameWithoutExtension(file) + ".cue", ios::binary);
                cue << cueFor(file);
            }
        }
    }
    r.ok = true;
    r.path = dest;
    r.name = DirEntry::getFileNameFromPath(dest);
    PLOG_INFO << "Game " << title << " installed to " << dest;
    return done(r);
}

bool GameInstaller::remove(const string &gameFolder, string &error) {
    if (!DirEntry::removeDirAndContents(gameFolder)) {
        error = "cannot remove " + gameFolder;
        return false;
    }
    return true;
}
