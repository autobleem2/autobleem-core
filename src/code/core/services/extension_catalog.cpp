//
// ExtensionCatalog - see the header.
//
#include "extension_catalog.h"
#include "../main.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <ableem/engine/log.h>

using namespace std;

//*******************************
// ExtensionCatalog::ExtensionCatalog
//*******************************
ExtensionCatalog::ExtensionCatalog(string extensionsDir, string stateDir, vector<string> keys, string pluginExtension,
                                   string runtimeDir)
    : extensionsDir_(std::move(extensionsDir)), stateDir_(std::move(stateDir)), runtimeDir_(std::move(runtimeDir)),
      keys_(std::move(keys)), pluginExtension_(std::move(pluginExtension)) {
    if (runtimeDir_.empty())
        runtimeDir_ = stateDir_;
}

string ExtensionCatalog::disabledFile() const {
    return stateDir_ + sep + "disabled.txt";
}

string ExtensionCatalog::activeFile() const {
    return runtimeDir_ + sep + "extensions.active";
}

string ExtensionCatalog::persistedActiveFile() const {
    return stateDir_ + sep + ".active";
}

//*******************************
// ExtensionCatalog::parseNetwork
//*******************************
ExtensionNetwork ExtensionCatalog::parseNetwork(const string &value) {
    string v = ableem::toLowerCopy(Strings::trim(value));
    if (v == "required")
        return ExtensionNetwork::Required;
    if (v == "optional")
        return ExtensionNetwork::Optional;
    return ExtensionNetwork::None;
}

//*******************************
// ExtensionCatalog::parseProvides / ExtensionInfo::providesEntry
//*******************************
vector<string> ExtensionCatalog::parseProvides(const string &value) {
    vector<string> entries;
    string current;
    auto flush = [&]() {
        if (!current.empty() && std::find(entries.begin(), entries.end(), current) == entries.end())
            entries.push_back(current);
        current.clear();
    };
    for (char c : ableem::toLowerCopy(value)) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t')
            flush();
        else
            current += c;
    }
    flush();
    return entries;
}

bool ExtensionInfo::providesEntry(const string &entry) const {
    const string wanted = ableem::toLowerCopy(Strings::trim(entry));
    return !wanted.empty() && std::find(provides.begin(), provides.end(), wanted) != provides.end();
}

//*******************************
// ExtensionCatalog::scan
//*******************************
const vector<ExtensionInfo> &ExtensionCatalog::scan() {
    // what the runtime found out about a plugin stays with its name across a rescan
    map<string, string> problems;
    for (const ExtensionInfo &e : extensions_)
        if (!e.loadProblem.empty())
            problems[e.name] = e.loadProblem;

    extensions_.clear();
    if (!DirEntry::exists(extensionsDir_))
        return extensions_;
    const vector<string> disabled = readDisabled();

    AppManifest::Options options;
    options.programKey = "plugin";
    options.extensions = {pluginExtension_};
    options.allowStartup = false;

    for (const auto &dir : DirEntry::diru_DirsOnly(extensionsDir_)) {
        string folder = extensionsDir_ + sep + dir.name;
        if (!DirEntry::exists(folder + sep + "extension.ini"))
            continue;
        ExtensionInfo e;
        e.name = dir.name;
        e.folder = folder;
        e.manifest = AppManifest::load(folder, "extension.ini", keys_, options);
        e.title = e.manifest.value("name").empty() ? dir.name : e.manifest.value("name");
        e.description = e.manifest.value("description");
        e.author = e.manifest.value("author");
        e.version = e.manifest.value("version");
        string icon = e.manifest.value("icon");
        if (!icon.empty() && DirEntry::exists(folder + sep + icon))
            e.icon = folder + sep + icon;
        e.network = parseNetwork(e.manifest.value("network"));
        e.background = AppManifest::parseFlag(e.manifest.value("background"), false);
        e.provides = parseProvides(e.manifest.value("provides"));
        e.disabled =
            find_if(disabled.begin(), disabled.end(), [&](const string &n) { return n == dir.name; }) != disabled.end();
        auto problem = problems.find(dir.name);
        if (problem != problems.end())
            e.loadProblem = problem->second;
        PLOG_INFO << "[" << e.name << "] extension " << e.title << " " << e.version
                  << (e.builtForThisSystem() ? "" : " - not built for this system: " + e.manifest.problem)
                  << (e.disabled ? " - disabled" : "");
        extensions_.push_back(std::move(e));
    }
    sort(extensions_.begin(), extensions_.end(),
         [](const ExtensionInfo &a, const ExtensionInfo &b) { return lessCaseInsensitive(a.title, b.title); });
    return extensions_;
}

//*******************************
// ExtensionCatalog::find
//*******************************
ExtensionInfo *ExtensionCatalog::find(const string &name) {
    for (ExtensionInfo &e : extensions_)
        if (e.name == name)
            return &e;
    return nullptr;
}

//*******************************
// ExtensionCatalog::findProvider
//*******************************
ExtensionInfo *ExtensionCatalog::findProvider(const string &entry) {
    for (ExtensionInfo &e : extensions_)
        if (e.runnable() && e.providesEntry(entry))
            return &e;
    return nullptr;
}

//*******************************
// ExtensionCatalog::findUnavailableProvider
//*******************************
ExtensionInfo *ExtensionCatalog::findUnavailableProvider(const string &entry) {
    if (findProvider(entry) != nullptr)
        return nullptr;
    for (ExtensionInfo &e : extensions_)
        if (e.providesEntry(entry))
            return &e;
    return nullptr;
}

//*******************************
// ExtensionInfo::problem
//*******************************
const char *const ExtensionInfo::WrongAbiProblem = "built for a different AutoBleem";

ExtensionProblem ExtensionInfo::problem() const {
    if (disabled)
        return ExtensionProblem::Disabled;
    if (!builtForThisSystem())
        return ExtensionProblem::NotBuiltForThisSystem;
    if (loadProblem == WrongAbiProblem)
        return ExtensionProblem::WrongAbi;
    if (!loadProblem.empty())
        return ExtensionProblem::LoadFailed;
    return ExtensionProblem::None;
}

//*******************************
// ExtensionCatalog::readDisabled / isDisabled / setDisabled
//*******************************
vector<string> ExtensionCatalog::readDisabled() const {
    vector<string> names;
    ifstream in(disabledFile());
    string line;
    while (Strings::getlineRemoveCR(in, line)) {
        line = Strings::trim(line);
        if (!line.empty())
            names.push_back(line);
    }
    return names;
}

bool ExtensionCatalog::isDisabled(const string &name) const {
    for (const string &n : readDisabled())
        if (n == name)
            return true;
    return false;
}

void ExtensionCatalog::setDisabled(const string &name, bool disabled) {
    vector<string> names = readDisabled();
    names.erase(remove(names.begin(), names.end(), name), names.end());
    if (disabled)
        names.push_back(name);
    DirEntry::createDirs(stateDir_);
    ofstream out(disabledFile(), ios::binary | ios::trunc);
    for (const string &n : names)
        out << n << "\n";
    if (ExtensionInfo *e = find(name))
        e->disabled = disabled;
    PLOG_INFO << "[" << name << "] " << (disabled ? "disabled" : "enabled");
}

//*******************************
// ExtensionCatalog::markActive / clearActive / takeCrashed
//*******************************
void ExtensionCatalog::markActive(const string &name) {
    DirEntry::createDirs(runtimeDir_);
    ofstream out(activeFile(), ios::binary | ios::trunc);
    out << name << "\n";
}

void ExtensionCatalog::clearActive() {
    if (DirEntry::exists(activeFile()))
        DirEntry::removeFile(activeFile());
}

string ExtensionCatalog::takeCrashed() {
    // RAM first (a launcher restarted in the same boot - the Linux session), then the stick (a crash the
    // console rebooted after: rc/ab_log.sh copied the marker there; or an older launcher's marker)
    string file = activeFile();
    if (!DirEntry::exists(file))
        file = persistedActiveFile();
    if (!DirEntry::exists(file))
        return "";
    string name;
    {
        ifstream in(file);
        Strings::getlineRemoveCR(in, name);
    }
    name = Strings::trim(name);
    DirEntry::removeFile(file);
    if (name.empty())
        return "";
    PLOG_WARNING << "[" << name << "] was running when AutoBleem stopped last time - disabling it";
    setDisabled(name, true);
    return name;
}

//*******************************
// ExtensionCatalog::folderReadme
//*******************************
const char *ExtensionCatalog::folderReadme() {
    return R"(AutoBleem extensions
====================

Extensions add screens and services to the launcher itself - the AutoBleem Store is one.
Each extension is a folder of its own:

    Extensions/<name>/
        extension.ini
        bin/<platform>/<name>.so        (.dll on Windows)

AutoBleem comes with the AutoBleem Store (and on the console with PSC-Bios); installing
or updating AutoBleem puts in the version it brings. Any other extension is one you unpack
here by hand - an update leaves it alone.

The System menu (L2+R2) -> Extensions lists what is installed; Cross runs one, Triangle
switches it on or off.
)";
}

//*******************************
// ExtensionCatalog::ensureFolder
//*******************************
bool ExtensionCatalog::ensureFolder(const string &extensionsDir) {
    if (!DirEntry::createDirs(extensionsDir))
        return false;
    string readme = extensionsDir + sep + "README.txt";
    if (!DirEntry::exists(readme))
        DirEntry::writeFileIfChanged(readme, folderReadme());
    return true;
}
