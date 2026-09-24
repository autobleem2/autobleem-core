//
// The scanner processors' bookkeeping: ProcessorCatalog (processor.ini), ProcessorSequences (sequence.ini, the
// user's order) and ProcessorState (what already ran) - docs/scanner-processors-plan.md in the launcher.
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/main.h"
#include "core/services/processor_catalog.h"
#include "core/services/processor_sequences.h"
#include "core/services/processor_state.h"

using namespace std;

namespace {
// a processor folder with its ini and a program for the given platform keys
void processor(const TempDir &tmp, const string &name, const string &ini, const vector<string> &keys = {"psc"}) {
    tmp.makeSubDir("Processors/" + name);
    tmp.writeFile("Processors/" + name + "/processor.ini", ini);
    for (const string &k : keys) {
        tmp.makeSubDir("Processors/" + name + "/bin/" + k);
        tmp.writeFile("Processors/" + name + "/bin/" + k + "/" + name, "x");
    }
}

vector<string> names(const vector<ProcessorSequences::Entry> &entries) {
    vector<string> out;
    for (const auto &e : entries)
        out.push_back((e.enabled ? "" : "-") + e.name);
    return out;
}

const string unzipIni = "[Processor]\nName=Unzip\nDescription=Unpacks zipped games\nAuthor=someone\nVersion=1.0.0\n"
                        "Exec=bin/{key}/unzip\nKinds=games-folder,rom   ; any subset\nMatch=*.zip;*.7z\n"
                        "Order=10\nTimeout=120\nModifies=true\n";
} // namespace

TEST_CASE("ProcessorCatalog reads processor.ini, comments and all, and resolves the program") {
    TempDir tmp("processors");
    processor(tmp, "unzip", unzipIni);
    processor(tmp, "checker",
              "[Processor]\nExec=bin/{key}/checker\nKinds=ps1\nSystems=Sega - Mega Drive - Genesis;MAME\n"
              "Modifies=false\n",
              {"win"});
    tmp.makeSubDir("Processors/.tmp");
    tmp.makeSubDir("Processors/no-ini");

    ProcessorCatalog catalog(tmp.at("Processors"), {"psc"});
    const vector<ProcessorInfo> &list = catalog.scan();
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "checker");
    CHECK(list[1].name == "unzip");

    const ProcessorInfo &unzip = list[1];
    CHECK(unzip.title == "Unzip");
    CHECK(unzip.description == "Unpacks zipped games");
    CHECK(unzip.version == "1.0.0");
    CHECK(unzip.kinds == vector<ProcessorKind>{ProcessorKind::GamesFolder, ProcessorKind::Rom});
    CHECK(unzip.match == vector<string>{"*.zip", "*.7z"});
    CHECK(unzip.order == 10);
    CHECK(unzip.timeoutSeconds == 120);
    CHECK(unzip.modifies);
    CHECK(unzip.builtForThisSystem());
    CHECK(unzip.manifest.program == tmp.at("Processors/unzip/bin/psc/unzip"));
    CHECK(unzip.belongsTo(ProcessorSequence::Ps1));
    CHECK(unzip.belongsTo(ProcessorSequence::Roms));
    CHECK(unzip.matchesFile("Crash Bandicoot (USA).ZIP"));
    CHECK_FALSE(unzip.matchesFile("Crash.bin"));
    CHECK(unzip.wantsSystem("anything"));

    const ProcessorInfo &checker = list[0];
    CHECK(checker.title == "checker"); // no Name=
    CHECK_FALSE(checker.builtForThisSystem());
    CHECK_FALSE(checker.modifies);
    CHECK(checker.belongsTo(ProcessorSequence::Ps1));
    CHECK_FALSE(checker.belongsTo(ProcessorSequence::Roms));
    CHECK(checker.matchesFile("whatever.bin")); // no Match: every file
    CHECK(checker.wantsSystem("mame"));
    CHECK_FALSE(checker.wantsSystem("Nintendo - Game Boy"));

    // only what can run here is watched for
    CHECK(catalog.watchPatterns() == vector<string>{"*.zip", "*.7z"});
    CHECK(catalog.find("unzip") == &list[1]);
    CHECK(catalog.find("nope") == nullptr);
}

TEST_CASE("ProcessorCatalog::globMatch and stripComment") {
    CHECK(ProcessorCatalog::globMatch("a.bin.ecm", "*.ecm"));
    CHECK(ProcessorCatalog::globMatch("Game (Disc 1).cue", "*(Disc ?).cue"));
    CHECK(ProcessorCatalog::globMatch("x", "*"));
    CHECK_FALSE(ProcessorCatalog::globMatch("a.ecm.bak", "*.ecm"));
    CHECK_FALSE(ProcessorCatalog::globMatch("", "?"));
    CHECK(ProcessorCatalog::stripComment("games-folder,rom   ; any subset") == "games-folder,rom");
    CHECK(ProcessorCatalog::stripComment("*.zip;*.7z") == "*.zip;*.7z"); // no blank before ';': a separator
    CHECK(ProcessorCatalog::stripComment("; all") == "");
    CHECK(ProcessorCatalog::parseKinds("PS1, rom, bogus, ps1") ==
          vector<ProcessorKind>{ProcessorKind::Ps1, ProcessorKind::Rom});
}

TEST_CASE("ProcessorSequences: new ones by Order at the end, the user's order kept, the gone dropped") {
    TempDir tmp("sequences");
    processor(tmp, "unzip", unzipIni);
    processor(tmp, "rvz2chd", "[Processor]\nExec=bin/{key}/rvz2chd\nKinds=ps1\nOrder=20\n");
    processor(tmp, "patch", "[Processor]\nExec=bin/{key}/patch\nKinds=ps1\nOrder=5\n");
    ProcessorCatalog catalog(tmp.at("Processors"), {"psc"});
    catalog.scan();

    // first contact: nothing in the file, every one new - by Order
    ProcessorSequences seq(tmp.at("Processors/sequence.ini"));
    CHECK(seq.load(catalog.processors()));
    CHECK(names(seq.entries(ProcessorSequence::Ps1)) == vector<string>{"patch", "unzip", "rvz2chd"});
    CHECK(names(seq.entries(ProcessorSequence::Roms)) == vector<string>{"unzip"});

    // the user sorts: patch last, unzip off
    CHECK(seq.move(ProcessorSequence::Ps1, 0, 2));
    seq.setEnabled(ProcessorSequence::Ps1, 0, false);
    CHECK(names(seq.entries(ProcessorSequence::Ps1)) == vector<string>{"-unzip", "rvz2chd", "patch"});
    CHECK_FALSE(seq.move(ProcessorSequence::Ps1, 1, 1));
    REQUIRE(seq.save());

    // read back: the same, nothing to save
    ProcessorSequences again(tmp.at("Processors/sequence.ini"));
    CHECK_FALSE(again.load(catalog.processors()));
    CHECK(names(again.entries(ProcessorSequence::Ps1)) == vector<string>{"-unzip", "rvz2chd", "patch"});
    vector<const ProcessorInfo *> chain = again.chain(ProcessorSequence::Ps1, catalog.processors());
    REQUIRE(chain.size() == 2);
    CHECK(chain[0]->name == "rvz2chd");
    CHECK(chain[1]->name == "patch");

    // a new one with Order=1 still goes to the end - the user's order is not touched; a removed one goes
    processor(tmp, "first", "[Processor]\nExec=bin/{key}/first\nKinds=ps1\nOrder=1\n");
    DirEntry::removeDirAndContents(tmp.at("Processors/rvz2chd"));
    catalog.scan();
    ProcessorSequences third(tmp.at("Processors/sequence.ini"));
    CHECK(third.load(catalog.processors()));
    CHECK(names(third.entries(ProcessorSequence::Ps1)) == vector<string>{"-unzip", "patch", "first"});
}

TEST_CASE("ProcessorSequences reads a hand-written file: comments, case, duplicates, strangers") {
    TempDir tmp("sequences_hand");
    processor(tmp, "unzip", unzipIni);
    processor(tmp, "patch", "[Processor]\nExec=bin/{key}/patch\nKinds=ps1\n", {"win"}); // not for this system
    tmp.writeFile("Processors/sequence.ini", "; mine\n[PS1]\npatch   ; keep it first\nunzip\nunzip\nstranger\n"
                                             "\n[roms]\n-unzip\npatch\n");
    ProcessorCatalog catalog(tmp.at("Processors"), {"psc"});
    catalog.scan();
    ProcessorSequences seq(tmp.at("Processors/sequence.ini"));
    CHECK(seq.load(catalog.processors())); // the duplicate and the stranger went
    CHECK(names(seq.entries(ProcessorSequence::Ps1)) == vector<string>{"patch", "unzip"});
    CHECK(names(seq.entries(ProcessorSequence::Roms)) == vector<string>{"-unzip"});
    // patch keeps its place but does not run here
    vector<const ProcessorInfo *> chain = seq.chain(ProcessorSequence::Ps1, catalog.processors());
    REQUIRE(chain.size() == 1);
    CHECK(chain[0]->name == "unzip");
    CHECK(seq.chain(ProcessorSequence::Roms, catalog.processors()).empty());
}

TEST_CASE("ProcessorState: settled by version and digest, interrupted owed a run, saved and forgotten") {
    TempDir tmp("processor_state");
    ProcessorState state(tmp.at("processors.state"));
    CHECK(state.load()); // no file yet
    CHECK_FALSE(state.isSettled("unzip", "1.0", "ps1", "Crash", "d1"));

    state.record("unzip", "1.0", "ps1", "Crash", "d1", ProcessorResult::Ok);
    state.record("unzip", "1.0", "games-folder", "", "t1", ProcessorResult::Failed);
    state.record("unecm", "2.0", "ps1", "Tekken", "d2", ProcessorResult::Interrupted);
    CHECK(state.isSettled("unzip", "1.0", "ps1", "Crash", "d1"));
    CHECK_FALSE(state.isSettled("unzip", "1.1", "ps1", "Crash", "d1"));  // a new version runs again
    CHECK_FALSE(state.isSettled("unzip", "1.0", "ps1", "Crash", "d9"));  // so does a changed game
    CHECK(state.isSettled("unzip", "1.0", "games-folder", "", "t1"));    // failed: not retried as it is
    CHECK_FALSE(state.isSettled("unecm", "2.0", "ps1", "Tekken", "d2")); // interrupted: owed a run
    REQUIRE(state.save());

    ProcessorState again(tmp.at("processors.state"));
    REQUIRE(again.load());
    CHECK(again.isSettled("unzip", "1.0", "ps1", "Crash", "d1"));
    CHECK(again.isSettled("unzip", "1.0", "games-folder", "", "t1"));
    CHECK_FALSE(again.isSettled("unecm", "2.0", "ps1", "Tekken", "d2"));
    again.forget("unzip");
    CHECK_FALSE(again.isSettled("unzip", "1.0", "ps1", "Crash", "d1"));
    CHECK_FALSE(again.empty());
}

TEST_CASE("ProcessorState::digest: names and sizes, without what the launcher writes itself") {
    TempDir tmp("processor_digest");
    tmp.makeSubDir("Games/Crash/sub");
    tmp.makeSubDir("Games/!SaveStates/Crash");
    tmp.writeFile("Games/Crash/Crash.bin", "1234");
    tmp.writeFile("Games/Crash/sub/extra.txt", "x");
    string before = ProcessorState::digest(tmp.at("Games"));
    CHECK(ProcessorState::files(tmp.at("Games")).size() == 2);

    // none of these count
    tmp.writeFile("Games/Crash/Game.ini", "[Game]\n");
    tmp.writeFile("Games/Crash/pcsx.cfg", "x");
    tmp.writeFile("Games/Crash/Crash.m3u", "x");
    tmp.writeFile("Games/Crash/Crash.bin.part", "half");
    tmp.writeFile("Games/Crash/.hidden", "x");
    tmp.writeFile("Games/!SaveStates/Crash/state", "x");
    CHECK(ProcessorState::digest(tmp.at("Games")) == before);

    // a size or a name does
    tmp.writeFile("Games/Crash/Crash.bin", "12345");
    CHECK(ProcessorState::digest(tmp.at("Games")) != before);
    string file = ProcessorState::digest(tmp.at("Games/Crash/Crash.bin"));
    CHECK(file != ProcessorState::digest(tmp.at("Games/Crash/sub/extra.txt")));
    CHECK(file.size() == 32);
}
