//
// ProcessorRunner: one processor run, over a scripted fake process (docs/scanner-processors-plan.md in the
// launcher).
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/main.h"
#include "core/services/processor_runner.h"

#include <chrono>
#include <thread>

using namespace std;

namespace {

// prints its lines, then returns `code`; with `waitForStop` it asks shouldStop until told to, and says -2
class FakeProcess : public ProcessorProcess {
public:
    vector<string> lines;
    int code = 0;
    bool waitForStop = false;
    vector<vector<string>> calls;
    vector<pair<string, string>> lastEnv;
    string lastCwd;

    int run(const string &, const vector<string> &args, const string &cwd, const vector<pair<string, string>> &env,
            const System::OutputLine &onLine, const function<bool()> &shouldStop) override {
        calls.push_back(args);
        lastEnv = env;
        lastCwd = cwd;
        for (const string &l : lines) {
            if (onLine)
                onLine(l.compare(0, 2, "! ") == 0 ? l.substr(2) : l, l.compare(0, 2, "! ") == 0);
        }
        if (waitForStop) {
            auto until = chrono::steady_clock::now() + chrono::seconds(10);
            while (chrono::steady_clock::now() < until) {
                if (shouldStop && shouldStop())
                    return -2;
                this_thread::sleep_for(chrono::milliseconds(20));
            }
        }
        return code;
    }
};

ProcessorInfo unzip(const TempDir &tmp) {
    ProcessorInfo p;
    p.name = "unzip";
    p.version = "1.0.0";
    p.folder = tmp.at("Processors/unzip");
    p.manifest.program = p.folder + "/bin/psc/unzip";
    p.timeoutSeconds = 0;
    return p;
}

string envValue(const vector<pair<string, string>> &env, const string &name) {
    for (const auto &kv : env) {
        if (kv.first == name)
            return kv.second;
    }
    return "<none>";
}

} // namespace

TEST_CASE("ProcessorRunner: a good run, its progress, its log and its environment") {
    TempDir tmp("runner_ok");
    FakeProcess fake;
    fake.lines = {"#Starting - Unzip V1.0.0",
                  "#Unpacking Crash.zip",
                  "1/2",
                  "40",
                  "! inflating",
                  "100",
                  "#WARN - kept the zip",
                  "#DONE"};
    ProcessorRunner runner(fake, {tmp.at("processors.log"), tmp.at("abproc"), {{"AB_ROOT", "/media"}}});

    vector<string> seen;
    ProcessorRunner::Outcome outcome = runner.start(
        unzip(tmp), {"--games", "/media/Games"}, "",
        [&seen](const ableem::ProcessorOutput &o) { seen.push_back(o.stage() + "|" + to_string(o.percent())); },
        nullptr);
    CHECK(outcome.result == ProcessorResult::Ok);
    CHECK(outcome.message.empty());
    CHECK(outcome.announced);
    CHECK(outcome.warnings == vector<string>{"kept the zip"});
    CHECK(seen == vector<string>{"|-1", "Unpacking Crash.zip|-1", "Unpacking Crash.zip|-1", "Unpacking Crash.zip|40",
                                 "Unpacking Crash.zip|100"});

    REQUIRE(fake.calls.size() == 1);
    CHECK(fake.calls[0] == vector<string>{"--start", "--games", "/media/Games"});
    CHECK(fake.lastCwd == tmp.at("Processors/unzip"));
    CHECK(envValue(fake.lastEnv, "AB_PROCESSOR_PROTOCOL") == "1");
    CHECK(envValue(fake.lastEnv, "AB_ROOT") == "/media");
    CHECK(envValue(fake.lastEnv, "AB_TMP") == tmp.at("abproc") + "/unzip");
    CHECK_FALSE(DirEntry::exists(tmp.at("abproc/unzip"))); // removed after the run

    string log = tmp.readFile("processors.log");
    CHECK(log.find("unzip 1.0.0 --start --games /media/Games") != string::npos);
    CHECK(log.find("! inflating\n") != string::npos);
    CHECK(log.find("=== exit 0, ok") != string::npos);
}

TEST_CASE("ProcessorRunner: the ways a run fails") {
    TempDir tmp("runner_fail");
    FakeProcess fake;
    ProcessorRunner runner(fake, {tmp.at("processors.log"), "", {}});

    fake.lines = {"#Starting - X", "#ERROR - Not enough space"};
    fake.code = 1;
    ProcessorRunner::Outcome o = runner.start(unzip(tmp), {"--ps1", "g"}, "Crash", nullptr, nullptr);
    CHECK(o.result == ProcessorResult::Failed);
    CHECK(o.message == "Not enough space");
    CHECK_FALSE(o.announced);

    fake.lines = {"#Starting - X", "#Working", "43"};
    fake.code = 0;
    o = runner.start(unzip(tmp), {"--ps1", "g"}, "", nullptr, nullptr);
    CHECK(o.result == ProcessorResult::Failed);
    CHECK(o.message == "ended (exit 0) without #DONE");

    fake.lines = {"#DONE"};
    fake.code = 2;
    o = runner.start(unzip(tmp), {"--ps1", "g"}, "", nullptr, nullptr);
    CHECK(o.message == "said #DONE but exited with 2");

    fake.lines = {};
    fake.code = -1;
    o = runner.start(unzip(tmp), {"--ps1", "g"}, "", nullptr, nullptr);
    CHECK(o.message == "could not be started");
}

TEST_CASE("ProcessorRunner: stopped by the launcher is Interrupted, silence past Timeout is Failed") {
    TempDir tmp("runner_stop");
    FakeProcess fake;
    fake.lines = {"#Starting - X", "#Working", "7"};
    fake.waitForStop = true;
    ProcessorRunner runner(fake, {"", "", {}});

    int asked = 0;
    ProcessorRunner::Outcome o =
        runner.start(unzip(tmp), {"--ps1", "g"}, "", nullptr, [&asked] { return ++asked > 2; });
    CHECK(o.result == ProcessorResult::Interrupted);

    ProcessorInfo slow = unzip(tmp);
    slow.timeoutSeconds = 1;
    auto start = chrono::steady_clock::now();
    o = runner.start(slow, {"--ps1", "g"}, "", nullptr, nullptr);
    CHECK(o.result == ProcessorResult::Failed);
    CHECK(o.message == "no output for 1 s");
    CHECK(chrono::steady_clock::now() - start < chrono::seconds(5));
}

TEST_CASE("ProcessorRunner::isMine: 0 is mine, 1 is not, anything else is not and logged") {
    TempDir tmp("runner_ismine");
    FakeProcess fake;
    ProcessorRunner runner(fake, {tmp.at("processors.log"), "", {}});
    fake.code = 0;
    CHECK(runner.isMine(unzip(tmp), {"--rom", "a.zip", "--system", "MAME"}, nullptr));
    CHECK(fake.calls.back() == vector<string>{"--ismine", "--rom", "a.zip", "--system", "MAME"});
    fake.code = 1;
    CHECK_FALSE(runner.isMine(unzip(tmp), {"--ps1", "g"}, nullptr));
    CHECK_FALSE(DirEntry::exists(tmp.at("processors.log")));
    fake.code = 139;
    CHECK_FALSE(runner.isMine(unzip(tmp), {"--ps1", "g"}, nullptr));
    CHECK(tmp.readFile("processors.log").find("exit 139 - taken as not mine") != string::npos);
}
