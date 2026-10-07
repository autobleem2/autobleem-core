//
// RaJobService: the runner's command line, its progress line, the inspection of what is installed, and a whole job
// against the FAKE runner (tests/data/fake_ra_runner.sh, which follows the contract in ra_job_service.h) - progress,
// success, a failure with an exit code, and a stop.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"

#include "core/services/ra_job_service.h"

#include <ableem/engine/filesystem.h>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using ableem::DirEntry;
using std::string;

namespace {

const char *RetroArchJson = R"({
  "version": "v1.22.2-6",
  "armhf": {"name": "retroarch-armhf.tar.gz", "size": 3, "sha256": "RA32", "url": "http://site/ra.tar.gz"}
})";

// the fake runner, as a job command: `sh script %a%B --progress "%p"`, with its environment up front
string fakeCommand(const string &env = "") {
    return env + " sh \"" + std::string(AB_TEST_DATA_DIR) + "/fake_ra_runner.sh\" %a%B --progress \"%p\"";
}

RaJobService::Config jobConfig(const TempDir &tmp, const string &env = "FAKE_RA_DELAY=0.02") {
    RaJobService::Config c;
    c.jobCommand = fakeCommand(env);
    c.usbRoot = tmp.path();
    c.workDir = tmp.path() + "/System/RaJob";
    c.retroarchDir = tmp.path() + "/RetroArch";
    c.binaries = {tmp.path() + "/usr/local/bin/retroarch", "/usr/bin/retroarch-not-here"};
    return c;
}

// polls until the job is over (or 20 s), the way the panel does once a frame
RaJobService::Status finish(RaJobService &service) {
    for (int i = 0; i < 1000; i++) {
        const RaJobService::Status status = service.poll();
        if (status.over())
            return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return service.poll();
}

RaJobService::Inspection inspect(RaJobService &service) {
    service.startInspect();
    for (int i = 0; i < 500; i++) {
        const RaJobService::Inspection found = service.pollInspect();
        if (found.ready)
            return found;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return service.pollInspect();
}

} // namespace

TEST_CASE("RaJobService: the command line's placeholders") {
    CHECK(RaJobService::commandFor("run %a%B --progress \"%p\" --log %l --root %R --dir %r",
                                   RaJobService::Action::Install, "/p", "/l", "/media", "/media/bin") ==
          "run install --progress \"/p\" --log /l --root /media --dir /media/bin");
    CHECK(RaJobService::commandFor("run %a%B", RaJobService::Action::Update, "", "", "", "") == "run update");
    CHECK(RaJobService::commandFor("run %a%B", RaJobService::Action::Remove, "", "", "", "") == "run remove");
    CHECK(RaJobService::commandFor("run %a%B", RaJobService::Action::RemoveWithBios, "", "", "", "") ==
          "run remove --bios");
}

TEST_CASE("RaJobService: the progress line") {
    int step = -1, steps = -1;
    string title;
    uint64_t done = 0, total = 0;

    SUBCASE("the whole line, with and without the prefix, and a title with spaces") {
        REQUIRE(RaJobService::parseProgress("phase 2/5|Downloading cores|1048576|734003200\n", step, steps, title,
                                            done, total));
        CHECK(step == 2);
        CHECK(steps == 5);
        CHECK(title == "Downloading cores");
        CHECK(done == 1048576);
        CHECK(total == 734003200);
        REQUIRE(RaJobService::parseProgress("0/3|Preparing|0|0", step, steps, title, done, total));
        CHECK(step == 0);
        CHECK(total == 0);
    }
    SUBCASE("a half-written or odd line does not parse") {
        CHECK_FALSE(RaJobService::parseProgress("", step, steps, title, done, total));
        CHECK_FALSE(RaJobService::parseProgress("phase 2/5|Downloading", step, steps, title, done, total));
        CHECK_FALSE(RaJobService::parseProgress("phase 2/5|T|12|", step, steps, title, done, total));
        CHECK_FALSE(RaJobService::parseProgress("phase two/5|T|1|2", step, steps, title, done, total));
        CHECK_FALSE(RaJobService::parseProgress("phase 6/5|T|1|2", step, steps, title, done, total));
        CHECK_FALSE(RaJobService::parseProgress("phase 2/5|T|1x|2", step, steps, title, done, total));
        CHECK_FALSE(RaJobService::parseProgress("phase 2|T|1|2", step, steps, title, done, total));
    }
}

TEST_CASE("RaJobService: the exit codes and the whole job's fraction") {
    CHECK(RaJobService::failureFor(0) == RaJobService::Failure::None);
    CHECK(RaJobService::failureFor(3) == RaJobService::Failure::NoNetwork);
    CHECK(RaJobService::failureFor(4) == RaJobService::Failure::NoSpace);
    CHECK(RaJobService::failureFor(1) == RaJobService::Failure::Other);
    CHECK(RaJobService::failureFor(127) == RaJobService::Failure::Other);

    RaJobService::Status status;
    CHECK(status.fraction() < 0); // nothing reported yet
    status.steps = 4;
    status.step = 1;
    CHECK(status.fraction() == doctest::Approx(0.0));
    status.step = 3;
    status.done = 50;
    status.total = 100;
    CHECK(status.fraction() == doctest::Approx(0.625));
    status.total = 0; // a phase with no total: its start
    CHECK(status.fraction() == doctest::Approx(0.5));
    status.step = 4;
    status.done = 100;
    status.total = 100;
    CHECK(status.fraction() == doctest::Approx(1.0));
}

TEST_CASE("RaJobService: inspecting what is installed") {
    TempDir tmp("rajob_inspect");
    RaJobService::Config c = jobConfig(tmp);
    c.installedVersion = [] { return string("v1.22.2-3"); };

    SUBCASE("nothing there: not installed, no size") {
        RaJobService service;
        service.configure(c);
        const RaJobService::Inspection found = inspect(service);
        CHECK(found.ready);
        CHECK_FALSE(found.installed);
        CHECK(found.version.empty());
        CHECK(found.sizeBytes == 0);
    }

    SUBCASE("installed: the version, and the size of what Remove deletes - not of what it keeps") {
        tmp.makeSubDir("usr/local/bin");
        tmp.writeFile("usr/local/bin/retroarch", string(1000, 'x'));
        tmp.makeSubDir("RetroArch/cores");
        tmp.writeFile("RetroArch/cores/a_libretro.so", string(2000, 'x'));
        tmp.makeSubDir("RetroArch/info/deep");
        tmp.writeFile("RetroArch/info/deep/a.info", string(30, 'x'));
        tmp.makeSubDir("RetroArch/roms");
        tmp.writeFile("RetroArch/roms/game.nes", string(5000, 'x')); // kept
        tmp.makeSubDir("RetroArch/saves");
        tmp.writeFile("RetroArch/saves/game.srm", string(700, 'x')); // kept
        RaJobService service;
        service.configure(c);
        const RaJobService::Inspection found = inspect(service);
        CHECK(found.installed);
        CHECK_FALSE(found.systemPackage);
        CHECK(found.version == "v1.22.2-3");
        CHECK(found.sizeBytes == 3030);
    }

    SUBCASE("only a distribution's /usr/bin program is installed but not ours to remove") {
        // a path under /usr/bin that exists on this host: a shell is always there
        c.binaries = {"/usr/bin/env"};
        RaJobService service;
        service.configure(c);
        if (DirEntry::exists("/usr/bin/env")) {
            const RaJobService::Inspection found = inspect(service);
            CHECK(found.installed);
            CHECK(found.systemPackage);
            CHECK(found.sizeBytes == 0);
        }
    }

    SUBCASE("the catalog: a newer version is an update, the same one is not, no network is not asked") {
        tmp.makeSubDir("usr/local/bin");
        tmp.writeFile("usr/local/bin/retroarch", "x");
        c.repoUrl = "http://site";
        c.catalog = "rpi/retroarch/latest.json";
        c.arch = "armhf";
        c.fetchCommand = "fetch %u %o";
        std::vector<string> fetched;
        const string json = RetroArchJson;
        auto fetch = [&](const string &commandLine) {
            fetched.push_back(commandLine);
            const size_t sp = commandLine.rfind(' ');
            std::ofstream(commandLine.substr(sp + 1), std::ios::binary) << json;
            return 0;
        };
        {
            RaJobService service(nullptr, fetch);
            service.configure(c);
            const RaJobService::Inspection found = inspect(service);
            CHECK(found.latestVersion == "v1.22.2-6");
            CHECK(found.updateAvailable()); // installed v1.22.2-3
            REQUIRE(fetched.size() == 1);
            CHECK(fetched[0].find("http://site/rpi/retroarch/latest.json") != string::npos);
        }
        {
            c.installedVersion = [] { return string("v1.22.2-6"); };
            RaJobService service(nullptr, fetch);
            service.configure(c);
            CHECK_FALSE(inspect(service).updateAvailable());
        }
        {
            fetched.clear();
            c.networkUp = [] { return false; };
            RaJobService service(nullptr, fetch);
            service.configure(c);
            const RaJobService::Inspection found = inspect(service);
            CHECK_FALSE(found.networkUp);
            CHECK(found.latestVersion.empty());
            CHECK(fetched.empty());
        }
        {
            c.networkUp = nullptr;
            c.installedVersion = [] { return string(); }; // no stamp: nothing to compare
            RaJobService service(nullptr, fetch);
            service.configure(c);
            CHECK_FALSE(inspect(service).updateAvailable());
        }
    }
}

#ifndef _WIN32
TEST_CASE("RaJobService: a job against the fake runner") {
    TempDir tmp("rajob_job");

    SUBCASE("no command: unsupported, nothing starts") {
        RaJobService service;
        RaJobService::Config c = jobConfig(tmp);
        c.jobCommand.clear();
        service.configure(c);
        CHECK_FALSE(service.supported());
        CHECK_FALSE(service.start(RaJobService::Action::Install));
        CHECK(service.poll().phase == RaJobService::Phase::Idle);
    }

    SUBCASE("an install: phases, bytes and the log, then success") {
        RaJobService service;
        service.configure(jobConfig(tmp));
        REQUIRE(service.start(RaJobService::Action::Install));
        CHECK_FALSE(service.start(RaJobService::Action::Update)); // one at a time
        int highest = 0;
        string seenTitle;
        RaJobService::Status status;
        for (int i = 0; i < 1000; i++) {
            status = service.poll();
            highest = std::max(highest, status.step);
            if (status.step == 3)
                seenTitle = status.title;
            if (status.over())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(status.phase == RaJobService::Phase::Succeeded);
        CHECK(status.exitCode == 0);
        CHECK(status.failure == RaJobService::Failure::None);
        CHECK(status.steps == 4);
        CHECK(highest == 4);
        CHECK(seenTitle == "Downloading cores");
        CHECK(status.fraction() == doctest::Approx(1.0));
        REQUIRE_FALSE(status.log.empty());
        CHECK(status.log.back() == "done");
        CHECK_FALSE(service.busy());
        CHECK(service.start(RaJobService::Action::Remove)); // free again
        CHECK(finish(service).phase == RaJobService::Phase::Succeeded);
    }

    SUBCASE("a remove with the BIOS has its second phase") {
        RaJobService service;
        service.configure(jobConfig(tmp, "FAKE_RA_DELAY=0.01 FAKE_RA_TICKS=2"));
        REQUIRE(service.start(RaJobService::Action::RemoveWithBios));
        const RaJobService::Status status = finish(service);
        CHECK(status.phase == RaJobService::Phase::Succeeded);
        CHECK(status.steps == 2);
        CHECK(status.title == "Removing BIOS files");
    }

    SUBCASE("a failure: the exit code, what it means, the runner's last words") {
        RaJobService service;
        service.configure(jobConfig(tmp, "FAKE_RA_DELAY=0.01 FAKE_RA_FAIL=4"));
        REQUIRE(service.start(RaJobService::Action::Install));
        const RaJobService::Status status = finish(service);
        CHECK(status.phase == RaJobService::Phase::Failed);
        CHECK(status.exitCode == 4);
        CHECK(status.failure == RaJobService::Failure::NoSpace);
        REQUIRE_FALSE(status.log.empty());
        CHECK(status.log.back().find("fake failure") == 0);
        CHECK(status.step == 2);
    }

    SUBCASE("a runner that cannot start is a failure, not a hang") {
        RaJobService service;
        RaJobService::Config c = jobConfig(tmp);
        c.jobCommand = "/nonexistent/runner %a";
        service.configure(c);
        REQUIRE(service.start(RaJobService::Action::Install));
        const RaJobService::Status status = finish(service);
        CHECK(status.phase == RaJobService::Phase::Failed);
        CHECK(status.failure == RaJobService::Failure::Other);
    }

    SUBCASE("a stop: SIGTERM reaches the runner, the job ends Stopped, and the next start works") {
        RaJobService service;
        service.configure(jobConfig(tmp, "FAKE_RA_DELAY=0.1 FAKE_RA_TICKS=100"));
        REQUIRE(service.start(RaJobService::Action::Install));
        RaJobService::Status status;
        for (int i = 0; i < 500 && status.step < 1; i++) { // wait until it is really going
            status = service.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        REQUIRE(status.step >= 1);
        service.stop();
        status = finish(service);
        CHECK(status.phase == RaJobService::Phase::Stopped);
        CHECK(status.failure == RaJobService::Failure::None);
        CHECK_FALSE(status.stopping);
        CHECK_FALSE(service.busy());

        service.configure(jobConfig(tmp, "FAKE_RA_DELAY=0.01 FAKE_RA_TICKS=1"));
        REQUIRE(service.start(RaJobService::Action::Install));
        CHECK(finish(service).phase == RaJobService::Phase::Succeeded);
    }

    SUBCASE("a runner that stops itself with the contract's code is Stopped too") {
        RaJobService service;
        RaJobService::Config c = jobConfig(tmp);
        c.jobCommand = "exit 5 #";
        service.configure(c);
        REQUIRE(service.start(RaJobService::Action::Install));
        CHECK(finish(service).phase == RaJobService::Phase::Stopped);
    }
}
#endif
