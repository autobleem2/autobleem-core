//
// ExtensionCatalog: Extensions/*/extension.ini read and resolved for this machine, the disabled list and the
// crash guard (docs/extensions-plan.md in the launcher).
//
#include "doctest/doctest.h"

#include "../support/temp_dir.h"
#include "core/services/extension_catalog.h"
#include "core/main.h"

using namespace std;

namespace {
// an extension folder with its ini and a library for the given platform keys
void extension(const TempDir &tmp, const string &name, const string &ini, const vector<string> &keys) {
    tmp.makeSubDir("Extensions/" + name);
    tmp.writeFile("Extensions/" + name + "/extension.ini", ini);
    for (const string &k : keys) {
        tmp.makeSubDir("Extensions/" + name + "/bin/" + k);
        tmp.writeFile("Extensions/" + name + "/bin/" + k + "/" + name + ".so", "x");
    }
}

ExtensionCatalog catalogIn(const TempDir &tmp, const vector<string> &keys = {"psc"}) {
    return ExtensionCatalog(tmp.at("Extensions"), tmp.at("System/Extensions"), keys, ".so");
}
} // namespace

TEST_CASE("ExtensionCatalog reads every extension.ini, sorted by title, and resolves its plugin") {
    TempDir tmp("extensions");
    extension(tmp, "store",
              "[extension]\nName=AutoBleem Store\nDescription=Download apps and games\nAuthor=AutoBleem team\n"
              "Version=1.0.0\nPlugin=bin/{key}/store\nIcon=icon.png\nBackground=true\nNetwork=required\n",
              {"psc", "rpi64"});
    tmp.writeFile("Extensions/store/icon.png", "png");
    extension(tmp, "hello", "[extension]\nName=Hello\nPlugin=bin/{key}/hello\n", {"win"});
    tmp.makeSubDir("Extensions/not-one"); // no extension.ini

    ExtensionCatalog catalog = catalogIn(tmp);
    const vector<ExtensionInfo> &list = catalog.scan();
    REQUIRE(list.size() == 2);
    CHECK(list[0].title == "AutoBleem Store");
    CHECK(list[1].title == "Hello");

    const ExtensionInfo &store = list[0];
    CHECK(store.name == "store");
    CHECK(store.folder == tmp.at("Extensions/store"));
    CHECK(store.description == "Download apps and games");
    CHECK(store.author == "AutoBleem team");
    CHECK(store.version == "1.0.0");
    CHECK(store.icon == tmp.at("Extensions/store") + "/icon.png");
    CHECK(store.background);
    CHECK(store.network == ExtensionNetwork::Required);
    CHECK(store.builtForThisSystem());
    CHECK(store.manifest.program == tmp.at("Extensions/store/bin/psc/store.so")); // the suffix added
    CHECK_FALSE(store.disabled);

    // built only for Windows: listed, but not for this system
    const ExtensionInfo &hello = list[1];
    CHECK_FALSE(hello.builtForThisSystem());
    CHECK_FALSE(hello.background);
    CHECK(hello.network == ExtensionNetwork::None);
    CHECK(hello.icon.empty());
}

TEST_CASE("ExtensionCatalog: an extension of the old App kind (Startup=) is not a plugin") {
    TempDir tmp("extensions");
    extension(tmp, "old", "[extension]\nName=Old\nStartup=run.sh\n", {});
    tmp.writeFile("Extensions/old/run.sh", "#!/bin/sh\n");
    ExtensionCatalog catalog = catalogIn(tmp);
    REQUIRE(catalog.scan().size() == 1);
    CHECK_FALSE(catalog.extensions()[0].builtForThisSystem());
}

TEST_CASE("ExtensionCatalog: the name defaults to the folder's; Network= values") {
    TempDir tmp("extensions");
    extension(tmp, "nameless", "[extension]\nPlugin=bin/{key}/nameless\n", {"psc"});
    ExtensionCatalog catalog = catalogIn(tmp);
    REQUIRE(catalog.scan().size() == 1);
    CHECK(catalog.extensions()[0].title == "nameless");

    CHECK(ExtensionCatalog::parseNetwork(" Required ") == ExtensionNetwork::Required);
    CHECK(ExtensionCatalog::parseNetwork("optional") == ExtensionNetwork::Optional);
    CHECK(ExtensionCatalog::parseNetwork("none") == ExtensionNetwork::None);
    CHECK(ExtensionCatalog::parseNetwork("") == ExtensionNetwork::None);
    CHECK(ExtensionCatalog::parseNetwork("sometimes") == ExtensionNetwork::None);
}

TEST_CASE("ExtensionCatalog: the disabled list survives a rescan and a new catalog") {
    TempDir tmp("extensions");
    extension(tmp, "store", "[extension]\nPlugin=bin/{key}/store\n", {"psc"});
    extension(tmp, "hello", "[extension]\nPlugin=bin/{key}/hello\n", {"psc"});
    {
        ExtensionCatalog catalog = catalogIn(tmp);
        catalog.scan();
        catalog.setDisabled("store", true);
        CHECK(catalog.find("store")->disabled);
        CHECK(catalog.isDisabled("store"));
        CHECK_FALSE(catalog.isDisabled("hello"));
        catalog.setDisabled("store", true); // twice is once
    }
    ExtensionCatalog again = catalogIn(tmp);
    again.scan();
    CHECK(again.find("store")->disabled);
    CHECK_FALSE(again.find("hello")->disabled);
    CHECK(tmp.readFile("System/Extensions/disabled.txt") == "store\n");

    again.setDisabled("store", false);
    again.scan();
    CHECK_FALSE(again.find("store")->disabled);
}

TEST_CASE("ExtensionCatalog: the crash guard names, disables and forgets the extension that was running") {
    TempDir tmp("extensions");
    extension(tmp, "store", "[extension]\nPlugin=bin/{key}/store\n", {"psc"});
    ExtensionCatalog catalog = catalogIn(tmp);
    catalog.scan();

    CHECK(catalog.takeCrashed().empty()); // nothing was running

    catalog.markActive("store");
    catalog.clearActive(); // a call that returned
    CHECK(catalog.takeCrashed().empty());

    catalog.markActive("store"); // and the launcher died here
    ExtensionCatalog next = catalogIn(tmp);
    next.scan();
    CHECK(next.takeCrashed() == "store");
    CHECK(next.find("store")->disabled);
    CHECK(next.takeCrashed().empty()); // once
}

TEST_CASE("ExtensionCatalog: what the runtime learnt about a plugin is kept across a rescan") {
    TempDir tmp("extensions");
    extension(tmp, "store", "[extension]\nPlugin=bin/{key}/store\n", {"psc"});
    ExtensionCatalog catalog = catalogIn(tmp);
    catalog.scan();
    catalog.find("store")->loadProblem = "built for a different AutoBleem";
    catalog.scan();
    CHECK(catalog.find("store")->loadProblem == "built for a different AutoBleem");
}

TEST_CASE("ExtensionCatalog: no Extensions folder is an empty list") {
    TempDir tmp("extensions");
    ExtensionCatalog catalog = catalogIn(tmp);
    CHECK(catalog.scan().empty());
}

TEST_CASE("ExtensionCatalog: the crash guard lives in RAM; a crash's copy on the stick is read too") {
    TempDir tmp("extensions");
    extension(tmp, "store", "[extension]\nPlugin=bin/{key}/store\n", {"psc"});
    extension(tmp, "hello", "[extension]\nPlugin=bin/{key}/hello\n", {"psc"});
    auto inRam = [&tmp] {
        return ExtensionCatalog(tmp.at("Extensions"), tmp.at("System/Extensions"), {"psc"}, ".so", tmp.at("run"));
    };
    ExtensionCatalog catalog = inRam();
    catalog.scan();

    catalog.markActive("store");
    CHECK(DirEntry::exists(tmp.at("run/extensions.active")));
    CHECK_FALSE(DirEntry::exists(tmp.at("System/Extensions/.active"))); // the stick is not written around a call
    catalog.clearActive();

    // a crash the console rebooted after: rc/ab_log.sh copied the marker to the stick
    tmp.writeFile("System/Extensions/.active", "hello\n");
    ExtensionCatalog next = inRam();
    next.scan();
    CHECK(next.takeCrashed() == "hello");
    CHECK(next.find("hello")->disabled);
    CHECK_FALSE(DirEntry::exists(tmp.at("System/Extensions/.active")));
}
