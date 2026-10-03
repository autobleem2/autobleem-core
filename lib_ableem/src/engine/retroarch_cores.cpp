#include "ableem/engine/retroarch_cores.h"
#include "ableem/engine/environment.h"
#include "ableem/engine/filesystem.h"
#include "ableem/engine/log.h"
#include "ableem/engine/strings.h"

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace std;

namespace ableem {

namespace {

// most extensions first; equal counts by file stem, so the order never depends on how the stick lists its files
bool sortByMaxExtensions(const CoreInfoPtr &i, const CoreInfoPtr &j) {
    if (i->extensions.size() != j->extensions.size())
        return i->extensions.size() > j->extensions.size();
    return i->stem < j->stem;
}

// "cores/km_snes9x2010_libretro.so" -> "km_snes9x2010"
string stemOf(const string &corePath) {
    string stem = DirEntry::getFileNameWithoutExtension(DirEntry::getFileNameFromPath(corePath));
    const string suffix = "_libretro";
    if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
        stem.erase(stem.size() - suffix.size());
    return stem;
}

string stripLpl(const string &dbName) {
    return DirEntry::matchExtension(dbName, "lpl") ? DirEntry::getFileNameWithoutExtension(dbName) : dbName;
}

// the value of a `key = "a|b|c"` line, unquoted and trimmed
string unquoted(const string &line, const string &lcaseLine) {
    string value = line.substr(lcaseLine.find('=') + 1);
    value.erase(remove(value.begin(), value.end(), '\"'), value.end());
    trim(value);
    return value;
}

vector<string> splitBar(const string &value) {
    vector<string> out;
    stringstream in(value);
    string item;
    while (getline(in, item, '|'))
        out.push_back(item);
    return out;
}

} // namespace

//*******************************
// CoreInfo::shortName
//*******************************
string CoreInfo::shortName() const {
    const size_t open = name.rfind('(');
    if (open == string::npos || name.empty() || name.back() != ')' || open + 2 > name.size())
        return name;
    string inside = name.substr(open + 1, name.size() - open - 2);
    trim(inside);
    return inside.empty() ? name : inside;
}

//*******************************
// CoreInfoTable::parseInfoFile
//*******************************
CoreInfoPtr CoreInfoTable::parseInfoFile(const string &file, const string &corePath) {
    CoreInfoPtr info = make_shared<CoreInfo>();
    info->core_path = corePath;
    info->stem = stemOf(corePath);

    ifstream in(file);
    string line;
    while (getline(in, line)) {
        string lcaseLine = line;
        lcase(lcaseLine);
        if (lcaseLine.find('=') == string::npos)
            continue;
        // the key whole: "database_match_archive_member = true" is not the "database" line (km_FinalBurn Neo's
        // .info has both, and the later one used to make its database "true")
        string key = lcaseLine.substr(0, lcaseLine.find('='));
        trim(key);
        if (key == "display_name") {
            info->name = unquoted(line, lcaseLine);
        } else if (key == "supported_extensions") {
            info->extensions = splitBar(unquoted(line, lcaseLine));
        } else if (key == "database") {
            info->databases = splitBar(unquoted(line, lcaseLine));
        } else if (key == "block_extract") {
            info->block_extract = toLowerCopy(unquoted(line, lcaseLine)) == "true";
        }
    }
    return info;
}

//*******************************
// CoreInfoTable::load
//*******************************
void CoreInfoTable::load(const string &retroarchDir, const string &coresCfgPath, const string &userCfgPath) {
    cores_.clear();
    databases_.clear();
    defaultCores_.clear();
    overrideCores_.clear();
    platformCores_.clear();

    if (!DirEntry::exists(retroarchDir)) {
        PLOG_WARNING << "RetroArch not found at " << retroarchDir;
        return;
    }
    const string infoDir = retroarchDir + sep + "info";
    const string coresDir = retroarchDir + sep + "cores";
    PLOG_INFO << "Reading core info files in " << infoDir;
    for (const DirEntry &entry : DirEntry::diru_FilesOnly(infoDir)) {
        if (DirEntry::getFileExtension(entry.name) != "info")
            continue;
        string corePath = coresDir + sep + DirEntry::getFileNameWithoutExtension(entry.name) +
                          Environment::getRetroarchCoreExtension();
        CoreInfoPtr info = parseInfoFile(infoDir + sep + entry.name, corePath);
        if (!DirEntry::exists(info->core_path))
            continue;
        cores_.push_back(info);
        for (const string &db : info->databases)
            databases_.insert(db);
    }
    stable_sort(cores_.begin(), cores_.end(), sortByMaxExtensions);
    PLOG_INFO << "Installed cores: " << cores_.size() << ", databases: " << databases_.size();

    // each database gets the first core (most extensions first) whose .info lists it
    for (const string &dbName : databases_) {
        for (const CoreInfoPtr &info : cores_) {
            if (find(info->databases.begin(), info->databases.end(), dbName) != info->databases.end()) {
                defaultCores_.emplace_back(dbName, info);
                PLOG_DEBUG << "Mapping DB: " << dbName << "  Core: " << info->name;
                break;
            }
        }
    }

    // the platform's cfg first, then the user's: overrideCores_ is searched from the front, so what the user chose
    // wins; platformCores_ keeps the platform's own picks (the window marks them "(default)")
    vector<pair<string, CoreInfoPtr>> platform = readCfg(coresCfgPath);
    platformCores_ = platform;
    overrideCores_ = readCfg(userCfgPath);
    overrideCores_.insert(overrideCores_.end(), platform.begin(), platform.end());
}

//*******************************
// CoreInfoTable::readCfg
//*******************************
vector<pair<string, CoreInfoPtr>> CoreInfoTable::readCfg(const string &path) const {
    vector<pair<string, CoreInfoPtr>> out;
    if (path.empty())
        return out;
    ifstream in(path);
    string line;
    while (getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.find('=') == string::npos)
            continue;
        string dbName = line.substr(0, line.find('='));
        string value = line.substr(line.find('=') + 1);
        lcase(dbName);
        trim(dbName);
        trim(value);
        CoreInfoPtr info = value.empty() ? nullptr : coreForCfgValue(value);
        if (info) {
            out.emplace_back(dbName, info);
            PLOG_INFO << "Core override: " << dbName << " -> " << info->name;
        } else {
            PLOG_WARNING << "Core override: " << dbName << " = '" << value << "' matches no installed core";
        }
    }
    return out;
}

//*******************************
// CoreInfoTable::platformCoreFor
//*******************************
CoreInfoPtr CoreInfoTable::platformCoreFor(const string &dbName) const {
    string key = stripLpl(dbName);
    lcase(key);
    trim(key);
    for (const auto &kv : platformCores_) {
        if (kv.first == key)
            return kv.second;
    }
    return defaultCoreFor(dbName);
}

//*******************************
// CoreInfoTable::platformOrder
//*******************************
CoreInfos CoreInfoTable::platformOrder(const string &dbName) const {
    CoreInfos out;
    CoreInfoPtr first = platformCoreFor(dbName);
    if (!first)
        return out;
    const string key = stripLpl(dbName);
    out.push_back(first);
    CoreInfos others;
    for (const CoreInfoPtr &info : cores_) {
        if (info != first && find(info->databases.begin(), info->databases.end(), key) != info->databases.end())
            others.push_back(info);
    }
    stable_sort(others.begin(), others.end(),
                [](const CoreInfoPtr &i, const CoreInfoPtr &j) { return i->stem < j->stem; });
    out.insert(out.end(), others.begin(), others.end());
    return out;
}

//*******************************
// CoreInfoTable::userPicks / saveUserPicks
//*******************************
map<string, string> CoreInfoTable::loadUserPicks(const string &path) {
    map<string, string> picks;
    ifstream in(path);
    string line;
    while (getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.find('=') == string::npos)
            continue;
        string db = line.substr(0, line.find('='));
        string value = line.substr(line.find('=') + 1);
        trim(db);
        trim(value);
        if (!db.empty() && !value.empty())
            picks[db] = value;
    }
    return picks;
}

bool CoreInfoTable::saveUserPicks(const string &path, const map<string, string> &picks) {
    if (picks.empty()) {
        if (DirEntry::exists(path))
            return DirEntry::removeFile(path);
        return true;
    }
    string text =
        "# AutoBleem - the cores you chose for a system (RetroArch cores window), \"<database>=<core file stem>\".\n"
        "# Read after the platform's own choices, so these win; a system set back to the platform's pick has no "
        "line.\n";
    for (const auto &kv : picks)
        text += kv.first + "=" + kv.second + "\n";
    return DirEntry::writeFileIfChanged(path, text) != DirEntry::WriteResult::Failed;
}

//*******************************
// CoreInfoTable::coreForCfgValue
//*******************************
CoreInfoPtr CoreInfoTable::coreForCfgValue(const string &value) const {
    for (const CoreInfoPtr &info : cores_) { // the file stem, exactly
        if (info->stem == value)
            return info;
    }
    for (const CoreInfoPtr &info : cores_) { // the display name, exactly
        if (info->name == value)
            return info;
    }
    for (const CoreInfoPtr &info : cores_) { // a part of the display name: the first in the table's order
        if (info->name.find(value) != string::npos)
            return info;
    }
    return nullptr;
}

//*******************************
// CoreInfoTable::overrideCoreFor
//*******************************
CoreInfoPtr CoreInfoTable::overrideCoreFor(const string &dbName) const {
    string key = stripLpl(dbName);
    lcase(key);
    trim(key);
    for (const auto &kv : overrideCores_) {
        if (kv.first == key)
            return kv.second;
    }
    return nullptr;
}

//*******************************
// CoreInfoTable::defaultCoreFor
//*******************************
CoreInfoPtr CoreInfoTable::defaultCoreFor(const string &dbName) const {
    const string key = stripLpl(dbName);
    for (const auto &kv : defaultCores_) {
        if (kv.first == key)
            return kv.second;
    }
    return nullptr;
}

//*******************************
// CoreInfoTable::coresForDatabase
//*******************************
CoreInfos CoreInfoTable::coresForDatabase(const string &dbName) const {
    CoreInfos out;
    CoreInfoPtr preferred = coreForDatabase(dbName);
    if (!preferred)
        return out;
    const string key = stripLpl(dbName);
    out.push_back(preferred);
    CoreInfos others;
    for (const CoreInfoPtr &info : cores_) {
        if (info != preferred && find(info->databases.begin(), info->databases.end(), key) != info->databases.end())
            others.push_back(info);
    }
    stable_sort(others.begin(), others.end(),
                [](const CoreInfoPtr &i, const CoreInfoPtr &j) { return i->stem < j->stem; });
    out.insert(out.end(), others.begin(), others.end());
    return out;
}

//*******************************
// CoreInfoTable::coreForDatabase
//*******************************
CoreInfoPtr CoreInfoTable::coreForDatabase(const string &dbName) const {
    CoreInfoPtr core = overrideCoreFor(dbName);
    return core ? core : defaultCoreFor(dbName);
}

} // namespace ableem
