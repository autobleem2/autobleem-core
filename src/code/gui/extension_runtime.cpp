//
// ExtensionRuntime - see the header.
//
#include "extension_runtime.h"

#include <cstring>
#include <exception>

using namespace std;

//*******************************
// ExtensionRuntime::ExtensionRuntime / ~ExtensionRuntime
//*******************************
ExtensionRuntime::ExtensionRuntime(ExtensionCatalog &catalog, PluginLoader &loader, HostFactory hostFactory)
    : catalog_(catalog), loader_(loader), hostFactory_(std::move(hostFactory)) {}

ExtensionRuntime::~ExtensionRuntime() {
    shutdown();
}

//*******************************
// ExtensionRuntime::precheck
//*******************************
ExtensionRuntime::Refusal ExtensionRuntime::precheck(const ExtensionInfo &extension, bool networkUp) {
    if (!extension.builtForThisSystem())
        return Refusal::NotBuiltForThisSystem;
    if (extension.disabled)
        return Refusal::Disabled;
    if (extension.network == ExtensionNetwork::Required && !networkUp)
        return Refusal::Offline;
    return Refusal::None;
}

//*******************************
// ExtensionRuntime::guarded
//*******************************
bool ExtensionRuntime::guarded(Loaded &loaded, const char *what, const function<void()> &call) {
    catalog_.markActive(loaded.name);
    bool ok = true;
    try {
        call();
    } catch (const exception &e) {
        PLOG_ERROR << "[" << loaded.name << "] " << what << " threw: " << e.what();
        ok = false;
    } catch (...) {
        PLOG_ERROR << "[" << loaded.name << "] " << what << " threw";
        ok = false;
    }
    catalog_.clearActive();
    if (!ok) {
        loaded.failed = true;
        if (ExtensionInfo *info = catalog_.find(loaded.name))
            info->loadProblem = string(what) + " failed";
    }
    return ok;
}

//*******************************
// ExtensionRuntime::load
//*******************************
ExtensionRuntime::Loaded *ExtensionRuntime::load(ExtensionInfo &extension, Refusal &why) {
    for (auto &l : loaded_) {
        if (l->name != extension.name)
            continue;
        if (l->failed) {
            why = Refusal::Failed; // it threw once: not called again until the launcher restarts
            return nullptr;
        }
        return l.get();
    }

    const string &path = extension.manifest.program;
    string error;
    void *handle = loader_.open(path, error);
    if (handle == nullptr) {
        PLOG_ERROR << "[" << extension.name << "] cannot load " << path << ": " << error;
        extension.loadProblem = error;
        why = Refusal::LoadFailed;
        return nullptr;
    }
    auto abi = reinterpret_cast<AbExtensionAbiFunction>(loader_.symbol(handle, "ab_extension_abi"));
    auto create = reinterpret_cast<AbExtensionCreateFunction>(loader_.symbol(handle, "ab_extension_create"));
    if (abi == nullptr || create == nullptr) {
        PLOG_ERROR << "[" << extension.name << "] " << path << " is not an AutoBleem extension";
        extension.loadProblem = "not an AutoBleem extension";
        why = Refusal::LoadFailed;
        return nullptr;
    }
    const char *stamp = abi();
    if (stamp == nullptr || strcmp(stamp, AB_SDK_STAMP) != 0) {
        PLOG_ERROR << "[" << extension.name << "] built for " << (stamp ? stamp : "(nothing)")
                   << ", this AutoBleem is " << AB_SDK_STAMP << " - not loaded";
        extension.loadProblem = "built for a different AutoBleem";
        why = Refusal::WrongAbi;
        return nullptr;
    }

    unique_ptr<Loaded> loaded(new Loaded());
    loaded->name = extension.name;
    loaded->handle = handle;
    loaded->host = hostFactory_(extension);
    Loaded &l = *loaded;
    loaded_.push_back(std::move(loaded));
    Extension *created = nullptr;
    if (!guarded(l, "create", [&]() { created = create(*l.host); }) || created == nullptr) {
        l.failed = true; // remembered, so the next Cross is refused rather than tried again
        why = Refusal::Failed;
        return nullptr;
    }
    l.extension.reset(created);
    PLOG_INFO << "[" << extension.name << "] loaded " << path;
    return &l;
}

//*******************************
// ExtensionRuntime::startBackground
//*******************************
void ExtensionRuntime::startBackground() {
    for (ExtensionInfo &e : catalog_.extensions()) {
        if (!e.background || precheck(e, true) != Refusal::None)
            continue;
        Refusal why = Refusal::None;
        load(e, why);
    }
}

//*******************************
// ExtensionRuntime::run
//*******************************
ExtensionRuntime::Refusal ExtensionRuntime::run(const string &name, bool networkUp) {
    ExtensionInfo *info = catalog_.find(name);
    if (info == nullptr)
        return Refusal::NotFound;
    Refusal why = precheck(*info, networkUp);
    if (why != Refusal::None) {
        PLOG_INFO << "[" << name << "] not run: " << static_cast<int>(why);
        return why;
    }
    Loaded *loaded = load(*info, why);
    if (loaded == nullptr)
        return why;
    PLOG_INFO << "[" << name << "] run";
    bool ok = guarded(*loaded, "run", [&]() { loaded->extension->run(); });
    PLOG_INFO << "[" << name << "] run ended";
    return ok ? Refusal::None : Refusal::Failed;
}

//*******************************
// ExtensionRuntime::poll / suspend / resume / shutdown
//*******************************
void ExtensionRuntime::poll() {
    if (suspended_ || shutDown_)
        return;
    for (auto &l : loaded_) {
        if (l->failed)
            continue;
        ExtensionInfo *info = catalog_.find(l->name);
        if (info == nullptr || !info->background)
            continue;
        if (!l->polled) {
            // the first poll is inside the crash guard, like create and run; after that the marker would
            // be written every frame, which a stick should not be asked to do
            l->polled = true;
            guarded(*l, "poll", [&]() { l->extension->poll(); });
            continue;
        }
        try {
            l->extension->poll();
        } catch (...) {
            PLOG_ERROR << "[" << l->name << "] poll threw - not called again";
            l->failed = true;
        }
    }
}

void ExtensionRuntime::suspend() {
    if (suspended_ || shutDown_)
        return;
    suspended_ = true;
    for (auto &l : loaded_)
        if (!l->failed)
            guarded(*l, "suspend", [&]() { l->extension->suspend(); });
}

void ExtensionRuntime::resume() {
    if (!suspended_ || shutDown_)
        return;
    suspended_ = false;
    for (auto &l : loaded_)
        if (!l->failed)
            guarded(*l, "resume", [&]() { l->extension->resume(); });
}

void ExtensionRuntime::shutdown() {
    if (shutDown_)
        return;
    shutDown_ = true;
    for (auto &l : loaded_) {
        if (!l->failed)
            guarded(*l, "shutdown", [&]() { l->extension->shutdown(); });
        PLOG_INFO << "[" << l->name << "] shut down";
    }
    // the extensions go before their hosts (Loaded's member order) and before the process ends; the
    // libraries stay mapped - never unloaded
    loaded_.clear();
}

//*******************************
// ExtensionRuntime::isLoaded
//*******************************
bool ExtensionRuntime::isLoaded(const string &name) const {
    for (const auto &l : loaded_)
        if (l->name == name && !l->failed)
            return true;
    return false;
}
