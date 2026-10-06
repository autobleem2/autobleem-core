//
// PackageTable - see the header.
//
#include "package_table.h"
#include "../main.h"

#include <ableem/engine/log.h>

#include <cstdlib>
#include <sstream>

using namespace std;

namespace {

string lowerTrim(const string &s) {
    return ableem::toLowerCopy(Strings::trim(s));
}

// the "a;b;c" list of a value, trimmed, empty items dropped
vector<string> listOf(const string &value) {
    vector<string> out;
    for (const string &item : Strings::getTokens(value, ';')) {
        const string t = Strings::trim(item);
        if (!t.empty())
            out.push_back(t);
    }
    return out;
}

// one row being read
struct Pending {
    PackageRow row;
    int line = 0;
    bool open = false;
    bool bad = false; // a key of it was malformed: the row is dropped
    string why;
};

void finish(Pending &p, vector<PackageRow> &rows, vector<string> &problems) {
    if (!p.open)
        return;
    p.open = false;
    const string where = "[" + p.row.id + "] (line " + to_string(p.line) + ")";
    if (p.bad) {
        problems.push_back(where + ": " + p.why);
        return;
    }
    if (p.row.kind.empty() || p.row.title.empty() || p.row.match.empty()) {
        problems.push_back(where + ": needs kind, title and match");
        return;
    }
    rows.push_back(p.row);
}

} // namespace

//*******************************
// PackageTable::validName
//*******************************
bool PackageTable::validName(const string &value, size_t maxLength) {
    if (value.empty() || value.size() > maxLength)
        return false;
    bool lastDash = true; // no leading '-'
    for (char c : value) {
        if (c == '-') {
            if (lastDash)
                return false; // leading or doubled
            lastDash = true;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            lastDash = false;
        } else {
            return false;
        }
    }
    return !lastDash; // no trailing '-'
}

//*******************************
// PackageTable::cleanRelativePath
//*******************************
string PackageTable::cleanRelativePath(const string &path) {
    string p = Strings::trim(path);
    if (p.empty())
        return "";
    for (char &c : p) {
        if (c == '\\')
            c = '/';
        if (static_cast<unsigned char>(c) < 32)
            return "";
    }
    if (p[0] == '/' || p.find(':') != string::npos)
        return ""; // absolute, or a drive letter
    string out;
    for (const string &segment : Strings::getTokens(p, '/')) { // empty segments (a doubled '/') are dropped
        const string s = Strings::trim(segment);
        if (s == "..")
            return "";
        if (s.empty() || s == ".")
            continue;
        out += (out.empty() ? "" : "/") + s;
    }
    return out;
}

//*******************************
// PackageTable::parse
//*******************************
void PackageTable::parse(const string &text, vector<PackageRow> &rows, vector<string> &problems) {
    istringstream in(text);
    string line;
    int number = 0;
    Pending pending;
    while (getline(in, line)) {
        number++;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3);
        line = Strings::trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue; // only a line that STARTS with # or ; is a comment: a # inside a value is text

        if (line[0] == '[') {
            finish(pending, rows, problems);
            pending = Pending();
            const size_t close = line.find(']');
            const string id = close == string::npos ? string() : lowerTrim(line.substr(1, close - 1));
            pending.line = number;
            pending.open = true;
            pending.row.id = id;
            if (!validName(id)) {
                pending.row.id = close == string::npos ? line : line.substr(1, close - 1);
                pending.bad = true;
                pending.why = "the row id is not a valid name";
            }
            continue;
        }
        if (!pending.open)
            continue; // a key before any row
        const size_t eq = line.find('=');
        if (eq == string::npos)
            continue;
        const string key = lowerTrim(line.substr(0, eq));
        const string value = Strings::trim(line.substr(eq + 1));
        PackageRow &row = pending.row;
        auto reject = [&pending](const string &why) {
            pending.bad = true;
            pending.why = why;
        };

        if (key == "kind") {
            row.kind = ableem::toLowerCopy(value);
            if (!validName(row.kind))
                reject("kind \"" + value + "\" is not a valid kind");
        } else if (key == "title") {
            row.title = value;
        } else if (key == "match") {
            row.match.clear();
            for (const string &item : listOf(value)) {
                const string clean = cleanRelativePath(item);
                if (clean.empty()) {
                    reject("match \"" + item + "\" is not a relative path");
                    break;
                }
                row.match.push_back(clean);
            }
        } else if (key == "main") {
            row.main = cleanRelativePath(value);
            if (row.main.empty() && !value.empty())
                reject("main \"" + value + "\" is not a relative path");
        } else if (key == "size") {
            char *end = nullptr;
            const long long n = strtoll(value.c_str(), &end, 10);
            if (value.empty() || end == nullptr || *end != 0 || n < 0)
                reject("size \"" + value + "\" is not a number");
            else
                row.size = n;
        } else if (key == "magic") {
            row.magic = value.substr(0, 4);
        } else if (key == "variant") {
            row.variant = value;
        } else if (key == "licence") {
            row.licence = value;
        } else if (key == "start") {
            row.starts.clear();
            for (const string &item : listOf(value)) {
                const size_t bar = item.find('|');
                const string file = cleanRelativePath(item.substr(0, bar));
                if (file.empty()) {
                    reject("start \"" + item + "\" names no file");
                    break;
                }
                row.starts.emplace_back(file, bar == string::npos ? string() : Strings::trim(item.substr(bar + 1)));
            }
        } else if (key.compare(0, 4, "set.") == 0) {
            const string name = key.substr(4);
            if (!name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == string::npos)
                row.settings.emplace_back(name, value);
        } else if (key == "mapper") {
            row.mapper = cleanRelativePath(value);
        }
        // anything else: room for later versions
    }
    finish(pending, rows, problems);
}

//*******************************
// PackageTable::load
//*******************************
bool PackageTable::load(const string &path, vector<PackageRow> &rows, vector<string> &problems) {
    string text;
    if (!DirEntry::readFile(path, text))
        return false;
    parse(text, rows, problems);
    return true;
}
