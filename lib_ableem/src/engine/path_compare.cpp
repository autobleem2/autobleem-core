#include "ableem/engine/path_compare.h"

#include <algorithm>
#include <vector>

using namespace std;

namespace ableem {

//*******************************
// PathCompare::normalize
//*******************************
string PathCompare::normalize(const string &path, bool ignoreCase) {
    string slashed = path;
    replace(slashed.begin(), slashed.end(), '\\', '/');
    const bool absolute = !slashed.empty() && slashed[0] == '/';
    const bool unc = slashed.size() > 2 && slashed.compare(0, 2, "//") == 0 && slashed[2] != '/'; // //host/share

    vector<string> parts;
    size_t start = 0;
    while (start <= slashed.size()) {
        size_t end = slashed.find('/', start);
        if (end == string::npos)
            end = slashed.size();
        string part = slashed.substr(start, end - start);
        start = end + 1;
        if (part.empty() || part == ".")
            continue;
        if (part == "..") {
            const bool driveOnly = parts.size() == 1 && parts[0].size() == 2 && parts[0][1] == ':'; // C:/..
            if (!parts.empty() && parts.back() != ".." && !driveOnly) {
                parts.pop_back();
                continue;
            }
            if (absolute || driveOnly)
                continue;
        }
        parts.push_back(part);
    }

    string out = unc ? "//" : (absolute ? "/" : "");
    for (size_t i = 0; i < parts.size(); i++)
        out += (i ? "/" : "") + parts[i];
    if (out.empty() && !path.empty())
        out = "."; // "./" and the like: the folder itself
    if (ignoreCase) {
        for (char &c : out)
            c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

//*******************************
// PathCompare::hostIgnoresCase
//*******************************
bool PathCompare::hostIgnoresCase() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

//*******************************
// PathCompare::same / isUnder / relativeTo
//*******************************
bool PathCompare::same(const string &a, const string &b) {
    return same(a, b, hostIgnoresCase());
}

bool PathCompare::same(const string &a, const string &b, bool ignoreCase) {
    return normalize(a, ignoreCase) == normalize(b, ignoreCase);
}

bool PathCompare::isUnder(const string &path, const string &dir) {
    return isUnder(path, dir, hostIgnoresCase());
}

bool PathCompare::isUnder(const string &path, const string &dir, bool ignoreCase) {
    const string root = normalize(dir, ignoreCase);
    if (root.empty())
        return false;
    const string full = normalize(path, ignoreCase);
    if (full == root)
        return true;
    const string prefix = root.back() == '/' ? root : root + "/";
    return full.compare(0, prefix.size(), prefix) == 0;
}

bool PathCompare::relativeTo(const string &path, const string &dir, string &rest) {
    if (!isUnder(path, dir))
        return false;
    const string root = normalize(dir);
    const string full = normalize(path); // lower-casing keeps the length, so the cut falls in the same place
    if (full.size() <= root.size()) {
        rest.clear();
        return true;
    }
    rest = full.substr(root.back() == '/' ? root.size() : root.size() + 1);
    return true;
}

} // namespace ableem
