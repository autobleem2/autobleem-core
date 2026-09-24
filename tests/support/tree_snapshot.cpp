#include "tree_snapshot.h"

#include <ableem/engine/filesystem.h>

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

using namespace ableem;

namespace test_support {

namespace {

//*******************************
// statEntry
//*******************************
bool statEntry(const std::string &path, TreeSnapshot::Entry &entry) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    entry.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    entry.size = entry.isDir ? 0 : (static_cast<long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    entry.mtime = entry.isDir ? 0
                              : (static_cast<long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                                    data.ftLastWriteTime.dwLowDateTime;
#else
    struct stat st{};
    if (stat(path.c_str(), &st) != 0)
        return false;
    entry.isDir = S_ISDIR(st.st_mode);
    entry.size = entry.isDir ? 0 : static_cast<long long>(st.st_size);
#ifdef __APPLE__
    entry.mtime = entry.isDir ? 0 : st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
    entry.mtime = entry.isDir ? 0 : st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
#endif
    return true;
}

} // namespace

//*******************************
// TreeSnapshot::TreeSnapshot
//*******************************
TreeSnapshot::TreeSnapshot(const std::string &root) : root_(root) {
    walk(root, "");
}

//*******************************
// TreeSnapshot::walk
//*******************************
void TreeSnapshot::walk(const std::string &dir, const std::string &relative) {
    for (const auto &e : DirEntry::diru(dir)) {
        std::string path = dir + sep + e.name;
        std::string rel = relative.empty() ? e.name : relative + "/" + e.name;
        Entry entry{};
        if (!statEntry(path, entry))
            continue;
        entries_[rel] = entry;
        if (entry.isDir)
            walk(path, rel);
    }
}

//*******************************
// TreeSnapshot::changesTo
//*******************************
std::vector<std::string> TreeSnapshot::changesTo(const TreeSnapshot &after) const {
    std::vector<std::string> changes;
    for (const auto &b : entries_) {
        auto a = after.entries_.find(b.first);
        if (a == after.entries_.end())
            changes.push_back("- " + b.first);
        else if (a->second.isDir != b.second.isDir || a->second.size != b.second.size ||
                 a->second.mtime != b.second.mtime)
            changes.push_back("~ " + b.first);
    }
    for (const auto &a : after.entries_)
        if (entries_.find(a.first) == entries_.end())
            changes.push_back("+ " + a.first);
    std::sort(changes.begin(), changes.end());
    return changes;
}

} // namespace test_support
