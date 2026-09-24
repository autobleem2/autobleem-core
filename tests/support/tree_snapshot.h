//
// TreeSnapshot: every file and directory under a root with its size and modification time, to assert that
// an operation wrote nothing (docs/quiet-stick-plan.md in the launcher repo: a no-change scan, a second
// start, a save of an unchanged file must leave the stick alone).
//
#pragma once

#include <map>
#include <string>
#include <vector>

namespace test_support {

//******************
// TreeSnapshot
//******************
// A rewrite with the same bytes is exactly what these tests are after, so the content alone cannot tell;
// the modification time can. It is read at the filesystem's own resolution (100 ns on NTFS, ns on ext4),
// so two writes a few microseconds apart still differ - and a file replaced through .tmp + rename is a
// new file with a new time. Directories are listed too (a created or removed one is a write), without a
// time: adding an entry to a directory changes its time on some filesystems and not on others.
//
//     TreeSnapshot before(tmp.path());
//     scan();
//     CHECK(before.changesSince(TreeSnapshot(tmp.path())).empty());
//
class TreeSnapshot {
public:
    explicit TreeSnapshot(const std::string &root);

    // "+ path" (new), "- path" (gone), "~ path" (size or time changed), sorted; empty when nothing changed
    std::vector<std::string> changesTo(const TreeSnapshot &after) const;

    struct Entry {
        bool isDir;
        long long size;
        long long mtime; // filesystem ticks; only compared, never interpreted
    };

private:
    void walk(const std::string &dir, const std::string &relative);
    std::string root_;
    std::map<std::string, Entry> entries_;
};

} // namespace test_support
