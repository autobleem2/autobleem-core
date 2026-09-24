#include "processor_catalog.h"
#include "../main.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

using namespace std;

namespace {

vector<string> splitList(const string &value, const string &separators) {
    vector<string> out;
    string current;
    for (char c : value + separators.substr(0, 1)) {
        if (separators.find(c) != string::npos) {
            string item = Strings::trim(current);
            if (!item.empty())
                out.push_back(item);
            current.clear();
        } else {
            current += c;
        }
    }
    return out;
}

string lower(string s) {
    for (char &c : s)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

//*******************************
// ProcessorInfo
//*******************************
bool ProcessorInfo::has(ProcessorKind kind) const {
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

bool ProcessorInfo::belongsTo(ProcessorSequence sequence) const {
    if (sequence == ProcessorSequence::Ps1)
        return has(ProcessorKind::GamesFolder) || has(ProcessorKind::Ps1);
    return has(ProcessorKind::RomsFolder) || has(ProcessorKind::Rom);
}

bool ProcessorInfo::matchesFile(const string &fileName) const {
    if (match.empty())
        return true;
    for (const string &pattern : match) {
        if (ProcessorCatalog::globMatch(fileName, pattern))
            return true;
    }
    return false;
}

bool ProcessorInfo::wantsSystem(const string &system) const {
    if (systems.empty())
        return true;
    for (const string &s : systems) {
        if (lower(s) == lower(system))
            return true;
    }
    return false;
}

//*******************************
// ProcessorCatalog
//*******************************
ProcessorCatalog::ProcessorCatalog(string processorsDir, vector<string> keys)
    : dir_(std::move(processorsDir)), keys_(std::move(keys)) {}

//*******************************
// ProcessorCatalog::globMatch
//*******************************
bool ProcessorCatalog::globMatch(const string &name, const string &pattern) {
    // the classic two-pointer walk with one backtrack point per '*'
    size_t n = 0, p = 0, star = string::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || tolower(static_cast<unsigned char>(pattern[p])) ==
                                                            tolower(static_cast<unsigned char>(name[n])))) {
            ++n;
            ++p;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != string::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

//*******************************
// ProcessorCatalog::parseKinds
//*******************************
vector<ProcessorKind> ProcessorCatalog::parseKinds(const string &value) {
    vector<ProcessorKind> kinds;
    for (const string &item : splitList(lower(value), ",; ")) {
        ProcessorKind kind;
        if (item == "games-folder")
            kind = ProcessorKind::GamesFolder;
        else if (item == "roms-folder")
            kind = ProcessorKind::RomsFolder;
        else if (item == "ps1")
            kind = ProcessorKind::Ps1;
        else if (item == "rom")
            kind = ProcessorKind::Rom;
        else
            continue;
        if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
            kinds.push_back(kind);
    }
    return kinds;
}

//*******************************
// ProcessorCatalog::kindName
//*******************************
const char *ProcessorCatalog::kindName(ProcessorKind kind) {
    switch (kind) {
    case ProcessorKind::GamesFolder:
        return "games-folder";
    case ProcessorKind::RomsFolder:
        return "roms-folder";
    case ProcessorKind::Ps1:
        return "ps1";
    case ProcessorKind::Rom:
        return "rom";
    }
    return "";
}

//*******************************
// ProcessorCatalog::stripComment
//*******************************
string ProcessorCatalog::stripComment(const string &value) {
    for (size_t i = 1; i < value.size(); ++i) {
        if ((value[i] == ';' || value[i] == '#') && (value[i - 1] == ' ' || value[i - 1] == '\t'))
            return Strings::trim(value.substr(0, i));
    }
    if (!value.empty() && (value[0] == ';' || value[0] == '#'))
        return "";
    return Strings::trim(value);
}

//*******************************
// ProcessorCatalog::load
//*******************************
ProcessorInfo ProcessorCatalog::load(const string &folder, const vector<string> &keys) {
    ProcessorInfo info;
    info.folder = folder;
    info.name = DirEntry::getFileNameFromPath(folder);

    map<string, string> values;
    string iniPath = folder + sep + "processor.ini";
    if (DirEntry::exists(iniPath)) {
        IniFile ini;
        ini.load(iniPath);
        for (const auto &kv : ini.values)
            values[Strings::trim(kv.first)] = stripComment(kv.second);
    }
    AppManifest::Options options;
    options.extensions = AppManifest::programExtensions();
    options.allowStartup = false;
    info.manifest = AppManifest::resolve(folder, values, keys, options);
    if (!DirEntry::exists(iniPath))
        info.manifest.problem = "there is no processor.ini";

    auto value = [&values](const string &key) {
        auto it = values.find(key);
        return it == values.end() ? string() : it->second;
    };
    info.title = value("name").empty() ? info.name : value("name");
    info.description = value("description");
    info.author = value("author");
    info.version = value("version");
    info.kinds = parseKinds(value("kinds"));
    info.match = splitList(value("match"), ";,");
    info.systems = splitList(value("systems"), ";");
    if (!value("order").empty())
        info.order = atoi(value("order").c_str());
    if (!value("timeout").empty())
        info.timeoutSeconds = max(0, atoi(value("timeout").c_str()));
    info.modifies = AppManifest::parseFlag(value("modifies"), true);
    return info;
}

//*******************************
// ProcessorCatalog::folderReadme
//*******************************
const char *ProcessorCatalog::folderReadme() {
    return R"(AutoBleem scanner processors
============================

Programs in this folder run before every scan of your games. A processor can turn a
format AutoBleem does not read into one it does (a zipped game, for example), or
change a game's data (a translation patch, a mod).

Each processor is a folder of its own:

    System/Processors/<name>/
        processor.ini
        bin/psc/<name>                  the PlayStation Classic
        bin/linux-armhf/<name>          any 32-bit Raspberry Pi
        bin/linux-arm64/<name>          any 64-bit Raspberry Pi
        bin/linux-i386/<name>           the PC stick
        bin/windows-x86_64/<name>.exe   Windows

Only the bin/ folders for your machines are needed. Unpack a processor here and the
next scan runs it. The System menu (L2+R2) -> Scanner processors puts them in order
and switches them on or off; sequence.ini in this folder is that order.

The first one, and the example to copy when you write your own:
    https://github.com/autobleem2/proc_unzip
)";
}

//*******************************
// ProcessorCatalog::ensureFolder
//*******************************
bool ProcessorCatalog::ensureFolder(const string &processorsDir) {
    if (!DirEntry::createDirs(processorsDir))
        return false;
    string readme = processorsDir + sep + "README.txt";
    if (!DirEntry::exists(readme))
        DirEntry::writeFileIfChanged(readme, folderReadme());
    return true;
}

//*******************************
// ProcessorCatalog::scan
//*******************************
const vector<ProcessorInfo> &ProcessorCatalog::scan() {
    processors_.clear();
    if (!DirEntry::isDirectory(dir_))
        return processors_;
    for (const DirEntry &entry : DirEntry::diru_DirsOnly(dir_)) {
        if (entry.name.empty() || entry.name[0] == '.')
            continue;
        string folder = dir_ + sep + entry.name;
        if (!DirEntry::exists(folder + sep + "processor.ini"))
            continue;
        processors_.push_back(load(folder, keys_));
    }
    sort(processors_.begin(), processors_.end(),
         [](const ProcessorInfo &a, const ProcessorInfo &b) { return a.name < b.name; });
    return processors_;
}

//*******************************
// ProcessorCatalog::find
//*******************************
const ProcessorInfo *ProcessorCatalog::find(const string &name) const {
    for (const ProcessorInfo &p : processors_) {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

//*******************************
// ProcessorCatalog::watchPatterns
//*******************************
vector<string> ProcessorCatalog::watchPatterns() const {
    vector<string> patterns;
    for (const ProcessorInfo &p : processors_) {
        if (!p.builtForThisSystem())
            continue;
        for (const string &m : p.match) {
            if (std::find(patterns.begin(), patterns.end(), m) == patterns.end())
                patterns.push_back(m);
        }
    }
    return patterns;
}
