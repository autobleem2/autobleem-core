//
// ExtensionCatalog: what is in Extensions/ - each folder's extension.ini read and its plugin resolved for this
// machine - plus the two lists the launcher keeps about them in System/Extensions/: the extensions the user
// (or the crash guard) disabled, and the one that was running when the launcher last died
// (docs/extensions-plan.md in the launcher).
//
#pragma once

#include "app_manifest.h"

#include <string>
#include <vector>

//******************
// ExtensionNetwork
//******************
// extension.ini's Network=: required - no use offline, the launcher refuses to run it without a network;
// optional - runs offline and does less; none (or absent) - the network does not matter to it
enum class ExtensionNetwork { None, Optional, Required };

//******************
// ExtensionInfo
//******************
struct ExtensionInfo {
    std::string name;        // the folder's name: Extensions/<name>/ - the log tag, the key in the lists
    std::string folder;      // absolute
    std::string title;       // Name= (the folder's name when the ini has none)
    std::string description; // Description=
    std::string author;      // Author=
    std::string version;     // Version=
    std::string icon;        // Icon=, absolute; "" when there is none or the file is missing
    ExtensionNetwork network = ExtensionNetwork::None;
    bool background = false; // Background=true: loaded at start-up, polled every frame
    AppManifest manifest;    // Plugin= resolved for this machine's platform keys
    bool disabled = false;   // System/Extensions/disabled.txt names it
    std::string loadProblem; // set by the runtime when loading failed or the ABI did not match

    // there is a library for this machine
    bool builtForThisSystem() const { return manifest.runnable(); }
};

//******************
// ExtensionCatalog
//******************
class ExtensionCatalog {
public:
    // extensionsDir: Extensions/; stateDir: System/Extensions/; keys: Env::appPlatformKeys();
    // pluginExtension: ".so" / ".dll" (AppManifest::pluginExtension()); runtimeDir: where the crash guard's
    // marker lives while the launcher runs - RAM (Env::getPathToRuntimeDir()); "" = stateDir
    ExtensionCatalog(std::string extensionsDir, std::string stateDir, std::vector<std::string> keys,
                     std::string pluginExtension, std::string runtimeDir = "");

    // reads every Extensions/*/extension.ini again (sorted by title); what the runtime learnt about a
    // plugin it already loaded (loadProblem) is kept for that name
    const std::vector<ExtensionInfo> &scan();
    const std::vector<ExtensionInfo> &extensions() const { return extensions_; }
    std::vector<ExtensionInfo> &extensions() { return extensions_; }
    ExtensionInfo *find(const std::string &name);

    // the user's switch (and the crash guard's): System/Extensions/disabled.txt, one name per line
    void setDisabled(const std::string &name, bool disabled);
    bool isDisabled(const std::string &name) const;

    // the crash guard: markActive() before the launcher calls into a plugin, clearActive() after; a marker
    // still there at the next start means that extension took the launcher down - takeCrashed() names it,
    // disables it and removes the marker ("" when there was none). The marker is in RAM (<runtime>/
    // extensions.active - it is written around every call, docs/quiet-stick-plan.md); a crash that ends in
    // a reboot has rc/ab_log.sh copy it to System/Extensions/.active first, which takeCrashed() reads too
    void markActive(const std::string &name);
    void clearActive();
    std::string takeCrashed();

    static ExtensionNetwork parseNetwork(const std::string &value);
    // what the installers do for a data tree: Extensions/ made, with a README.txt saying what goes there -
    // written only when it is not there (the quiet stick). False when the folder could not be made.
    static bool ensureFolder(const std::string &extensionsDir);
    static const char *folderReadme();
    std::string disabledFile() const;
    std::string activeFile() const;          // <runtime>/extensions.active
    std::string persistedActiveFile() const; // System/Extensions/.active - a crash's copy, or an older launcher's

private:
    std::vector<std::string> readDisabled() const;

    std::string extensionsDir_, stateDir_, runtimeDir_;
    std::vector<std::string> keys_;
    std::string pluginExtension_;
    std::vector<ExtensionInfo> extensions_;
};
