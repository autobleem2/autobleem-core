//
// Downloader: one file fetched with the platform's download command - into <target>.part, checked, then
// renamed over the target - and, when asked, resumed from what an earlier attempt left in the .part. Was
// UpdateService::downloadFile; the update and the Store fetch through it (docs/store-plan.md in the launcher).
//
#pragma once

#include <cstdint>
#include <functional>
#include <string>

//******************
// DownloadRequest
//******************
struct DownloadRequest {
    std::string url;
    std::string target;  // where the file ends up; <target>.part meanwhile
    uint64_t size = 0;   // expected size; 0 = unknown
    std::string sha256;  // expected sum (lower-case hex); "" = unknown
    bool resume = false; // keep and continue a .part an earlier attempt left (the resume command must exist)
};

//******************
// Downloader
//******************
class Downloader {
public:
    // runs a command line and returns its exit status - System::runShellCommand, a fake in the tests
    using CommandRunner = std::function<int(const std::string &)>;

    enum class Result { Downloaded, AlreadyThere, Failed, WrongSize, WrongChecksum };

    // command: the platform's download command, %u (the URL) and %o (the file) in it; resumeCommand: the
    // same with its "continue" switch (abfetch --continue, curl -C -) - "" when the platform has none, and
    // a resumed request then starts over
    explicit Downloader(std::string command, std::string resumeCommand = "", CommandRunner runner = nullptr);

    // the target is already there and right (by sha256, else by size when that is all there is), or it is
    // fetched: the command into the .part, the .part checked, renamed over the target. A failed or wrong
    // download removes the .part unless the request resumes and it merely failed (the next attempt
    // continues it); a wrong size or sum always removes it - continuing a wrong file cannot make it right.
    Result fetch(const DownloadRequest &request, std::string &error);

    static std::string partPath(const std::string &target) { return target + ".part"; }
    static std::string commandFor(const std::string &commandTemplate, const std::string &url,
                                  const std::string &outPath);
    // what fetch() needs to decide a finished file is right
    static bool matches(const std::string &path, uint64_t size, const std::string &sha256);

private:
    std::string command_, resumeCommand_;
    CommandRunner runner_;
};
