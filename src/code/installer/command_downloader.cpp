//
// CommandDownloader - see the header.
//
#include "installer/command_downloader.h"

#include "core/services/system.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/strings.h>

#include <utility>

using namespace std;
using ableem::DirEntry;
using ableem::Strings;

CommandDownloader::CommandDownloader(string commandTemplate, Runner runner)
    : template_(move(commandTemplate)), runner_(move(runner)) {
    if (!runner_)
        runner_ = [](const string &line) { return System::runShellCommand(line); };
}

bool CommandDownloader::fetch(const string &url, const string &destFile, const Progress &progress, string &error) {
    if (template_.empty()) {
        error = "this platform has no download command";
        return false;
    }
    // to .part first, so a half download never passes for the file
    const string part = destFile + ".part";
    DirEntry::removeFile(part);
    string command = template_;
    Strings::replaceAll(command, "%u", url);
    Strings::replaceAll(command, "%o", part);
    const int status = runner_(command);
    if (status != 0 || DirEntry::fileSize(part) < 0) {
        DirEntry::removeFile(part);
        error = url + ": the download failed (exit status " + to_string(status) + ")";
        return false;
    }
    DirEntry::removeFile(destFile);
    if (!DirEntry::renameFile(part, destFile)) {
        error = "cannot write " + destFile;
        return false;
    }
    if (progress) {
        const uint64_t size = static_cast<uint64_t>(DirEntry::fileSize(destFile));
        progress(size, size);
    }
    return true;
}
