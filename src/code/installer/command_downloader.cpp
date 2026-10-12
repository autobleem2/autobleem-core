//
// CommandDownloader - see the header.
//
#include "installer/command_downloader.h"

#include "core/services/system.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/sha256.h>
#include <ableem/engine/strings.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

using namespace std;
using ableem::DirEntry;
using ableem::Sha256;
using ableem::Strings;

namespace {
const unsigned RetryDelayMs = 2000;

void realSleep(unsigned ms) {
    this_thread::sleep_for(chrono::milliseconds(ms));
}
} // namespace

CommandDownloader::CommandDownloader(string commandTemplate, Runner runner)
    : template_(move(commandTemplate)), runner_(move(runner)) {
    if (!runner_)
        runner_ = [](const string &line) { return System::runShellCommand(line); };
    network_ = []() { return System::hasDefaultRoute(); };
    sleeper_ = realSleep;
}

bool CommandDownloader::expect(uint64_t size, const string &sha256) {
    expectedSize_ = size;
    expectedSha_ = sha256;
    return true;
}

void CommandDownloader::setRetry(int retries, unsigned networkWaitMs, unsigned pollMs) {
    retries_ = retries;
    networkWaitMs_ = networkWaitMs;
    pollMs_ = pollMs;
}

void CommandDownloader::setNetwork(NetworkUp network, Sleeper sleeper) {
    network_ = move(network);
    sleeper_ = move(sleeper);
}

//*******************************
// CommandDownloader::waitForNetwork
//*******************************
// false when no default route came up within the wait, or the progress callback asked to stop
bool CommandDownloader::waitForNetwork(const Progress &progress) {
    for (unsigned waited = 0; !network_(); waited += 1000) {
        if (waited >= networkWaitMs_ || (progress && !progress(0, 0)))
            return false;
        sleeper_(1000);
    }
    return true;
}

//*******************************
// CommandDownloader::fetch
//*******************************
bool CommandDownloader::fetch(const string &url, const string &destFile, const Progress &progress, string &error) {
    const uint64_t wantSize = expectedSize_;
    const string wantSha = expectedSha_;
    expectedSize_ = 0;
    expectedSha_.clear();
    if (template_.empty()) {
        error = "this platform has no download command";
        return false;
    }
    // to .part first, so a half download never passes for the file; kept when the command can continue it
    const string part = destFile + ".part";
    const bool resumable = template_.find("%c") != string::npos;
    if (!resumable)
        DirEntry::removeFile(part);
    long long have = DirEntry::fileSize(part);
    if (wantSize > 0 && have > 0 && static_cast<uint64_t>(have) > wantSize) {
        DirEntry::removeFile(part); // longer than the file can be: not a part of it
        have = -1;
    }

    string failure;
    bool complete = wantSize > 0 && have > 0 && static_cast<uint64_t>(have) == wantSize; // nothing left to ask for
    bool stopped = false;
    for (int attempt = 0; !complete && attempt <= retries_; attempt++) {
        if (attempt > 0) {
            sleeper_(RetryDelayMs);
            if (progress && !progress(0, wantSize)) {
                stopped = true;
                break;
            }
        }
        if (!waitForNetwork(progress)) {
            if (progress && !progress(0, wantSize))
                stopped = true;
            else
                failure = string(ErrNoNetwork) + ": " + url + " cannot be reached";
            break;
        }
        have = DirEntry::fileSize(part);
        string command = template_;
        Strings::replaceAll(command, "%c", resumable && have > 0 ? "--continue" : "");
        Strings::replaceAll(command, "%u", url);
        Strings::replaceAll(command, "%o", part);

        // the command runs on its own thread so the .part's growth can be reported from this one
        atomic<int> status(-1);
        atomic<bool> done(false);
        thread runner([&]() {
            status = runner_(command);
            done = true;
        });
        while (!done) {
            for (unsigned waited = 0; waited < pollMs_ && !done; waited += 10)
                this_thread::sleep_for(chrono::milliseconds(10));
            const long long size = DirEntry::fileSize(part);
            if (progress && !done && !progress(size > 0 ? static_cast<uint64_t>(size) : 0, wantSize))
                stopped = true; // the command cannot be interrupted: it ends, and the part stays for the resume
        }
        runner.join();
        if (status == 0 && DirEntry::fileSize(part) >= 0) {
            complete = true;
            break;
        }
        failure = url + ": the download failed (exit status " + to_string(status.load()) + ")";
        if (stopped)
            break;
    }

    if (!complete) {
        if (!resumable)
            DirEntry::removeFile(part);
        error = stopped ? "Stopped" : failure;
        return false;
    }

    const long long size = DirEntry::fileSize(part);
    if ((wantSize > 0 && static_cast<uint64_t>(size) != wantSize) ||
        (!wantSha.empty() && Sha256::ofFile(part) != wantSha)) {
        DirEntry::removeFile(part); // a part that is wrong cannot be continued
        error = string(ErrChecksum) + ": " + url + " does not match what the site published";
        return false;
    }
    DirEntry::removeFile(destFile);
    if (!DirEntry::renameFile(part, destFile)) {
        error = "cannot write " + destFile;
        return false;
    }
    if (progress)
        progress(static_cast<uint64_t>(size), static_cast<uint64_t>(size));
    return true;
}
