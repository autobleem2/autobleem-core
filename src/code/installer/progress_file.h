//
// ProgressFile: the runner's progress file, the launcher's window onto a RetroArch install/remove that runs as
// a child process (abupdate --retroarch ... --progress <FILE>, autobleem-retroarch ... --progress <FILE>).
// An InstallListener, so any job in this folder reports through it; plus the SIGTERM flag the job checks.
//
// The file is UTF-8, ONE line, rewritten atomically (<FILE>.tmp, then rename), at every phase change and at
// least every second while the run lives (a heartbeat thread, so a long download without a callback still
// shows life):
//
//     phase <i>/<n>|<key>|<done>|<total>
//
// <key> is the title the job gives onPhase (the RetroArch-only jobs use the contract's keys as titles).
// <total> 0 = unknown. When the run ends finish() replaces the line, once, with the LAST content:
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

    // the final line; stops the heartbeat first, so nothing rewrites it. Returns the process exit code.
    int finish(bool ok, bool stopped, const std::string &errorCode);
    static int exitCode(bool ok, bool stopped);

    // "phase 2/9|download-cores|1024|4096" / "end|error|no-space" - the format, word for word
    static std::string phaseLine(int index, int total, const std::string &key, uint64_t done, uint64_t size);
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
};
