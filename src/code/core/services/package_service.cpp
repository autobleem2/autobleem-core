//
// PackageService - see the header.
//
#include "package_service.h"
#include "content_installer.h"
#include "environment.h"
#include "../main.h"

#include <ableem/engine/log.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace std;

namespace {

const size_t MaxDescriptorBytes = 64 * 1024;
const size_t MaxTitleChars = 80;
const size_t MaxDescriptionChars = 200;
const size_t MaxIdLength = 40;

string lower(const string &s) {
    return ableem::toLowerCopy(s);
}

string lowerTrim(const string &s) {
    return ableem::toLowerCopy(Strings::trim(s));
}

// at most `limit` characters (UTF-8 code points), never cutting one in half
string truncateChars(const string &s, size_t limit) {
    size_t chars = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (chars == limit)
                return s.substr(0, i);
            chars++;
        }
    }
    return s;
}

// directories the scan never looks into (and files it never matches)
bool skippedName(const string &name) {
    return name.empty() || name[0] == '.' || name[0] == '$' || lower(name) == "system volume information";
}

bool byLowerName(const DirEntry &a, const DirEntry &b) {
    const string x = lower(a.name), y = lower(b.name);
    return x == y ? a.name < b.name : x < y;
}

//******************
// Lister
//******************
// directory listings, each read once per scan: sorted by lower-cased name, so the walk is the same on every
// stick, and a name is found whatever its letter case (FAT keeps the case it was written with and ignores it when
// looking a name up; a case-sensitive stick does not)
class Lister {
public:
    const DirEntries &list(const string &dir) {
        auto it = cache_.find(dir);
        if (it != cache_.end())
            return it->second;
        DirEntries entries = DirEntry::diru(dir);
        sort(entries.begin(), entries.end(), byLowerName);
        return cache_.emplace(dir, std::move(entries)).first->second;
    }

    // the entry of `dir` called `name` in any letter case (the exact spelling first); nullptr when there is none
    const DirEntry *find(const string &dir, const string &name, bool wantDir) {
        const DirEntry *loose = nullptr;
        const string wanted = lower(name);
        for (const DirEntry &e : list(dir)) {
            if (e.isDir != wantDir || lower(e.name) != wanted)
                continue;
            if (e.name == name)
                return &e;
            if (loose == nullptr)
                loose = &e;
        }
        return loose;
    }

    // `rel` ('/' separated) under `root` with the spelling the disk has; "" when it is not there or is not a file
    // (wantFile) / a folder (!wantFile)
    string resolve(const string &root, const string &rel, bool wantFile) {
        string dir = root, real;
        const vector<string> segments = Strings::getTokens(rel, '/');
        for (size_t i = 0; i < segments.size(); i++) {
            const bool last = i + 1 == segments.size();
            const DirEntry *e = find(dir, segments[i], !(last && wantFile));
            if (e == nullptr)
                return "";
            real += (real.empty() ? "" : "/") + e->name;
            dir += "/" + e->name;
        }
        return real;
    }

private:
    map<string, DirEntries> cache_;
};

// the first `magic.size()` bytes of the file are `magic`
bool headMatches(const string &file, const string &magic) {
    ifstream in(file, ios::binary);
    char head[4] = {};
    in.read(head, static_cast<streamsize>(magic.size()));
    return static_cast<size_t>(in.gcount()) == magic.size() && string(head, magic.size()) == magic;
}

// a folder's name as a package id: lower case, anything but [a-z0-9] a '-', runs and the ends trimmed
string idFromFolder(const string &name) {
    string id;
    for (char c : lower(name)) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (ok)
            id += c;
        else if (!id.empty() && id.back() != '-')
            id += '-';
    }
    while (!id.empty() && id.back() == '-')
        id.pop_back();
    if (id.size() > MaxIdLength)
        id.resize(MaxIdLength);
    while (!id.empty() && id.back() == '-')
        id.pop_back();
    return id.empty() ? "package" : id;
}

// an App folder name the descriptor's Replaces= may name: one plain folder name
bool plainFolderName(const string &name) {
    return !name.empty() && name[0] != '.' && name.find('/') == string::npos && name.find('\\') == string::npos &&
           name.find(':') == string::npos;
}

// ---------------------------------------------------------------------------------------------
// the descriptor reader (docs/packages.md 2.2)
// ---------------------------------------------------------------------------------------------
bool readDescriptorWith(Lister &lister, const string &folder, PackageInfo &out, string &problem,
                        const string &nameForId = "") {
    const DirEntry *entry = lister.find(folder, "package.ini", false);
    if (entry == nullptr) {
        problem = "there is no package.ini";
        return false;
    }
    const string path = folder + "/" + entry->name;
    const long long size = DirEntry::fileSize(path);
    if (size < 0 || static_cast<size_t>(size) > MaxDescriptorBytes) {
        problem = "package.ini is over 64 KB";
        return false;
    }
    string text;
    if (!DirEntry::readFile(path, text)) {
        problem = "package.ini cannot be read";
        return false;
    }

    map<string, string> v;
    for (const auto &kv : PackageService::parseDescriptor(text))
        v[kv.first] = kv.second;
    auto get = [&v](const string &key) {
        auto it = v.find(key);
        return it == v.end() ? string() : it->second;
    };

    PackageInfo info;
    info.root = folder;
    info.descriptor = true;
    info.title = truncateChars(get("title"), MaxTitleChars);
    if (info.title.empty()) {
        problem = "package.ini has no Title";
        return false;
    }
    string folderName = nameForId;
    if (folderName.empty()) {
        folderName = folder;
        const size_t slash = folderName.find_last_of('/');
        if (slash != string::npos)
            folderName = folderName.substr(slash + 1);
    }
    const string wantedId = lowerTrim(get("id"));
    if (!wantedId.empty() && !PackageTable::validName(wantedId, MaxIdLength)) {
        problem = "the package id \"" + get("id") + "\" is not a valid id";
        return false;
    }
    info.id = wantedId.empty() ? idFromFolder(folderName) : wantedId;
    info.version = get("version");
    info.licence = get("licence");
    info.author = get("author");
    info.description = truncateChars(get("description"), MaxDescriptionChars);
    string source = lowerTrim(get("source"));
    info.source = (source == "store" || source == "mod") ? source : "user";
    info.storeId = get("storeid");
    info.peSource = get("pesource");
    for (const string &name : Strings::getTokens(get("replaces"), ';')) {
        const string n = Strings::trim(name);
        if (plainFolderName(n))
            info.replaces.push_back(n);
    }
    const string image = PackageTable::cleanRelativePath(get("image"));
    if (!image.empty()) {
        const string real = lister.resolve(folder, image, true);
        if (!real.empty())
            info.image = folder + "/" + real;
    }
    const string readme = PackageTable::cleanRelativePath(get("readme"));
    if (!readme.empty()) {
        const string real = lister.resolve(folder, readme, true);
        if (!real.empty())
            info.readme = folder + "/" + real;
    }

    // the default kinds: one, or several separated by ';'
    vector<string> kinds;
    for (const string &k : Strings::getTokens(get("kind"), ';')) {
        const string kind = lowerTrim(k);
        if (PackageTable::validName(kind) && find(kinds.begin(), kinds.end(), kind) == kinds.end())
            kinds.push_back(kind);
    }

    set<string> seenIds;
    for (int n = 1;; n++) { // info.games collects the games that survive
        const string p = "game" + to_string(n) + ".";
        const string title = Strings::trim(get(p + "title"));
        if (title.empty())
            break; // the first N without a Title ends the list
        PackageGame game;
        game.title = truncateChars(title, MaxTitleChars);
        const string fileRel = PackageTable::cleanRelativePath(get(p + "file"));
        game.file = fileRel.empty() ? string() : lister.resolve(folder, fileRel, true);
        if (game.file.empty()) {
            PLOG_INFO << folder << ": game " << n << " (" << title << ") dropped: its File is missing or not allowed";
            continue;
        }
        const string kindWanted = lowerTrim(get(p + "kind"));
        game.kind = kindWanted.empty() ? (kinds.empty() ? string() : kinds.front()) : kindWanted;
        if (!PackageTable::validName(game.kind)) {
            PLOG_INFO << folder << ": game " << n << " (" << title << ") dropped: no valid kind";
            continue;
        }
        const string idWanted = lowerTrim(get(p + "id"));
        game.id = idWanted.empty() ? "game" + to_string(n) : idWanted;
        if (!PackageTable::validName(game.id, MaxIdLength) || !seenIds.insert(game.id).second) {
            PLOG_INFO << folder << ": game " << n << " (" << title << ") dropped: its id is invalid or taken";
            continue;
        }
        game.variant = get(p + "variant");
        for (int m = 1;; m++) {
            const string sp = p + "start" + to_string(m) + ".";
            const string startFile = PackageTable::cleanRelativePath(get(sp + "file"));
            if (get(sp + "file").empty())
                break;
            const string real = startFile.empty() ? string() : lister.resolve(folder, startFile, true);
            if (real.empty())
                continue;
            const string startTitle = Strings::trim(get(sp + "title"));
            game.starts.push_back({real, startTitle.empty() ? real : startTitle});
        }
        const string dosbox = p + "dosbox.";
        for (const auto &kv : v) {
            if (kv.first.compare(0, dosbox.size(), dosbox) == 0 && kv.first.size() > dosbox.size())
                game.settings.emplace_back(kv.first.substr(dosbox.size()), kv.second);
        }
        const string mapper = PackageTable::cleanRelativePath(get(p + "mapper"));
        if (!mapper.empty())
            game.mapper = lister.resolve(folder, mapper, true);
        info.games.push_back(game);
    }
    if (info.games.empty()) {
        problem = "package.ini has no game left";
        return false;
    }
    out = std::move(info);
    return true;
}

// ---------------------------------------------------------------------------------------------
// the scan (docs/packages.md 2.1, 4.4)
// ---------------------------------------------------------------------------------------------
class Scanner {
public:
    Scanner(const vector<PackageRow> &rows, const PackageService::Limits &limits, vector<string> &problems)
        : rows_(rows), limits_(limits), problems_(problems) {}

    // Packages/ itself: recognised folders, descriptor packages, and the unknown data below the top level
    void scanPackages(const string &dir, vector<PackageInfo> &out) {
        visit(dir, "", "u", "Packages", 0, true, false, out);
    }

    // an engine's own data folder (PackageDir=): `rel` is the folder under the App, for the ids
    void scanEngineDir(const string &dir, const string &rel, vector<PackageInfo> &out) {
        const size_t slash = rel.find_last_of('/');
        visit(dir, rel, "e", slash == string::npos ? rel : rel.substr(slash + 1), 0, false, true, out);
    }

private:
    void visit(const string &dir, const string &rel, const string &idBase, const string &title, int depth,
               bool markUnknown, bool inApp, vector<PackageInfo> &out) {
        if (++folders_ > limits_.maxFolders) {
            if (!capped_) {
                capped_ = true;
                problems_.push_back("stopped after " + to_string(limits_.maxFolders) + " folders");
                PLOG_WARNING << "PackageService: the scan stopped after " << limits_.maxFolders << " folders";
            }
            return;
        }
        // a descriptor package is never looked into
        for (const DirEntry &e : lister_.list(dir)) {
            if (!e.isDir && lower(e.name) == "package.ini") {
                PackageInfo info;
                string problem;
                if (readDescriptorWith(lister_, dir, info, problem)) {
                    info.inApp = inApp;
                    out.push_back(std::move(info));
                } else {
                    problems_.push_back(dir + ": " + problem);
                    PLOG_INFO << "PackageService: " << dir << ": " << problem;
                }
                return;
            }
        }

        PackageInfo here;
        if (recognise(dir, here.games)) {
            here.root = dir;
            here.title = title;
            here.id = idBase + "/" + lower(rel);
            here.source = "user";
            here.inApp = inApp;
            for (const PackageGame &g : here.games) {
                if (here.licence.empty())
                    here.licence = g.licence;
            }
            out.push_back(std::move(here));
        }
        if (depth >= limits_.maxDepth)
            return;
        for (const DirEntry &e : lister_.list(dir)) {
            if (!e.isDir || skippedName(e.name))
                continue;
            const size_t before = out.size();
            const string childRel = rel.empty() ? e.name : rel + "/" + e.name;
            visit(dir + "/" + e.name, childRel, idBase, e.name, depth + 1, false, inApp, out);
            if (markUnknown && out.size() == before && !capped_) {
                // nothing in it was recognised, here or below: data we do not know
                PackageInfo unknown;
                unknown.id = idBase + "/" + lower(e.name);
                unknown.title = e.name;
                unknown.root = dir + "/" + e.name;
                unknown.source = "user";
                unknown.unknown = true;
                out.push_back(std::move(unknown));
            }
        }
    }

    // the games the table finds in `dir`: the first matching row per main file wins
    bool recognise(const string &dir, vector<PackageGame> &games) {
        set<string> claimed;
        set<string> ids;
        for (const PackageRow &row : rows_) {
            vector<string> real;
            bool all = true;
            for (const string &m : row.match) {
                const string r = lister_.resolve(dir, m, true);
                if (r.empty()) {
                    all = false;
                    break;
                }
                real.push_back(r);
            }
            if (!all || real.empty())
                continue;
            const string main = row.main.empty() ? real.front() : lister_.resolve(dir, row.main, true);
            if (main.empty() || claimed.count(lower(main)) != 0)
                continue;
            if (row.size >= 0 && DirEntry::fileSize(dir + "/" + real.front()) != row.size)
                continue;
            if (!row.magic.empty() && !headMatches(dir + "/" + main, row.magic))
                continue;
            claimed.insert(lower(main));

            PackageGame game;
            game.id = row.id;
            for (int n = 2; !ids.insert(game.id).second; n++)
                game.id = row.id + "-" + to_string(n);
            game.title = row.title;
            game.variant = row.variant;
            game.kind = row.kind;
            game.file = main;
            game.licence = row.licence;
            for (const auto &s : row.starts) {
                const string r = lister_.resolve(dir, s.first, true);
                if (!r.empty())
                    game.starts.push_back({r, s.second.empty() ? r : s.second});
            }
            game.settings = row.settings;
            if (!row.mapper.empty())
                game.mapper = lister_.resolve(dir, row.mapper, true);
            games.push_back(std::move(game));
        }
        return !games.empty();
    }

    Lister lister_;
    const vector<PackageRow> &rows_;
    PackageService::Limits limits_;
    vector<string> &problems_;
    size_t folders_ = 0;
    bool capped_ = false;
};

struct KindName {
    const char *kind;
    const char *name;
};

// docs/packages.md 3.3 - the display names (translated by the caller)
const KindName KindNames[] = {
    {"doom-iwad", "Doom data"},
    {"heretic-iwad", "Heretic data"},
    {"hexen-iwad", "Hexen data"},
    {"strife-iwad", "Strife data"},
    {"quake-id1", "Quake data"},
    {"q3-openarena", "OpenArena data"},
    {"q3-baseq3", "Quake III Arena data"},
    {"theme-hospital", "Theme Hospital data"},
    {"dos-game", "DOS game"},
    {"duke3d-grp", "Duke Nukem 3D data"},
    {"sw-grp", "Shadow Warrior data"},
};

} // namespace

//*******************************
// PackageInfo::kinds
//*******************************
vector<string> PackageInfo::kinds() const {
    vector<string> out;
    for (const PackageGame &g : games)
        if (find(out.begin(), out.end(), g.kind) == out.end())
            out.push_back(g.kind);
    return out;
}

//*******************************
// PackageService::parseDescriptor
//*******************************
vector<pair<string, string>> PackageService::parseDescriptor(const string &text) {
    vector<pair<string, string>> out;
    istringstream in(text);
    string line;
    int number = 0;
    while (getline(in, line)) {
        number++;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3);
        Strings::removeComment(line); // a '#' starts a comment anywhere on the line
        line = Strings::trim(line);
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '[' || eq == string::npos)
            continue; // the [package] header is conventional and ignored
        out.emplace_back(lowerTrim(line.substr(0, eq)), Strings::trim(line.substr(eq + 1)));
    }
    return out;
}

//*******************************
// PackageService::readDescriptor
//*******************************
bool PackageService::readDescriptor(const string &folder, PackageInfo &out, string &problem, const string &folderName) {
    Lister lister;
    PackageInfo info;
    if (!readDescriptorWith(lister, folder, info, problem, folderName))
        return false;
    out = std::move(info);
    return true;
}

//*******************************
// PackageService::index / packages / rows / problems
//*******************************
shared_ptr<const PackageService::Index> PackageService::index() const {
    lock_guard<mutex> lock(mutex_);
    return index_;
}

vector<PackageInfo> PackageService::packages() const {
    return index()->packages;
}

size_t PackageService::packageCount() const {
    return index()->packages.size();
}

vector<PackageRow> PackageService::rows() const {
    return index()->rows;
}

vector<string> PackageService::problems() const {
    return index()->problems;
}

//*******************************
// PackageService::rescan
//*******************************
void PackageService::rescan() {
    rescan(Env::getPathToPackagesDir(), Env::getPathToPackagesTable());
}

void PackageService::rescan(const string &packagesDir, const string &shippedTable, const Limits &limits) {
    auto fresh = make_shared<Index>();
    fresh->limits = limits;

    // the player's table first (his rows win), then the shipped one - both only ever read
    if (DirEntry::isDirectory(packagesDir)) {
        Lister lister;
        const string own = lister.resolve(packagesDir, "packages.ini", true);
        if (!own.empty())
            PackageTable::load(packagesDir + "/" + own, fresh->rows, fresh->problems);
    }
    PackageTable::load(shippedTable, fresh->rows, fresh->problems);

    if (DirEntry::isDirectory(packagesDir)) {
        Scanner scanner(fresh->rows, limits, fresh->problems);
        scanner.scanPackages(packagesDir, fresh->packages);
        // two folders that claim one id: the first by folder name keeps it
        set<string> ids;
        for (PackageInfo &p : fresh->packages) {
            if (!p.unknown && !ids.insert(p.id).second) {
                p.duplicate = true;
                fresh->problems.push_back(p.root + ": the id " + p.id + " is taken by another folder");
            }
        }
    }
    PLOG_INFO << "PackageService: " << fresh->packages.size() << " package(s) in " << packagesDir;
    for (const string &p : fresh->problems)
        PLOG_INFO << "PackageService: " << p;

    lock_guard<mutex> lock(mutex_);
    index_ = fresh;
}

//*******************************
// PackageService::entriesFor
//*******************************
vector<PackageEntry> PackageService::entriesFor(const AppManifest &app) const {
    vector<PackageEntry> entries;
    if (app.uses.empty())
        return entries; // an App with no Uses= takes no package

    shared_ptr<const Index> idx = index();
    vector<PackageInfo> sources;
    for (const PackageInfo &p : idx->packages)
        if (!p.unknown && !p.duplicate)
            sources.push_back(p);

    // the engine's own data folders, scanned now with the same tables
    for (const string &dir : app.packageDirs) {
        const string rel = PackageTable::cleanRelativePath(dir);
        if (rel.empty())
            continue;
        const string abs = app.folder + "/" + rel;
        if (!DirEntry::isDirectory(abs))
            continue;
        vector<string> ignored;
        Scanner scanner(idx->rows, idx->limits, ignored);
        scanner.scanEngineDir(abs, rel, sources);
    }

    for (const PackageInfo &p : sources) {
        for (const PackageGame &g : p.games) {
            if (find(app.uses.begin(), app.uses.end(), g.kind) == app.uses.end())
                continue;
            PackageEntry e;
            e.packageId = p.id;
            e.packageTitle = p.title;
            e.source = p.source;
            e.inApp = p.inApp;
            e.root = p.root;
            e.game = g;
            entries.push_back(std::move(e));
        }
    }
    auto place = [&app](const PackageEntry &e) {
        return find(app.uses.begin(), app.uses.end(), e.game.kind) - app.uses.begin();
    };
    stable_sort(entries.begin(), entries.end(), [&place](const PackageEntry &a, const PackageEntry &b) {
        const auto pa = place(a), pb = place(b);
        if (pa != pb)
            return pa < pb;
        if (naturalLess(a.game.title, b.game.title))
            return true;
        if (naturalLess(b.game.title, a.game.title))
            return false;
        return naturalLess(a.packageTitle, b.packageTitle);
    });
    return entries;
}

//*******************************
// PackageService::stillThere
//*******************************
bool PackageService::stillThere(const PackageEntry &entry) {
    const string file = entry.file();
    if (!DirEntry::exists(file) || DirEntry::isDirectory(file))
        return false;
    const string mapper = entry.mapperFile();
    return mapper.empty() || DirEntry::exists(mapper);
}

//*******************************
// PackageService::signatureOf
//*******************************
string PackageService::signatureOf(const string &packagesDir) {
    if (!DirEntry::isDirectory(packagesDir))
        return "";
    string sig;
    DirEntries top = DirEntry::diru(packagesDir);
    sort(top.begin(), top.end(), byLowerName);
    for (const DirEntry &e : top) {
        sig += lower(e.name);
        if (!e.isDir) {
            sig += ":" + to_string(DirEntry::fileSize(packagesDir + "/" + e.name)) + "\n";
            continue;
        }
        sig += "/\n";
        if (skippedName(e.name))
            continue;
        // one level down: a game folder dropped into an existing folder changes it too
        DirEntries below = DirEntry::diru(packagesDir + "/" + e.name);
        sort(below.begin(), below.end(), byLowerName);
        for (const DirEntry &b : below) {
            sig += "  " + lower(b.name);
            sig +=
                b.isDir ? "/\n" : ":" + to_string(DirEntry::fileSize(packagesDir + "/" + e.name + "/" + b.name)) + "\n";
        }
    }
    return sig;
}

//*******************************
// PackageService::kindName / sourceName / knownKinds
//*******************************
string PackageService::kindName(const string &kind) {
    for (const KindName &k : KindNames)
        if (kind == k.kind)
            return k.name;
    return kind;
}

string PackageService::sourceName(const string &source, bool inApp) {
    if (inApp)
        return "In this App";
    if (source == "store")
        return "Store";
    if (source == "mod")
        return "Mod";
    return "Your files";
}

const vector<string> &PackageService::knownKinds() {
    static const vector<string> kinds = [] {
        vector<string> out;
        for (const KindName &k : KindNames)
            out.push_back(k.kind);
        return out;
    }();
    return kinds;
}

//*******************************
// PackageService::naturalLess
//*******************************
bool PackageService::naturalLess(const string &a, const string &b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[j]);
        if (isdigit(x) && isdigit(y)) {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && a[i2] == '0')
                i2++;
            while (j2 < b.size() && b[j2] == '0')
                j2++;
            size_t ie = i2, je = j2;
            while (ie < a.size() && isdigit(static_cast<unsigned char>(a[ie])))
                ie++;
            while (je < b.size() && isdigit(static_cast<unsigned char>(b[je])))
                je++;
            if (ie - i2 != je - j2)
                return ie - i2 < je - j2; // fewer digits = smaller
            const int c = a.compare(i2, ie - i2, b, j2, je - j2);
            if (c != 0)
                return c < 0;
            i = ie;
            j = je;
            continue;
        }
        const int lx = tolower(x), ly = tolower(y);
        if (lx != ly)
            return lx < ly;
        i++;
        j++;
    }
    return (a.size() - i) < (b.size() - j);
}

//*******************************
// PackageService::readmeText
//*******************************
string PackageService::readmeText(const vector<PackageRow> &rows) {
    string text;
    text += "Packages - game data for the engines on this stick\n";
    text += "====================================================\n\n";
    text += "This folder, Packages, is where game data goes: the files of a game an engine (an App such as\n";
    text += "Crispy Doom, TyrQuake or DOSBox) plays. Put the game's files here - or its whole folder - and\n";
    text += "start the engine: it offers every game it can run.\n\n";
    text += "Examples (file names may be in any letter case; sub-folders up to four levels deep are looked into):\n\n";
    set<string> shown;
    for (const PackageRow &row : rows) {
        if (!shown.insert(row.kind).second || row.match.empty())
            continue;
        text += "  Packages/" + GameInstaller::folderNameFor(row.title) + "/" + row.match.front() + "\n";
    }
    text += "\nYour own files are only read, never changed or moved.\n\n";
    text += "A game that is not listed: add one row for it to Packages/packages.ini (the manual, \"Your own\n";
    text += "games\", shows how), or - for a DOS game - put a package.ini next to it.\n\n";
    text += "More games and free data (Freedoom, Quake shareware) come from the Store. We never supply\n";
    text += "commercial game data.\n";
    return text;
}
