//
// ProcessorCatalog: what is in System/Processors/ - each folder's processor.ini read and its program resolved for
// this machine (docs/scanner-processors-plan.md in the launcher).
//
#pragma once

#include "app_manifest.h"

#include <string>
#include <vector>

//******************
// ProcessorKind
//******************
// processor.ini's Kinds=: what a processor can be given. games-folder / roms-folder: the whole tree, once per
// scan, before anything else (a preprocessor); ps1 / rom: one game folder or one ROM file at a time
enum class ProcessorKind { GamesFolder, RomsFolder, Ps1, Rom };

//******************
// ProcessorSequence
//******************
// the two chains the user sorts: what runs on the PS1 games (games-folder + ps1) and on the ROMs
// (roms-folder + rom)
enum class ProcessorSequence { Ps1, Roms };

//******************
// ProcessorInfo
//******************
struct ProcessorInfo {
    std::string name;        // the folder's name: System/Processors/<name>/ - the key in sequence.ini and the state
    std::string folder;      // absolute
    std::string title;       // Name= (the folder's name when the ini has none)
    std::string description; // Description=
    std::string author;      // Author=
    std::string version;     // Version=
    std::vector<ProcessorKind> kinds;
    std::vector<std::string> match;   // Match=: file patterns (* and ?, no case); empty = every file
    std::vector<std::string> systems; // Systems=: RetroArch system folder names; empty = all
    int order = 100;                  // Order=: where a new processor lands in a sequence
    int timeoutSeconds = 600;         // Timeout=: silence before it is killed; 0 = never
    bool modifies = true;             // Modifies=false: only reads - never stopped for a launch
    AppManifest manifest;             // Exec= resolved for this machine's platform keys

    bool builtForThisSystem() const { return manifest.runnable(); }
    bool has(ProcessorKind kind) const;
    bool belongsTo(ProcessorSequence sequence) const;
    // a file name (not a path) one of the Match patterns takes; true for any file when there is no Match
    bool matchesFile(const std::string &fileName) const;
    bool wantsSystem(const std::string &system) const;
};

//******************
// ProcessorCatalog
//******************
class ProcessorCatalog {
public:
    // processorsDir: System/Processors; keys: Env::appPlatformKeys()
    ProcessorCatalog(std::string processorsDir, std::vector<std::string> keys);

    // reads every <dir>/*/processor.ini again, sorted by name; folders starting with '.' are skipped
    const std::vector<ProcessorInfo> &scan();
    const std::vector<ProcessorInfo> &processors() const { return processors_; }
    const ProcessorInfo *find(const std::string &name) const;

    // the Match patterns of every processor that can run here - what makes a file worth watching for
    // in the games tree (a dropped .zip is not a game file, but it is work for unzip)
    std::vector<std::string> watchPatterns() const;

    // "*.zip" against "Crash.ZIP": * any run, ? one character, no case
    static bool globMatch(const std::string &name, const std::string &pattern);
    static std::vector<ProcessorKind> parseKinds(const std::string &value);
    static const char *kindName(ProcessorKind kind); // "games-folder", "roms-folder", "ps1", "rom"
    // a value with its trailing "  ; comment" (or "# comment") removed - a comment needs a blank before it
    static std::string stripComment(const std::string &value);

    static ProcessorInfo load(const std::string &folder, const std::vector<std::string> &keys);

    // what the installers do for a data tree: the folder made, and a README.txt in it saying what goes there -
    // written only when it is not there (the user may have edited it, and the quiet stick writes nothing it
    // does not have to). False when the folder could not be made.
    static bool ensureFolder(const std::string &processorsDir);
    static const char *folderReadme(); // the README.txt's text

private:
    std::string dir_;
    std::vector<std::string> keys_;
    std::vector<ProcessorInfo> processors_;
};
