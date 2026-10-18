#include "installer/legacy_layout.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/log.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
using ableem::DirEntry;

namespace {

#ifdef _WIN32
// no lstat/S_ISLNK in mingw: a symlink, a junction or any other reparse point counts as a link, and is never entered
DWORD winAttributes(const string &path) {
    return GetFileAttributesA(path.c_str());
}

bool isSymlink(const string &path) {
    const DWORD a = winAttributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool isRealDir(const string &path) {
    const DWORD a = winAttributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0 && (a & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

// the link itself only (a directory link is removed as a directory, which does not touch its target)
bool isRealFile(const string &path) {
    const DWORD a = winAttributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
}

bool unlinkLink(const string &path) {
    return (winAttributes(path) & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryA(path.c_str()) != 0
                                                             : DeleteFileA(path.c_str()) != 0;
}

bool gone(const string &path) {
    return winAttributes(path) == INVALID_FILE_ATTRIBUTES;
}
#else
bool isSymlink(const string &path) {
    struct stat st;
    return lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}

bool isRealDir(const string &path) {
    struct stat st;
    return lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isRealFile(const string &path) {
    struct stat st;
    return lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool unlinkLink(const string &path) {
    return unlink(path.c_str()) == 0;
}

bool gone(const string &path) {
    struct stat st;
    return lstat(path.c_str(), &st) != 0;
}
#endif

// removes `path` and what is in it without ever following a symlink: a link is unlinked, never entered (the
// global DirEntry::rmDir stat()s and would walk into the link's target). Dotfiles included. Failures are left
// in place; the first folder or file that could not go is in `failed`. True when `path` is gone.
bool removeTree(const string &path, string &failed) {
    if (isRealDir(path)) {
        if (DIR *d = opendir(path.c_str())) {
            vector<string> names;
            while (struct dirent *p = readdir(d))
                if (strcmp(p->d_name, ".") != 0 && strcmp(p->d_name, "..") != 0)
                    names.push_back(p->d_name);
            closedir(d);
            for (const string &name : names)
                removeTree(path + "/" + name, failed);
        }
        if (rmdir(path.c_str()) != 0 && failed.empty())
            failed = path;
    } else if (!unlinkLink(path) && failed.empty()) {
        failed = path;
    }
    return gone(path);
}

string readText(const string &path) {
    ifstream in(path, ios::binary);
    if (!in)
        return "";
    stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// the script scan of removeRetroBoot reads every regular file of a folder (any name, links never followed) up to this size
const long long kScanLimit = 1024 * 1024;

// true when a file of `dir` mentions retroboot/ ; sets `hit` to its path. Binaries (a NUL in the first 8 KiB) and
// files over kScanLimit are skipped, the latter with a log line.
bool folderMentionsRetroBoot(const string &dir, string &hit, const function<void(const string &)> &say) {
    DIR *d = opendir(dir.c_str());
    if (!d)
        return false;
    vector<string> names;
    while (struct dirent *p = readdir(d))
        if (strcmp(p->d_name, ".") != 0 && strcmp(p->d_name, "..") != 0)
            names.push_back(p->d_name);
    closedir(d);
    for (const string &name : names) {
        const string path = dir + "/" + name;
        if (!isRealFile(path))
            continue;
        ifstream in(path, ios::binary);
        if (!in)
            continue;
        in.seekg(0, ios::end);
        const long long size = static_cast<long long>(in.tellg());
        in.seekg(0, ios::beg);
        if (size > kScanLimit) {
            say("  not scanned (over 1 MiB): " + path);
            continue;
        }
        stringstream ss;
        ss << in.rdbuf();
        const string text = ss.str();
        if (text.substr(0, 8192).find('\0') != string::npos)
            continue;
        string low = text;
        transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        if (low.find("retroboot/") != string::npos) {
            hit = path;
            return true;
        }
    }
    return false;
}

bool writeText(const string &path, const string &text) {
    ofstream out(path, ios::binary | ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

void replaceAll(string &s, const string &from, const string &to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool endsWith(const string &s, const string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// the entry of `dir` named `name`, whatever its case (FAT does not care, and a stick from a Linux box
// may have it either way); "" when there is none
string findCi(const string &dir, const string &name) {
    for (const DirEntry &e : DirEntry::diru(dir)) {
        if (e.name.size() != name.size())
            continue;
        bool same = true;
        for (size_t i = 0; i < name.size() && same; i++)
            same = tolower(static_cast<unsigned char>(e.name[i])) == tolower(static_cast<unsigned char>(name[i]));
        if (same)
            return dir + "/" + e.name;
    }
    return "";
}

// moves src's contents into dst (made if missing) and removes src; a file already in dst is left when
// keepExisting, replaced otherwise
bool moveMerge(const string &src, const string &dst, bool keepExisting, string &error) {
    if (!DirEntry::createDirs(dst)) {
        error = "cannot create " + dst;
        return false;
    }
    for (const DirEntry &e : DirEntry::diru(src)) {
        const string from = src + "/" + e.name, to = dst + "/" + e.name;
        if (DirEntry::isDirectory(from) && DirEntry::isDirectory(to)) {
            if (!moveMerge(from, to, keepExisting, error))
                return false;
        } else if (DirEntry::exists(to)) {
            if (keepExisting) {
                if (DirEntry::isDirectory(from))
                    DirEntry::removeDirAndContents(from);
                else
                    DirEntry::removeFile(from);
            } else {
                if (DirEntry::isDirectory(to))
                    DirEntry::removeDirAndContents(to);
                else
                    DirEntry::removeFile(to);
                if (!DirEntry::renameFile(from, to)) {
                    error = "cannot move " + from;
                    return false;
                }
            }
        } else if (!DirEntry::renameFile(from, to)) {
            error = "cannot move " + from;
            return false;
        }
    }
    DirEntry::rmDir(src);
    return true;
}

// removes `dir` when nothing is in it, dotfiles counted (rmdir itself refuses a folder with anything inside)
bool removeIfEmpty(const string &dir) {
    return !isSymlink(dir) && rmdir(dir.c_str()) == 0;
}

// copies every file of src into dst that is not there yet
void copyMissing(const string &src, const string &dst) {
    if (!DirEntry::isDirectory(src))
        return;
    DirEntry::createDirs(dst);
    for (const DirEntry &e : DirEntry::diru_FilesOnly(src))
        if (!DirEntry::exists(dst + "/" + e.name))
            DirEntry::copyFile(src + "/" + e.name, dst + "/" + e.name);
}

// a text file with the rewrite applied, when it changes anything
bool rewriteFile(const string &path, const function<string(const string &)> &rewrite, const LegacyLayout::Say &say) {
    string text = readText(path);
    if (text.empty())
        return false;
    string changed = rewrite(text);
    if (changed == text)
        return false;
    writeText(path, changed);
    say("  rewrote " + path);
    return true;
}

// the ES-style folder names AutoBleem 1.0 / RetroBoot sticks carry, and the RetroArch database each one is -
// the same table as tools/install_autobleem.py's ROMS_LAYOUT_MAP (psx, atari800, the *h hack folders and
// videos have no confident database and are left as they are)
struct RomFolder {
    const char *old;
    const char *database;
};
const RomFolder RomFolders[] = {
    {"3do", "The 3DO Company - 3DO"},
    {"a2600", "Atari - 2600"},
    {"a5200", "Atari - 5200"},
    {"a7800", "Atari - 7800"},
    {"amiga", "Commodore - Amiga"},
    {"amstradcpc", "Amstrad - CPC"},
    {"arcade", "FBNeo - Arcade Games"},
    {"atarijaguar", "Atari - Jaguar"},
    {"atarilynx", "Atari - Lynx"},
    {"atarist", "Atari - ST"},
    {"c64", "Commodore - 64"},
    {"colecovision", "Coleco - ColecoVision"},
    {"daphne", "Daphne"},
    {"dosbox", "DOS"},
    {"dreamcast", "Sega - Dreamcast"},
    {"famicom", "Nintendo - Nintendo Entertainment System"},
    {"fba2012", "FBNeo - Arcade Games"},
    {"fds", "Nintendo - Family Computer Disk System"},
    {"gameandwatch", "Handheld Electronic Game"},
    {"gamegear", "Sega - Game Gear"},
    {"gb", "Nintendo - Game Boy"},
    {"gba", "Nintendo - Game Boy Advance"},
    {"gbc", "Nintendo - Game Boy Color"},
    {"genesis", "Sega - Mega Drive - Genesis"},
    {"intellivision", "Mattel - Intellivision"},
    {"mame", "MAME"},
    {"mastersystem", "Sega - Master System - Mark III"},
    {"megadrive", "Sega - Mega Drive - Genesis"},
    {"msx", "Microsoft - MSX"},
    {"n64", "Nintendo - Nintendo 64"},
    {"naomi", "Sega - NAOMI"},
    {"nds", "Nintendo - Nintendo DS"},
    {"neogeo", "FBNeo - Arcade Games"},
    {"neogeocd", "SNK - Neo Geo CD"},
    {"nes", "Nintendo - Nintendo Entertainment System"},
    {"ngp", "SNK - Neo Geo Pocket"},
    {"ngpc", "SNK - Neo Geo Pocket Color"},
    {"odyssey2", "Magnavox - Odyssey2"},
    {"pcengine", "NEC - PC Engine - TurboGrafx 16"},
    {"pcenginecd", "NEC - PC Engine CD - TurboGrafx-CD"},
    {"psp", "Sony - PlayStation Portable"},
    {"saturn", "Sega - Saturn"},
    {"scummvm", "ScummVM"},
    {"sega32x", "Sega - 32X"},
    {"segacd", "Sega - Mega-CD - Sega CD"},
    {"sfc", "Nintendo - Super Nintendo Entertainment System"},
    {"sg1000", "Sega - SG-1000"},
    {"snes", "Nintendo - Super Nintendo Entertainment System"},
    {"snesh", "Nintendo - Super Nintendo Entertainment System Hacks"},
    {"supergrafx", "NEC - PC Engine SuperGrafx"},
    {"tg-cd", "NEC - PC Engine CD - TurboGrafx-CD"},
    {"tg16", "NEC - PC Engine - TurboGrafx 16"},
    {"virtualboy", "Nintendo - Virtual Boy"},
    {"wonderswan", "Bandai - WonderSwan"},
    {"zxspectrum", "Sinclair - ZX Spectrum +3"},
};

string lower(string s) {
    for (char &c : s)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// every line "<key> = ..." (any spacing, a trailing \r kept) becomes `line` - what a regex with
// std::regex::multiline did, which the console's gcc-6 does not have (the console's own updater runs this)
string replaceKeyLines(const string &text, const string &key, const string &line) {
    string out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        string current = text.substr(start, end == string::npos ? string::npos : end - start);
        const bool cr = !current.empty() && current.back() == '\r';
        size_t i = key.size();
        if (current.compare(0, key.size(), key) == 0) {
            while (i < current.size() && (current[i] == ' ' || current[i] == '\t'))
                i++;
            if (i < current.size() && current[i] == '=')
                current = line + (cr ? "\r" : "");
        }
        out += current;
        if (end == string::npos)
            break;
        out += '\n';
        start = end + 1;
    }
    return out;
}

} // namespace

//*******************************
// LegacyLayout::convertRomFolders
//*******************************
int LegacyLayout::convertRomFolders(const string &romsDir, const Say &say) {
    if (!DirEntry::isDirectory(romsDir))
        return 0;
    int converted = 0;
    vector<string> claimed; // databases an earlier entry of this pass renamed a folder onto
    for (const RomFolder &f : RomFolders) {
        const string src = findCi(romsDir, f.old);
        const string target = romsDir + "/" + f.database;
        if (src.empty() || !DirEntry::isDirectory(src) || src == target)
            continue;
        const string existing = findCi(romsDir, f.database);
        const bool taken = find(claimed.begin(), claimed.end(), lower(f.database)) != claimed.end();
        if (!existing.empty() && existing == src) {
            // the same name in another case (daphne -> Daphne): on FAT that needs a stop on the way
            const string tmp = src + ".ab_rename_tmp";
            if (DirEntry::renameFile(src, tmp) && DirEntry::renameFile(tmp, target)) {
                claimed.push_back(lower(f.database));
                converted++;
            }
        } else if (existing.empty() && !taken) {
            if (DirEntry::renameFile(src, target)) {
                claimed.push_back(lower(f.database));
                converted++;
            }
        } else {
            // both there (famicom and nes, say): merge, the files already in the database's folder win
            string error;
            if (moveMerge(src, existing.empty() ? target : existing, true, error))
                converted++;
            else
                say("  " + error);
        }
        say("  roms/" + src.substr(romsDir.size() + 1) + " -> roms/" + f.database);
    }
    return converted;
}

//*******************************
// LegacyLayout::rewritePaths
//*******************************
string LegacyLayout::rewritePaths(const string &input) {
    string s = input;
    // the longer first, so the shorter never catch a longer's tail
    static const struct {
        const char *from, *to;
    } Rewrites[] = {
        {"/media/retroarch/retroboot/assets/lib", "/media/Autobleem/lib/apps"},
        {"/media/retroarch/apps", "/media/Apps"},
        {"/media/RetroArch/bin/apps", "/media/Apps"},
        {"/media/retroarch/logs/", "/media/System/Logs/"},
        {"/media/retroarch/system", "/media/RetroArch/bios"},
        {"/media/retroarch/", "/media/RetroArch/bin/"},
        {"/media/retroarch\"", "/media/RetroArch/bin\""},
        {"/media/roms/", "/media/RetroArch/roms/"},
        {"/media/roms\"", "/media/RetroArch/roms\""},
        {"${RB_LIBRARY_PATH}", "/tmp/applib"},
        {"$RB_LIBRARY_PATH", "/tmp/applib"},
    };
    for (const auto &r : Rewrites)
        replaceAll(s, r.from, r.to);
    return s;
}

//*******************************
// LegacyLayout::rewriteRunScript
//*******************************
string LegacyLayout::rewriteRunScript(const string &input) {
    // the block: if [ -z "${RB_LIBRARY_PATH}" ]; then ... fi  if [ ! -d "${RB_LIBRARY_PATH}" ]; then ... fi
    static const regex block(R"rx(if \[ -z "(\$\{RB_LIBRARY_PATH\}|/tmp/applib)" \]; then\n[^\n]*\nfi\n)rx"
                             R"rx(if \[ ! -d "(\$\{RB_LIBRARY_PATH\}|/tmp/applib)" \]; then\n[^\n]*\nfi\n)rx");
    string s = regex_replace(input, block, "");
    const string envLine = ". /media/Autobleem/rc/app_env.sh";
    if (s.find(envLine) == string::npos) {
        // after the shebang and the comment lines at the top
        vector<string> lines;
        stringstream in(s);
        string line;
        while (getline(in, line))
            lines.push_back(line);
        size_t at = 0;
        if (!lines.empty() && lines[0].rfind("#!", 0) == 0)
            at = 1;
        while (at < lines.size() && !lines[at].empty() && lines[at][0] == '#')
            at++;
        lines.insert(lines.begin() + static_cast<long>(at), envLine);
        s.clear();
        for (const string &l : lines)
            s += l + "\n";
    }
    return rewritePaths(s);
}

//*******************************
// LegacyLayout::detect
//*******************************
bool LegacyLayout::detect(const string &root) {
    const string ra = findCi(root, "retroarch");
    if (!ra.empty() && !DirEntry::isDirectory(ra + "/bin") &&
        (DirEntry::exists(ra + "/retroarch.cfg") || DirEntry::isDirectory(ra + "/cores")))
        return true;
    const string roms = findCi(root, "roms");
    return !roms.empty() && DirEntry::isDirectory(roms);
}

//*******************************
// LegacyLayout::migrate
//*******************************
bool LegacyLayout::migrate(const string &root, const Say &say, string &error) {
    // Themes/
    const string themes = findCi(root, "themes");
    if (!themes.empty() && themes.substr(root.size() + 1) != "Themes") {
        const string tmp = root + "/Themes.renaming";
        if (DirEntry::renameFile(themes, tmp) && DirEntry::renameFile(tmp, root + "/Themes"))
            say("  themes -> Themes");
    }
    // RetroArch/bin
    string ra = findCi(root, "retroarch");
    if (!ra.empty() && !DirEntry::isDirectory(ra + "/bin") &&
        (DirEntry::exists(ra + "/retroarch.cfg") || DirEntry::isDirectory(ra + "/cores"))) {
        const string tmp = root + "/RetroArch.migrating";
        if (!DirEntry::renameFile(ra, tmp) || !DirEntry::createDirs(root + "/RetroArch") ||
            !DirEntry::renameFile(tmp, root + "/RetroArch/bin")) {
            error = "cannot move " + ra + " to RetroArch/bin";
            return false;
        }
        say("  retroarch -> RetroArch/bin");
        ra = root + "/RetroArch";
    } else if (!ra.empty() && ra.substr(root.size() + 1) != "RetroArch") {
        const string tmp = root + "/RetroArch.renaming";
        if (DirEntry::renameFile(ra, tmp) && DirEntry::renameFile(tmp, root + "/RetroArch"))
            ra = root + "/RetroArch";
    }
    if (ra.empty())
        ra = root + "/RetroArch";
    const string bin = ra + "/bin";
    // RetroArch/bios
    const string system = DirEntry::isDirectory(bin) ? findCi(bin, "system") : "";
    if (!system.empty() && DirEntry::isDirectory(system)) {
        if (!moveMerge(system, ra + "/bios", false, error))
            return false;
        say("  retroarch/system -> RetroArch/bios");
    }
    // RetroArch/roms
    const string roms = findCi(root, "roms");
    if (!roms.empty() && DirEntry::isDirectory(roms)) {
        if (!moveMerge(roms, ra + "/roms", false, error))
            return false;
        say("  roms -> RetroArch/roms");
    }
    if (!DirEntry::isDirectory(bin))
        return true;

    // retroarch.cfg: the paths, and the two keys that must name the new folders
    rewriteFile(
        bin + "/retroarch.cfg",
        [](const string &text) {
            string s = rewritePaths(text);
            s = replaceKeyLines(s, "system_directory", "system_directory = \"/media/RetroArch/bios\"");
            s = replaceKeyLines(s, "rgui_browser_directory", "rgui_browser_directory = \"/media/RetroArch/roms/\"");
            return s;
        },
        say);
    // the playlists
    vector<string> playlists;
    for (const DirEntry &e : DirEntry::diru_FilesOnly(bin + "/playlists"))
        if (endsWith(e.name, ".lpl"))
            playlists.push_back(bin + "/playlists/" + e.name);
    for (const DirEntry &e : DirEntry::diru_FilesOnly(bin))
        if (e.name.rfind("content_", 0) == 0 && endsWith(e.name, ".lpl"))
            playlists.push_back(bin + "/" + e.name);
    // RetroBoot's own playlists cannot be carried over: Applications.lpl names its launchers, "Sony -
    // PlayStation.lpl" the console's internal games through links its init.sh made under /tmp, and
    // AutoBleem.lpl (the PS1 library) is written afresh by the launcher's next scan - which also rebuilds
    // every per-system playlist from RetroArch/roms/<system>/ (an old stick has no roms.fingerprint, so
    // the scan runs at the first boot). The rewritten paths below are what lets the scan keep RetroArch's
    // own labels and CRCs for the ROMs that are still there.
    for (const string &lpl : playlists) {
        const string name = lpl.substr(lpl.find_last_of('/') + 1);
        if (name == "Applications.lpl" || name == "Sony - PlayStation.lpl" || name == "AutoBleem.lpl") {
            DirEntry::removeFile(lpl);
            say("  removed " + name + " (rebuilt by the launcher's scan)");
            continue;
        }
        rewriteFile(lpl, rewritePaths, say);
    }
    // the libraries and the module RetroBoot carried
    const string rb = bin + "/retroboot";
    const string lib = root + "/Autobleem/lib";
    if (DirEntry::isDirectory(rb)) {
        copyMissing(rb + "/assets/lib", lib + "/apps");
        copyMissing(rb + "/lib", lib + "/retroarch");
        copyMissing(rb + "/modules", lib + "/modules");
        say("  RetroBoot's libraries -> Autobleem/lib");
    }
    // the apps
    const string apps = findCi(root, "Apps");
    const string rbApps = findCi(bin, "apps");
    if (!apps.empty()) {
        for (const DirEntry &app : DirEntry::diru_DirsOnly(apps)) {
            const string dir = apps + "/" + app.name;
            string all;
            for (const DirEntry &f : DirEntry::diru_FilesOnly(dir))
                if (endsWith(f.name, ".sh"))
                    all += readText(dir + "/" + f.name);
            const bool retroBootEra =
                all.find("/media/retroarch/") != string::npos || all.find("RB_LIBRARY_PATH") != string::npos ||
                all.find("retroboot/bin/") != string::npos || all.find("/media/RetroArch/bin/apps") != string::npos;
            if (!retroBootEra)
                continue;
            if (all.find("retroboot/bin/launch_rfa") != string::npos) {
                DirEntry::removeDirAndContents(dir);
                say("  removed Apps/" + app.name +
                    " (RetroBoot's own menu - the system menu's RetroArch item is that)");
                continue;
            }
            if (!rbApps.empty()) {
                const string src = findCi(rbApps, app.name);
                if (!src.empty() && DirEntry::isDirectory(src)) {
                    if (!moveMerge(src, dir, true, error))
                        return false;
                    say("  retroarch/apps/" + app.name + " -> Apps/" + app.name);
                }
            }
            for (const DirEntry &f : DirEntry::diru_FilesOnly(dir))
                if (endsWith(f.name, ".sh"))
                    rewriteFile(dir + "/" + f.name, f.name == "run.sh" ? rewriteRunScript : rewritePaths, say);
        }
    }
    return true;
}

//*******************************
// LegacyLayout::removeRetroBoot
//*******************************
bool LegacyLayout::removeRetroBoot(const string &root, const Say &say) {
    const string ra = findCi(root, "retroarch");
    const string bin = ra.empty() ? "" : findCi(ra, "bin");
    if (bin.empty())
        return true;
    const string rb = findCi(bin, "retroboot");
    const string rbApps = findCi(bin, "apps");
    if (!rb.empty() && isSymlink(rb)) {
        say("  RetroBoot's tree kept: RetroArch/bin/retroboot is a link, not a folder");
        return false;
    }
    const bool hasTree = !rb.empty() && isRealDir(rb);
    if (hasTree) {
        // every file migrate() copies must be on the stick where the launcher looks
        const string lib = root + "/Autobleem/lib";
        const struct {
            string from, to;
        } copies[] = {{rb + "/assets/lib", lib + "/apps"}, {rb + "/lib", lib + "/retroarch"}, {rb + "/modules", lib + "/modules"}};
        for (const auto &c : copies) {
            for (const DirEntry &f : DirEntry::diru_FilesOnly(c.from)) {
                const string there = c.to + "/" + f.name;
                if (!DirEntry::exists(there) || DirEntry::fileSize(there) != DirEntry::fileSize(c.from + "/" + f.name)) {
                    say("  RetroBoot's tree kept: " + f.name + " is not in " + c.to.substr(root.size() + 1) + " yet");
                    return false;
                }
            }
        }
        // a script of the stick that still starts something under retroboot/ would break
        // (every regular file counts, whatever its name)
        vector<string> folders;
        for (const DirEntry &app : DirEntry::diru_DirsOnly(root + "/Apps"))
            if (!isSymlink(root + "/Apps/" + app.name))
                folders.push_back(root + "/Apps/" + app.name);
        folders.push_back(root + "/Autobleem/rc");
        for (const string &folder : folders) {
            string hit;
            if (folderMentionsRetroBoot(folder, hit, say)) {
                say("  RetroBoot's tree kept: " + hit.substr(root.size() + 1) + " still starts something in it");
                return false;
            }
        }
        string failed;
        if (!removeTree(rb, failed)) {
            say("  RetroBoot's tree could not be removed completely: " + failed.substr(root.size() + 1) +
                " stays (the rest may be gone)");
            return false;
        }
        say("  removed RetroArch/bin/retroboot (RetroBoot's own tree)");
    }
    // the apps folder: only the folders with nothing in them - an app that is not in Apps/ keeps its files
    if (!rbApps.empty() && isRealDir(rbApps)) {
        for (const DirEntry &d : DirEntry::diru_DirsOnly(rbApps))
            removeIfEmpty(rbApps + "/" + d.name);
        if (removeIfEmpty(rbApps))
            say("  removed the emptied RetroArch/bin/apps");
        else
            say("  RetroArch/bin/apps kept: an app in it is not in Apps/");
    }
    return true;
}
