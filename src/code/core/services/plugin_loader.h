//
// PluginLoader: opening a shared library and finding a symbol in it - dlopen/dlsym, LoadLibraryEx/
// GetProcAddress - behind one seam, so the extension runtime can be tested with in-process fakes
// (docs/extensions-plan.md in the launcher).
//
#pragma once

#include <string>

//******************
// PluginLoader
//******************
class PluginLoader {
public:
    virtual ~PluginLoader() = default;
    // the library's handle, nullptr (and `error` says why) when it cannot be loaded. Never unloaded: a C++
    // library with static objects and threads is not safely unloadable, and the launcher's life is short.
    virtual void *open(const std::string &path, std::string &error) = 0;
    // the symbol's address, nullptr when the library has none by that name
    virtual void *symbol(void *handle, const char *name) = 0;
};

//******************
// NativePluginLoader
//******************
// The real one. Linux: dlopen(RTLD_NOW | RTLD_LOCAL) - every symbol bound at load, so a missing one is a
// refusal here rather than a crash later, and one plugin's symbols never become another's. Windows:
// LoadLibraryEx with the plugin's own folder searched for its DLLs.
class NativePluginLoader : public PluginLoader {
public:
    void *open(const std::string &path, std::string &error) override;
    void *symbol(void *handle, const char *name) override;
};
