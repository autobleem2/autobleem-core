// lib_ableem - engine: comparing paths as text. A Windows host writes its paths with backslashes, a trailing
// slash, a drive letter in either case; RetroArch and a playlist add "//" and "./" of their own. Two paths that
// name one file must compare equal - nothing on disk is looked at (no symlinks, no existence).
#pragma once

#include <string>

namespace ableem {

//******************
// PathCompare
//******************
struct PathCompare {
    // One form: forward slashes only, no empty or "." parts, ".." resolved against the part before it (a drive
    // letter and a leading "//host" stay), no trailing slash; lower case (ASCII) when `ignoreCase`.
    static std::string normalize(const std::string &path, bool ignoreCase = false);
    // whether two paths name the same file once normalised; case counts on every host but Windows
    static bool same(const std::string &a, const std::string &b);
    static bool same(const std::string &a, const std::string &b, bool ignoreCase);
    // whether `path` is `dir` or inside it (a folder name is never a prefix of another name); false for an empty `dir`
    static bool isUnder(const std::string &path, const std::string &dir);
    static bool isUnder(const std::string &path, const std::string &dir, bool ignoreCase);
    // when `path` is under `dir`: what follows it, normalised with its case kept ("" for `dir` itself)
    static bool relativeTo(const std::string &path, const std::string &dir, std::string &rest);
    // the host's rule: true on Windows
    static bool hostIgnoresCase();
};

} // namespace ableem
