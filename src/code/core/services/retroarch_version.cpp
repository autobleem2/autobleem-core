//
// retroarch_version - see the header.
//
#include "retroarch_version.h"

#include <cctype>
#include <fstream>
#include <sstream>

using namespace std;

namespace retroarch_version {

namespace {
string trimmed(const string &s) {
    size_t first = 0;
    size_t last = s.size();
    while (first < last && isspace(static_cast<unsigned char>(s[first])))
        first++;
    while (last > first && isspace(static_cast<unsigned char>(s[last - 1])))
        last--;
    return s.substr(first, last - first);
}
} // namespace

string parse(const string &text) {
    string version, build;
    bool isFirst = true;
    istringstream lines(text);
    string line;
    while (getline(lines, line)) {
        line = trimmed(line);
        if (isFirst) {
            isFirst = false;
            if (line.find('=') == string::npos)
                return line;
        }
        const size_t eq = line.find('=');
        if (eq == string::npos)
            continue;
        const string key = trimmed(line.substr(0, eq)), value = trimmed(line.substr(eq + 1));
        if (key == "retroarch_version")
            version = value;
        else if (key == "psc_build")
            build = value;
    }
    if (version.empty())
        return "";
    return build.empty() ? version : version + "-" + build;
}

string installed(const string &usbRoot) {
    ifstream file(usbRoot + "/RetroArch/bin/VERSION", ios::binary);
    if (!file)
        return "";
    ostringstream all;
    all << file.rdbuf();
    return parse(all.str());
}

} // namespace retroarch_version
