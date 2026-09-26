//
// ExtensionRuntime: the loaded extensions and their life - load (with the ABI check), create, run, poll,
// suspend/resume around a game, shutdown - and the crash guard around every call into one
// (docs/extensions-plan.md in the launcher).
//
#pragma once

#include "extension.h"
#include "core/services/extension_catalog.h"
#include "core/services/plugin_loader.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

//******************
// ExtensionRuntime
//******************
class ExtensionRuntime {
public:
    // makes the host an extension is created with - the launcher's, which knows its scan and its bubble
    using HostFactory = std::function<std::unique_ptr<ExtensionHost>(const ExtensionInfo &)>;

    // why an extension cannot run; None = it can
    // NotHandled: runEntry() - the extension's runEntry() returned false (it does not know that entry)
    enum class Refusal {
        None,
        NotFound,
        NotBuiltForThisSystem,
        Disabled,
        Offline,
        LoadFailed,
        WrongAbi,
        Failed,
        NotHandled
    };

    ExtensionRuntime(ExtensionCatalog &catalog, PluginLoader &loader, HostFactory hostFactory);
    ~ExtensionRuntime(); // shutdown(), if nobody did
    ExtensionRuntime(const ExtensionRuntime &) = delete;
    ExtensionRuntime &operator=(const ExtensionRuntime &) = delete;

    // what can be told without loading: not built for this system, disabled, offline for Network=required
    static Refusal precheck(const ExtensionInfo &extension, bool networkUp);

    // loads and creates every Background=true extension that can run (the network is not asked: its poll()
    // waits for one itself). Called once, at start-up, after the crash guard had its say.
    void startBackground();
    // the Extensions list's Cross: loads and creates it if need be, then run()
    Refusal run(const std::string &name, bool networkUp);
    // a launcher item's Cross: the extension at one of its Provides= entries - loaded and created if need
    // be, then runEntry(entry) inside the crash guard, as run(). NotHandled when it returned false
    Refusal runEntry(const std::string &name, const std::string &entry, bool networkUp);
    // the same for whichever extension provides the entry (ExtensionCatalog::findProvider); NotFound when
    // none does. The caller scans the catalog first, as for run()
    Refusal runProvider(const std::string &entry, bool networkUp);

    // once a frame: every loaded extension's poll() - nothing while suspended
    void poll();
    void suspend();
    void resume();
    void shutdown();

    bool isLoaded(const std::string &name) const;
    bool suspended() const { return suspended_; }

private:
    struct Loaded {
        std::string name;
        void *handle = nullptr;
        std::unique_ptr<ExtensionHost> host; // declared before ext: it outlives it
        std::unique_ptr<Extension> extension;
        bool polled = false;
        bool failed = false; // an exception came out of it: it is not called again
    };

    Loaded *load(ExtensionInfo &extension, Refusal &why);
    // precheck + load; nullptr (and why) when it cannot run
    Loaded *prepare(const std::string &name, bool networkUp, Refusal &why);
    // runs `call` inside the crash guard, catching what a plugin throws; false when it threw
    bool guarded(Loaded &loaded, const char *what, const std::function<void()> &call);

    ExtensionCatalog &catalog_;
    PluginLoader &loader_;
    HostFactory hostFactory_;
    std::vector<std::unique_ptr<Loaded>> loaded_;
    bool suspended_ = false;
    bool shutDown_ = false;
};
