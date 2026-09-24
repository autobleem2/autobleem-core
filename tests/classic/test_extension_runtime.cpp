//
// ExtensionRuntime: loading (and refusing) extensions, their life around the launcher's frames and games, and
// the crash guard around every call - with in-process fake plugins behind a fake PluginLoader
// (docs/extensions-plan.md in the launcher).
//
#include "doctest/doctest.h"

#include "support/temp_dir.h"
#include "core/main.h"
#include "gui/extension_runtime.h"

#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

vector<string> calls; // what the fake extensions and hosts saw, in order
ExtensionCatalog *currentCatalog = nullptr;

// the host the runtime creates each extension with - app() is never reached in these tests
struct FakeHost : ExtensionHost {
    explicit FakeHost(const ExtensionInfo &e) : name_(e.name), folder_(e.folder), state_("state") {}
    AppBase &app() override { abort(); }
    const string &name() const override { return name_; }
    const string &folder() const override { return folder_; }
    const string &stateDir() const override { return state_; }
    bool networkUp() override { return true; }
    void requestRescan() override { calls.push_back(name_ + ":rescan"); }
    void reloadApps() override {}
    void reloadConfig() override {}
    void notify(const string &, const string &, uint64_t, uint64_t) override {}
    void clearNotification() override {}
    plog::IAppender *logAppender() override { return nullptr; }
    plog::Severity logSeverity() override { return plog::none; }
    string name_, folder_, state_;
};

struct FakeExtension : Extension {
    explicit FakeExtension(ExtensionHost &host) : host(host) { calls.push_back(host.name() + ":create"); }
    ~FakeExtension() override { calls.push_back(host.name() + ":destroy"); }
    void run() override {
        // the crash guard's marker is there while the extension runs
        calls.push_back(host.name() + ":run" +
                        (DirEntry::exists(currentCatalog->activeFile()) ? "(guarded)" : "(unguarded)"));
        host.requestRescan();
    }
    void poll() override { calls.push_back(host.name() + ":poll"); }
    void suspend() override { calls.push_back(host.name() + ":suspend"); }
    void resume() override { calls.push_back(host.name() + ":resume"); }
    void shutdown() override { calls.push_back(host.name() + ":shutdown"); }
    ExtensionHost &host;
};

struct ThrowingExtension : FakeExtension {
    using FakeExtension::FakeExtension;
    void run() override { throw runtime_error("boom"); }
};

// the plugins' two C functions, as the fake loader hands them out
const char *goodAbi() { return AB_SDK_STAMP; }
const char *otherAbi() { return "sdk=0;cxx=gcc-6;cxx11abi=1;target=psc"; }
Extension *createFake(ExtensionHost &host) { return new FakeExtension(host); }
Extension *createThrowing(ExtensionHost &host) { return new ThrowingExtension(host); }
Extension *createNothing(ExtensionHost &) { throw runtime_error("no"); }

struct FakePlugin {
    AbExtensionAbiFunction abi;
    AbExtensionCreateFunction create;
};

struct FakeLoader : PluginLoader {
    void *open(const string &path, string &error) override {
        string file = DirEntry::getFileNameFromPath(path);
        auto it = plugins.find(file);
        if (it == plugins.end()) {
            error = "no such plugin";
            return nullptr;
        }
        opened.push_back(file);
        return &it->second;
    }
    void *symbol(void *handle, const char *name) override {
        auto *p = static_cast<FakePlugin *>(handle);
        if (string(name) == "ab_extension_abi")
            return reinterpret_cast<void *>(p->abi);
        if (string(name) == "ab_extension_create")
            return reinterpret_cast<void *>(p->create);
        return nullptr;
    }
    map<string, FakePlugin> plugins;
    vector<string> opened;
};

// a tree with extensions whose "libraries" the fake loader knows by file name
struct Tree {
    Tree() : tmp("ext_runtime"), catalog(tmp.at("Extensions"), tmp.at("System/Extensions"), {"psc"}, ".so") {
        calls.clear();
        currentCatalog = &catalog;
    }
    void add(const string &name, const string &extraIni, FakePlugin plugin) {
        tmp.makeSubDir("Extensions/" + name + "/bin/psc");
        tmp.writeFile("Extensions/" + name + "/extension.ini", "[extension]\nPlugin=bin/{key}/" + name + "\n" + extraIni);
        tmp.writeFile("Extensions/" + name + "/bin/psc/" + name + ".so", "x");
        loader.plugins[name + ".so"] = plugin;
    }
    unique_ptr<ExtensionRuntime> runtime() {
        catalog.scan();
        return unique_ptr<ExtensionRuntime>(new ExtensionRuntime(
            catalog, loader, [](const ExtensionInfo &e) { return unique_ptr<ExtensionHost>(new FakeHost(e)); }));
    }
    TempDir tmp;
    ExtensionCatalog catalog;
    FakeLoader loader;
};

bool has(const string &call) {
    for (const string &c : calls)
        if (c == call)
            return true;
    return false;
}

} // namespace

TEST_CASE("ExtensionRuntime runs an extension: loaded on first use, run inside the crash guard, host reachable") {
    Tree t;
    t.add("hello", "", {goodAbi, createFake});
    {
        auto rt = t.runtime();
        CHECK_FALSE(rt->isLoaded("hello"));
        CHECK(rt->run("hello", true) == ExtensionRuntime::Refusal::None);
        CHECK(rt->isLoaded("hello"));
        CHECK(calls == vector<string>{"hello:create", "hello:run(guarded)", "hello:rescan"});
        CHECK_FALSE(DirEntry::exists(t.catalog.activeFile())); // cleared after

        // a second run reuses the loaded one
        CHECK(rt->run("hello", true) == ExtensionRuntime::Refusal::None);
        CHECK(t.loader.opened.size() == 1);
    }
    // the runtime going shuts it down, then destroys it
    CHECK(calls[calls.size() - 2] == "hello:shutdown");
    CHECK(calls.back() == "hello:destroy");
}

TEST_CASE("ExtensionRuntime refuses: offline for Network=required, disabled, not built here, unknown") {
    Tree t;
    t.add("store", "Network=required\n", {goodAbi, createFake});
    t.add("maybe", "Network=optional\n", {goodAbi, createFake});
    t.tmp.makeSubDir("Extensions/winonly");
    t.tmp.writeFile("Extensions/winonly/extension.ini", "[extension]\nPlugin=bin/{key}/winonly\n");
    auto rt = t.runtime();

    CHECK(rt->run("store", false) == ExtensionRuntime::Refusal::Offline);
    CHECK_FALSE(rt->isLoaded("store"));
    CHECK(rt->run("maybe", false) == ExtensionRuntime::Refusal::None); // optional runs offline
    CHECK(rt->run("winonly", true) == ExtensionRuntime::Refusal::NotBuiltForThisSystem);
    CHECK(rt->run("nothing", true) == ExtensionRuntime::Refusal::NotFound);

    t.catalog.setDisabled("store", true);
    CHECK(rt->run("store", true) == ExtensionRuntime::Refusal::Disabled);
    CHECK(t.loader.opened == vector<string>{"maybe.so"});
}

TEST_CASE("ExtensionRuntime refuses a plugin built for another SDK, and says so in the catalog") {
    Tree t;
    t.add("old", "", {otherAbi, createFake});
    auto rt = t.runtime();
    CHECK(rt->run("old", true) == ExtensionRuntime::Refusal::WrongAbi);
    CHECK_FALSE(has("old:create")); // nothing of it was called but the stamp
    CHECK(t.catalog.find("old")->loadProblem == "built for a different AutoBleem");
}

TEST_CASE("ExtensionRuntime: a library that will not load, or is not an extension") {
    Tree t;
    t.add("broken", "", {goodAbi, createFake});
    t.loader.plugins.erase("broken.so"); // the file is there, the loader cannot open it
    t.add("stranger", "", {nullptr, nullptr});
    auto rt = t.runtime();
    CHECK(rt->run("broken", true) == ExtensionRuntime::Refusal::LoadFailed);
    CHECK(t.catalog.find("broken")->loadProblem == "no such plugin");
    CHECK(rt->run("stranger", true) == ExtensionRuntime::Refusal::LoadFailed);
    CHECK(t.catalog.find("stranger")->loadProblem == "not an AutoBleem extension");
}

TEST_CASE("ExtensionRuntime: an exception out of an extension is caught, logged, and it is not called again") {
    Tree t;
    t.add("thrower", "Background=true\n", {goodAbi, createThrowing});
    t.add("nocreate", "", {goodAbi, createNothing});
    auto rt = t.runtime();

    CHECK(rt->run("thrower", true) == ExtensionRuntime::Refusal::Failed);
    CHECK_FALSE(DirEntry::exists(t.catalog.activeFile())); // the guard is cleared: it did not crash us
    CHECK(rt->run("thrower", true) == ExtensionRuntime::Refusal::Failed);
    rt->poll(); // not polled any more
    CHECK_FALSE(has("thrower:poll"));

    CHECK(rt->run("nocreate", true) == ExtensionRuntime::Refusal::Failed);
    CHECK(rt->run("nocreate", true) == ExtensionRuntime::Refusal::Failed);
    CHECK(t.loader.opened.size() == 2); // not loaded twice
}

TEST_CASE("ExtensionRuntime: background extensions start with the launcher, poll every frame, pause for a game") {
    Tree t;
    t.add("store", "Background=true\nNetwork=required\n", {goodAbi, createFake});
    t.add("hello", "", {goodAbi, createFake});
    auto rt = t.runtime();

    rt->startBackground(); // the network is not asked: store's poll waits for one itself
    CHECK(rt->isLoaded("store"));
    CHECK_FALSE(rt->isLoaded("hello"));

    rt->poll();
    rt->poll();
    CHECK(calls == vector<string>{"store:create", "store:poll", "store:poll"});

    rt->run("hello", true); // not a background one: loaded, run, never polled
    calls.clear();
    rt->poll();
    CHECK(calls == vector<string>{"store:poll"});

    calls.clear();
    rt->suspend();
    rt->poll(); // nothing while a game runs
    rt->suspend(); // twice is once
    rt->resume();
    rt->poll();
    CHECK(calls == vector<string>{"store:suspend", "hello:suspend", "store:resume", "hello:resume", "store:poll"});

    calls.clear();
    rt->shutdown();
    rt->shutdown();
    rt->poll();
    CHECK(has("store:shutdown"));
    CHECK(has("hello:shutdown"));
    CHECK_FALSE(has("store:poll"));
}

TEST_CASE("ExtensionRuntime: a disabled background extension is not started") {
    Tree t;
    t.add("store", "Background=true\n", {goodAbi, createFake});
    t.catalog.scan();
    t.catalog.setDisabled("store", true);
    auto rt = t.runtime();
    rt->startBackground();
    CHECK_FALSE(rt->isLoaded("store"));
    CHECK(t.loader.opened.empty());
}

TEST_CASE("the SDK stamp names the ABI, the compiler and the target") {
    string stamp = AB_SDK_STAMP;
    CHECK(stamp.find("sdk=" + to_string(AB_SDK_ABI) + ";") == 0);
    CHECK(stamp.find(";cxx=") != string::npos);
    CHECK(stamp.find(";target=") != string::npos);
}
