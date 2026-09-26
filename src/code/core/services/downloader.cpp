//
// Downloader - see the header.
//
#include "downloader.h"
#include "system.h"
#include "../main.h"

#include <ableem/engine/log.h>
#include <ableem/engine/sha256.h>

using namespace std;

//*******************************
// Downloader::Downloader
//*******************************
Downloader::Downloader(string command, string resumeCommand, CommandRunner runner)
    : command_(std::move(command)), resumeCommand_(std::move(resumeCommand)), runner_(std::move(runner)) {
    if (!runner_)
        runner_ = [](const string &cmd) { return System::runShellCommand(cmd); };
}

//*******************************
// Downloader::commandFor
//*******************************
string Downloader::commandFor(const string &commandTemplate, const string &url, const string &outPath) {
    string cmd = commandTemplate;
    Strings::replaceAll(cmd, "%u", url);
    Strings::replaceAll(cmd, "%o", outPath);
    return cmd;
}

//*******************************
// Downloader::matches
//*******************************
bool Downloader::matches(const string &path, uint64_t size, const string &sha256) {
    const long long actual = DirEntry::fileSize(path);
    if (actual < 0)
        return false;
    if (size > 0 && static_cast<uint64_t>(actual) != size)
        return false;
    if (!sha256.empty())
        return ableem::Sha256::ofFile(path) == sha256;
    return true;
}

//*******************************
// Downloader::fetch
//*******************************
Downloader::Result Downloader::fetch(const DownloadRequest &request, string &error) {
    const string part = partPath(request.target);
    // already there from an earlier attempt, and provably right
    if ((request.size > 0 || !request.sha256.empty()) && DirEntry::exists(request.target) &&
        matches(request.target, request.size, request.sha256))
        return Result::AlreadyThere;

    const long long before = DirEntry::fileSize(part);
    const bool resume = request.resume && !resumeCommand_.empty() && before > 0;
    if (!resume)
        DirEntry::removeFile(part);
    const int status = runner_(commandFor(resume ? resumeCommand_ : command_, request.url, part));
    if (status != 0 || DirEntry::fileSize(part) <= 0) {
        error = "the download failed (" + to_string(status) + ")";
        PLOG_WARNING << "Download failed (" << status << "): " << request.url;
        // kept for the next attempt when it may be continued - unless this was a resume that got nowhere
        // (a server that does not do ranges makes curl -C - fail outright): then the next one starts over
        const bool gotNowhere = resume && DirEntry::fileSize(part) <= before;
        if (!request.resume || gotNowhere)
            DirEntry::removeFile(part);
        return Result::Failed;
    }
    const long long got = DirEntry::fileSize(part);
    if (request.size > 0 && static_cast<uint64_t>(got) != request.size) {
        error = "the download has " + to_string(got) + " bytes, " + to_string(request.size) + " were expected";
        PLOG_WARNING << request.url << ": " << error;
        DirEntry::removeFile(part);
        return Result::WrongSize;
    }
    if (!request.sha256.empty()) {
        const string sum = ableem::Sha256::ofFile(part);
        if (sum != request.sha256) {
            error = "the download's sha256 is " + sum + ", " + request.sha256 + " was expected";
            PLOG_WARNING << request.url << ": " << error;
            DirEntry::removeFile(part);
            return Result::WrongChecksum;
        }
    }
    if (!DirEntry::replaceFile(part, request.target)) {
        error = "cannot rename " + part;
        return Result::Failed;
    }
    return Result::Downloaded;
}
