//
// NativePluginLoader - see the header.
//
#include "plugin_loader.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace std;

//*******************************
// NativePluginLoader::open
//*******************************
void *NativePluginLoader::open(const string &path, string &error) {
#ifdef _WIN32
    // the plugin's own folder first for the DLLs it brings, then the system's; the launcher's folder is
    // where its import library points already (autobleem-gui.exe is the running image)
    int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    wstring wide(size > 0 ? size - 1 : 0, L'\0');
    if (size > 1)
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide[0], size);
    for (auto &c : wide)
        if (c == L'/')
            c = L'\\'; // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR wants a fully qualified path with backslashes
    HMODULE module =
        LoadLibraryExW(wide.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == nullptr)
        error = "LoadLibraryEx failed, error " + to_string(GetLastError());
    return reinterpret_cast<void *>(module);
#else
    void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char *why = dlerror();
        error = why != nullptr ? why : "dlopen failed";
    }
    return handle;
#endif
}

//*******************************
// NativePluginLoader::symbol
//*******************************
void *NativePluginLoader::symbol(void *handle, const char *name) {
    if (handle == nullptr)
        return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}
