//
// CommandDownloader: a Downloader over a shell command - the platform's download command (%u the URL,
// %o the file; curl on the console's AutoBleem kernel and the PC stick, the same the launcher's update
// check uses). What the console's own updater (the launcher's abupdate) gives InstallerJob; the PC programs
// have WinINet instead.
//
// The command writes <dest>.part, which is KEPT when the download does not complete. %c in the template is
// replaced by "--continue" when a .part is there to continue (abfetch asks only for the rest, an HTTP Range
// request) and by nothing otherwise - a template without %c starts over each time:
//     abfetch %c --connect-timeout 20 --stall-timeout 60 -o "%o" "%u"
// Progress: the .part's size is polled while the command runs (done = its size, total = what expect() was told,
// else 0). expect(size, sha256) also lets the downloader skip the command for a .part that is already whole and
// check the finished file (a mismatch deletes the .part: ErrChecksum). A failed command is retried; when the
// machine has no default route it waits for one (the network may come back) and gives up with ErrNoNetwork.
//
#pragma once

#include "installer/installer_job.h"

#include <functional>
#include <string>

class CommandDownloader : public Downloader {
public:
    using Runner = std::function<int(const std::string &commandLine)>;
    using NetworkUp = std::function<bool()>;
    using Sleeper = std::function<void(unsigned ms)>;
    // Without `runner` the command runs through System::runShellCommand(line, cancelled): when the progress
    // callback asks to stop (SIGTERM -> ProgressFile), the command and its process group are killed within about
    // a second and fetch() returns "Stopped" with the .part kept. A `runner` of a test cannot be interrupted.
    explicit CommandDownloader(std::string commandTemplate, Runner runner = Runner());
    bool fetch(const std::string &url, const std::string &destFile, const Progress &progress,
               std::string &error) override;
    bool expect(uint64_t size, const std::string &sha256) override;
    // HEAD for the size: the template's %c slot takes "--head" (abfetch --head -o FILE URL writes the
    // Content-Length number to FILE); a template without %c cannot ask and says false
    bool contentLength(const std::string &url, const std::string &scratchFile, uint64_t &size) override;

    // retries after a failed command, how long to wait for a network that is gone, how often the .part is
    // polled; `network` (default System::hasDefaultRoute) and `sleeper` (default a real sleep) for the tests
    void setRetry(int retries, unsigned networkWaitMs, unsigned pollMs = 250);
    void setNetwork(NetworkUp network, Sleeper sleeper);

private:
    bool waitForNetwork(const Progress &progress);
    std::string template_;
    Runner runner_;
    NetworkUp network_;
    Sleeper sleeper_;
    int retries_ = 3;
    unsigned networkWaitMs_ = 120000, pollMs_ = 250;
    uint64_t expectedSize_ = 0;
    std::string expectedSha_;
};
