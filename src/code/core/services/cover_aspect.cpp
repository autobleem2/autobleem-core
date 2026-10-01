//
// CoverAspectTable - see the header.
//
#include "cover_aspect.h"

#include <cctype>
#include <fstream>
#include <sstream>

using namespace std;

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

// one side of "w:h": digits only, 1..MaxSide
bool parseSide(const string &text, int &out) {
    const string s = trimmed(text);
    if (s.empty() || s.size() > 3)
        return false;
    int value = 0;
    for (char c : s) {
        if (!isdigit(static_cast<unsigned char>(c)))
            return false;
        value = value * 10 + (c - '0');
    }
    if (value < 1 || value > CoverAspectTable::MaxSide)
        return false;
    out = value;
    return true;
}

// a playlist's db_name may carry the ".lpl" the file has ("Nintendo - Nintendo 64.lpl")
string withoutLpl(const string &name) {
    const string ext = ".lpl";
    if (name.size() <= ext.size())
        return name;
    for (size_t i = 0; i < ext.size(); i++) {
        if (tolower(static_cast<unsigned char>(name[name.size() - ext.size() + i])) != ext[i])
            return name;
    }
    return name.substr(0, name.size() - ext.size());
}
} // namespace

//*******************************
// CoverAspectTable::pathFor
//*******************************
string CoverAspectTable::pathFor(const string &resourcesDir) {
    return resourcesDir + "/platform/cover_aspects.cfg";
}

//*******************************
// CoverAspectTable::load
//*******************************
CoverAspectTable CoverAspectTable::load(const string &path) {
    ifstream in(path, ios::binary);
    if (!in)
        return CoverAspectTable();
    stringstream text;
    text << in.rdbuf();
    return parse(text.str());
}

//*******************************
// CoverAspectTable::parse
//*******************************
CoverAspectTable CoverAspectTable::parse(const string &text) {
    CoverAspectTable table;
    istringstream lines(text);
    string line;
    while (getline(lines, line)) {
        line = trimmed(line); // also takes the '\r' of a CRLF file
        if (line.empty() || line[0] == '#')
            continue;
        const size_t eq = line.find('=');
        if (eq == string::npos)
            continue;
        const string name = trimmed(line.substr(0, eq));
        const string shape = line.substr(eq + 1);
        const size_t colon = shape.find(':');
        CoverAspect aspect;
        if (name.empty() || colon == string::npos || !parseSide(shape.substr(0, colon), aspect.w) ||
            !parseSide(shape.substr(colon + 1), aspect.h))
            continue;
        table.aspects_[name] = aspect;
    }
    return table;
}

//*******************************
// CoverAspectTable::aspectFor
//*******************************
CoverAspect CoverAspectTable::aspectFor(const string &dbName) const {
    auto it = aspects_.find(withoutLpl(trimmed(dbName)));
    return it == aspects_.end() ? CoverAspect() : it->second;
}
