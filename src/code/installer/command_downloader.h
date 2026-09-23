//
// CommandDownloader: a Downloader over a shell command - the platform's download command (%u the URL,
// %o the file; curl on the console's AutoBleem kernel and the PC stick, the same the launcher's update
// check uses). What the console's own updater (the launcher's abupdate) gives InstallerJob; the PC programs
// have WinINet instead. No progress: the command reports nothing back, the bar shows a pulse.
//
#pragma once

#include "installer/installer_job.h"

#include <functional>
#include <string>

class CommandDownloader : public Downloader {
public:
    using Runner = std::function<int(const std::string &commandLine)>;
    // `runner` defaults to System::runShellCommand
    explicit CommandDownloader(std::string commandTemplate, Runner runner = Runner());
    bool fetch(const std::string &url, const std::string &destFile, const Progress &progress,
               std::string &error) override;

private:
    std::string template_;
    Runner runner_;
};
