//
// RetroArchRunner - see the header.
//
#include "installer/retroarch_runner.h"

#include "installer/command_downloader.h"
#include "installer/progress_file.h"

using namespace std;

//*******************************
// RetroArchRunner::errorCode
//*******************************
const char *RetroArchRunner::errorCode(FailKind kind) {
    switch (kind) {
    case FailKind::NoNetwork:
        return "no-network";
    case FailKind::NoSpace:
        return "no-space";
    case FailKind::Download:
        return "download";
    case FailKind::Checksum:
        return "checksum";
    case FailKind::AptProvided:
        return "apt-provided";
    default:
        return "other";
    }
}

//*******************************
// RetroArchRunner::run
//*******************************
int RetroArchRunner::run(const string &verb, const RetroArchOptions &options, bool withBios, Downloader &downloader,
                         ProgressFile &progress, const InstallerJob::ShouldStop &shouldStop) {
    RetroArchResult result;
    progress.setStrict(true); // every phase of this runner gives a real total
    if (verb == "install") {
        result = InstallerJob::retroarchInstall(options, withBios, downloader, progress, shouldStop);
    } else if (verb == "remove") {
        result = InstallerJob::retroarchRemove(options, withBios, progress, shouldStop);
    } else {
        result.error = "unknown command " + verb;
        result.kind = FailKind::Other;
    }
    if (!result.ok)
        progress.onLine(result.stopped ? "stopped" : "failed: " + result.error);
    return progress.finish(result.ok, result.stopped, errorCode(result.kind));
}

int RetroArchRunner::run(const string &verb, const RetroArchOptions &options, bool withBios,
                         const string &progressFile, const string &downloadCommand) {
    ProgressFile::installStopHandler();
    ProgressFile progress(progressFile);
    CommandDownloader downloader(downloadCommand);
    return run(verb, options, withBios, downloader, progress, ProgressFile::shouldStop());
}
