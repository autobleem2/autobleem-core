//
// ProgressFile: the runner's progress file, the launcher's window onto a RetroArch install/remove that runs as
// a child process (abupdate --retroarch ... --progress <FILE>, autobleem-retroarch ... --progress <FILE>).
// An InstallListener, so any job in this folder reports through it; plus the SIGTERM flag the job checks.
//
// The file is UTF-8, ONE line, rewritten atomically (<FILE>.tmp, then rename), at every phase change and at
// least every second while the run lives (a heartbeat thread, so a long download without a callback still
// shows life):
//
//     phase <i>/<n>|<key>|<done>|<total>|<unit>|<item>|<item_i>|<item_n>
//
// <key> is the title the job gives onPhase (the RetroArch-only jobs use the contract's keys as titles).
// <unit> bytes|files|items is what done/total count, <item> the package the phase is on and <item_i>/<item_n>
// which one of how many. A job that reports no item (the full installs) still writes the old 4-field line.
// In strict mode (RetroArch runs: RetroArchRunner) no line is written while total is 0: a phase's first line
// comes with its first real total, and every phase gives one. When the run ends finish() replaces the line, once, with the LAST content:
//
//     end|ok        end|stopped        end|error|<code>
//
// <code>: no-network, no-space, download, checksum, apt-provided, other. The process exit code is exitCode():
// 0 ok, 3 stopped, 1 error. The human log goes to <FILE>.log (onLine appends, free text).
//
#pragma once

#include "installer/installer_job.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class ProgressFile : public InstallListener {
public:
    // `path` empty = no file (the listener still counts); `heartbeatMs` is the rewrite period while a phase runs
    explicit ProgressFile(std::string path, unsigned heartbeatMs = 1000);
    ~ProgressFile() override;
    ProgressFile(const ProgressFile &) = delete;
    ProgressFile &operator=(const ProgressFile &) = delete;

    void onPhase(int index, int total, const std::string &title) override;
    void onProgress(uint64_t done, uint64_t total) override;
    void onLine(const std::string &line) override;
    void onItem(const std::string &name, int index, int count, const std::string &unit) override;
    // strict: never write a line whose total is 0 (see the format above)
    void setStrict(bool strict);

    // the final line; stops the heartbeat first, so nothing rewrites it. Returns the process exit code.
    int finish(bool ok, bool stopped, const std::string &errorCode);
    static int exitCode(bool ok, bool stopped);

    // "phase 2/9|download-cores|1024|4096" / "end|error|no-space" - the format, word for word
    static std::string phaseLine(int index, int total, const std::string &key, uint64_t done, uint64_t size);
    // the same with the unit and the item: "phase 2/9|download-cores|1024|4096|bytes|cores-psc-20261003.tar.gz|1|1"
    static std::string phaseLine(int index, int total, const std::string &key, uint64_t done, uint64_t size,
                                 const std::string &unit, const std::string &item, int itemIndex, int itemCount);
    static std::string endLine(bool ok, bool stopped, const std::string &errorCode);

    // the SIGTERM flag: installStopHandler() makes SIGTERM (and SIGINT) set it, the job's ShouldStop reads it.
    // requestStop()/clearStop() are for tests.
    static void installStopHandler();
    static bool stopRequested();
    static void requestStop();
    static void clearStop();
    static InstallerJob::ShouldStop shouldStop();

private:
    void write(const std::string &line);
    void writeCurrent(); // the phase line for the current state (under the lock), or nothing yet
    void heartbeat();
    std::string path_;
    unsigned heartbeatMs_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    bool running_ = false;
    bool ended_ = false;
    int index_ = 0, count_ = 0;
    std::string key_;
    uint64_t done_ = 0, total_ = 0;
    std::string unit_, item_;
    int itemIndex_ = 0, itemCount_ = 0;
    bool strict_ = false;
    bool dirty_ = false; // the phase or item changed and no line says so yet
};
