// ab_test_path(rel) - a test's data path, resolved at run time from the test program's own location.
// harness.cmake force-includes this header into every suite and defines AB_TEST_DATA_DIR and friends as
// ab_test_path("<path from the program's folder>"). A path baked in as an absolute -D string would be part of
// the compiler-cache key (and of the cached object), so no two clones, build folders or containers could share
// their cache entries; the relative form is the same text everywhere.
#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

inline std::string ab_test_path(const char *relFromExeDir) {
    static const std::string exeDir = [] {
        std::string exe;
#ifdef _WIN32
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        exe.assign(buf, n);
        for (char &c : exe)
            if (c == '\\') c = '/';
#else
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) exe.assign(buf, static_cast<size_t>(n));
#endif
        size_t slash = exe.rfind('/');
        return slash == std::string::npos ? std::string(".") : exe.substr(0, slash);
    }();
    // exeDir + "/" + rel with the "." and ".." segments folded away
    std::vector<std::string> parts;
    std::string all = exeDir + "/" + relFromExeDir, seg;
    for (size_t i = 0; i <= all.size(); ++i) {
        if (i < all.size() && all[i] != '/') {
            seg += all[i];
            continue;
        }
        if (seg == "..") {
            if (!parts.empty() && parts.back() != "..") parts.pop_back();
            else parts.push_back(seg);
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(seg);
        }
        seg.clear();
    }
    std::string out = (!all.empty() && all[0] == '/') ? "/" : "";
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? "/" : "") + parts[i];
    return out;
}
