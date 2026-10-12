// ProgressFile: the runner's progress file - the format word for word, the atomic rewrite, the end line, the stop flag
#include <doctest/doctest.h>

#include "installer/progress_file.h"

#include <ableem/engine/filesystem.h>

#include "support/temp_dir.h"

#include <chrono>
#include <csignal>
#include <string>
#include <thread>

using namespace std;
using ableem::DirEntry;

TEST_CASE("the progress lines are the contract's, word for word") {
    CHECK(ProgressFile::phaseLine(2, 9, "download-cores", 1024, 4096) == "phase 2/9|download-cores|1024|4096");
    CHECK(ProgressFile::phaseLine(1, 1, "remove-files", 0, 0) == "phase 1/1|remove-files|0|0");
    CHECK(ProgressFile::endLine(true, false, "") == "end|ok");
    CHECK(ProgressFile::endLine(false, true, "") == "end|stopped");
    CHECK(ProgressFile::endLine(false, false, "no-space") == "end|error|no-space");
    CHECK(ProgressFile::endLine(false, false, "") == "end|error|other");
    CHECK(ProgressFile::exitCode(true, false) == 0);
    CHECK(ProgressFile::exitCode(false, true) == 3);
    CHECK(ProgressFile::exitCode(false, false) == 1);
}

TEST_CASE("the file is one line, rewritten at every phase change and by the heartbeat, never half written") {
    TempDir tmp("progress_file");
    const string path = tmp.at("progress");
    ProgressFile file(path, 20);
    CHECK_FALSE(DirEntry::exists(path)); // nothing before the first phase

    file.onPhase(1, 3, "check-space");
    CHECK(tmp.readFile("progress") == "phase 1/3|check-space|0|0");
    file.onProgress(10, 100);
    file.onPhase(2, 3, "download-retroarch"); // a new phase starts at 0/0
    CHECK(tmp.readFile("progress") == "phase 2/3|download-retroarch|0|0");

    // the heartbeat rewrites it with the numbers so far, without any other call
    file.onProgress(50, 200);
    string seen;
    for (int i = 0; i < 100 && seen != "phase 2/3|download-retroarch|50|200"; i++) {
        this_thread::sleep_for(chrono::milliseconds(20));
        seen = tmp.readFile("progress");
    }
    CHECK(seen == "phase 2/3|download-retroarch|50|200");
    CHECK(seen.find('\n') == string::npos);
    CHECK_FALSE(DirEntry::exists(path + ".tmp")); // renamed, not left
}

TEST_CASE("finish writes the last line once and nothing rewrites it after") {
    TempDir tmp("progress_file");
    const string path = tmp.at("progress");
    {
        ProgressFile file(path, 10);
        file.onPhase(1, 2, "unpack");
        CHECK(file.finish(false, false, "checksum") == 1);
        CHECK(tmp.readFile("progress") == "end|error|checksum");
        file.onPhase(2, 2, "merge-config"); // a late call changes nothing
        this_thread::sleep_for(chrono::milliseconds(50));
        CHECK(tmp.readFile("progress") == "end|error|checksum");
    }
    ProgressFile ok(path);
    ok.onPhase(1, 1, "remove-files");
    CHECK(ok.finish(true, false, "") == 0);
    CHECK(tmp.readFile("progress") == "end|ok");
    ProgressFile stopped(path);
    CHECK(stopped.finish(false, true, "") == 3);
    CHECK(tmp.readFile("progress") == "end|stopped");
}

TEST_CASE("the human log goes next to the file, appended") {
    TempDir tmp("progress_file");
    ProgressFile file(tmp.at("progress"));
    file.onLine("first");
    file.onLine("second");
    CHECK(tmp.readFile("progress.log") == "first\nsecond\n");
}

TEST_CASE("the stop flag is the SIGTERM flag the job checks") {
    ProgressFile::clearStop();
    const InstallerJob::ShouldStop stop = ProgressFile::shouldStop();
    CHECK_FALSE(stop());
    ProgressFile::installStopHandler();
    raise(SIGTERM);
    CHECK(stop());
    ProgressFile::clearStop();
    CHECK_FALSE(stop());
}
