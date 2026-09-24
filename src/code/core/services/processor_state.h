//
// ProcessorState: what the scanner processors already ran on, so a scan does not start them again on something
// that has not changed - <state>/processors.state (docs/scanner-processors-plan.md in the launcher, "Not running
// twice").
//
#pragma once

#include <map>
#include <string>
#include <vector>

//******************
// ProcessorResult
//******************
enum class ProcessorResult {
    Ok,         // #DONE and exit 0
    Failed,     // #ERROR, a crash, a non-zero exit, no #DONE, the timeout: not tried again until something changes
    Interrupted // stopped by the launcher (a game launch, power off): tried again on the next scan
};

//******************
// ProcessorState
//******************
// One line per (processor, kind, target):
//   <processor>\t<version>\t<kind>\t<target>\t<digest>\t<ok|failed|interrupted>
// The target is relative to the tree the scan gave (Games/ or roms/): "" for the tree itself. The digest is
// over file names and sizes - never modification times, the console has no battery clock - and leaves out
// what the launcher itself writes into a game folder (Game.ini, pcsx.cfg, the .m3u), '.'-files, *.part and the
// !SaveStates/!MemCards folders, so a scan's own writes do not count as a change.
class ProcessorState {
public:
    explicit ProcessorState(std::string file) : file_(std::move(file)) {}

    bool load(); // a missing file is an empty state (true)
    bool save() const;

    // the processor already settled this target at this version, and the target has not changed since: its
    // last run ended ok or failed (an interrupted one is owed another go)
    bool isSettled(const std::string &processor, const std::string &version, const std::string &kind,
                   const std::string &target, const std::string &digest) const;
    void record(const std::string &processor, const std::string &version, const std::string &kind,
                const std::string &target, const std::string &digest, ProcessorResult result);
    // everything about one processor ("Run again on everything")
    void forget(const std::string &processor);
    bool empty() const { return entries_.empty(); }

    // a file (name and size) or a folder (every file under it, as relative paths and sizes), hashed
    static std::string digest(const std::string &path);
    // the files under a folder the digest covers: "<relative path>" -> size
    static std::map<std::string, long long> files(const std::string &dir);
    // a file the digest (and the processors' Match) never looks at
    static bool ignoredName(const std::string &name);

    static const char *resultName(ProcessorResult result);

private:
    struct Line {
        std::string version, digest;
        ProcessorResult result = ProcessorResult::Ok;
    };
    static std::string key(const std::string &processor, const std::string &kind, const std::string &target);
    std::string file_;
    std::map<std::string, Line> entries_;
};
