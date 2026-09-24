//
// ProcessorOutput: the processor protocol's output lines, as the launcher reads them
// (docs/scanner-processors-plan.md in the launcher).
//
#include "doctest/doctest.h"

#include <ableem/engine/processor_output.h>

#include <string>
#include <vector>

using namespace std;
using ableem::ProcessorOutput;

namespace {
ProcessorOutput feedAll(const vector<string> &lines) {
    ProcessorOutput out;
    for (const string &line : lines)
        out.feed(line);
    return out;
}
} // namespace

TEST_CASE("ProcessorOutput: the request's success example") {
    ProcessorOutput out = feedAll({"#Starting  - Something V0.2.3", "0", "10", "100", "#Processing games", "0 ", "10",
                                   "40", "50", "60", "#DONE "});
    CHECK(out.title() == "Something V0.2.3");
    CHECK(out.stage() == "Processing games");
    CHECK(out.percent() == 60);
    CHECK(out.active());
    CHECK(out.finished());
    CHECK(out.reportedDone());
    CHECK_FALSE(out.reportedError());
    CHECK(out.succeeded(0));
    CHECK_FALSE(out.succeeded(1)); // #DONE with a non-zero exit is a failure
}

TEST_CASE("ProcessorOutput: the request's error example") {
    ProcessorOutput out = feedAll({"#Starting  - Something V0.2.3", "0", "10", "100", "#Processing games", "0", "10",
                                   "40", "#ERROR - Description"});
    CHECK(out.finished());
    CHECK(out.reportedError());
    CHECK(out.error() == "Description");
    CHECK(out.percent() == 40);
    CHECK_FALSE(out.succeeded(0));
}

TEST_CASE("ProcessorOutput: nothing to do is not announced") {
    ProcessorOutput out = feedAll({"#Starting  - Something V0.2.3", "#DONE"});
    CHECK_FALSE(out.active());
    CHECK(out.succeeded(0));
}

TEST_CASE("ProcessorOutput: exit 0 without #DONE is a failure") {
    ProcessorOutput out = feedAll({"#Starting - X", "#Working", "43"});
    CHECK_FALSE(out.finished());
    CHECK_FALSE(out.succeeded(0));
}

TEST_CASE("ProcessorOutput: a stage resets the percent and the counter; the counter is kept apart") {
    ProcessorOutput out;
    CHECK(out.feed("#Decoding a.bin.ecm"));
    CHECK(out.percent() == -1);
    CHECK(out.feed("1/2"));
    CHECK(out.done() == 1);
    CHECK(out.total() == 2);
    CHECK(out.feed("55"));
    CHECK(out.percent() == 55);
    CHECK(out.feed("#Decoding b.bin.ecm"));
    CHECK(out.stage() == "Decoding b.bin.ecm");
    CHECK(out.percent() == -1);
    CHECK(out.total() == 0);
    CHECK(out.feed("250")); // clamped
    CHECK(out.percent() == 100);
}

TEST_CASE("ProcessorOutput: warnings, carriage returns, chatter and lines after the end") {
    ProcessorOutput out;
    CHECK_FALSE(out.feed("#WARN - disc 2 has no cue, one was written\r"));
    CHECK_FALSE(out.feed("inflating: Crash.bin")); // a tool's own output: ignored
    CHECK_FALSE(out.feed("0/0"));                  // no total: not a counter
    CHECK_FALSE(out.feed("-5"));
    CHECK_FALSE(out.feed(""));
    CHECK_FALSE(out.feed("#"));
    REQUIRE(out.warnings().size() == 1);
    CHECK(out.warnings()[0] == "disc 2 has no cue, one was written");
    CHECK_FALSE(out.active());

    out.feed("#done");
    CHECK(out.reportedDone()); // case does not matter
    CHECK_FALSE(out.feed("#ERROR - too late"));
    CHECK_FALSE(out.reportedError());
    CHECK(out.succeeded(0));
}

TEST_CASE("ProcessorOutput: a stage that only starts with a keyword is still a stage") {
    ProcessorOutput out;
    CHECK(out.feed("#Doner kebab"));
    CHECK(out.stage() == "Doner kebab");
    CHECK_FALSE(out.finished());
    out.feed("#ERROR");
    CHECK(out.error() == "error");
}
