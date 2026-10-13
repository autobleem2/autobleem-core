//
// RetroArchRunner: the one call behind `abupdate <ROOT> --retroarch install|remove [--bios] --progress <FILE>`
// (and any runner that wants the same contract). The launcher starts the runner as a child process; this
// does what the child does:
//
//     RetroArchOptions o;
//     o.root = root;                            // the PSC's USB root
//     return RetroArchRunner::run(verb,         // "install" | "remove"
//                                 o, withBios,  // --bios
//                                 progressFile, // --progress <FILE>
//                                 downloadCommand); // CommandDownloader's template, e.g. the abfetch line with %c
//
// It installs the SIGTERM handler, makes the ProgressFile (<FILE> rewritten atomically at every phase change and
// every second, <FILE>.log), runs InstallerJob::retroarchInstall / retroarchRemove with a CommandDownloader, writes
// the last line (end|ok / end|stopped / end|error|<code>) once and returns the process exit code: 0 ok,
// 3 stopped, 1 error. `install` is also the update and the resume (.part files are kept; HTTP Range).
//
#pragma once

#include "installer/installer_job.h"

#include <string>

class ProgressFile;

class RetroArchRunner {
public:
    static int run(const std::string &verb, const RetroArchOptions &options, bool withBios,
                   const std::string &progressFile, const std::string &downloadCommand);
    // the same over a downloader and a progress file you provide (the tests, the Pi runner)
    static int run(const std::string &verb, const RetroArchOptions &options, bool withBios, Downloader &downloader,
                   ProgressFile &progress, const InstallerJob::ShouldStop &shouldStop);
    // end|error|<code>
    static const char *errorCode(FailKind kind);
};
