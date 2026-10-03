#include "ableem/engine/strings.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

using namespace std;

namespace ableem {

//*******************************
// Strings::replaceAll
//*******************************
void Strings::replaceAll(string &str, const string &from, const string &to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

//*******************************
// Strings::escapeCommas
//*******************************
string Strings::escapeCommas(string input) {
    replaceAll(input, "|", "||");
    replaceAll(input, ",", "|@");
    return input;
}

//*******************************
// Strings::unescapeCommas
//*******************************
string Strings::unescapeCommas(string input) {
    replaceAll(input, "|@", ",");
    replaceAll(input, "||", "|");
    return input;
}

//*******************************
// Strings::isInteger
//*******************************
bool Strings::isInteger(const char *input) {
    size_t ln = strlen(input);
    for (size_t i = 0; i < ln; i++) {
        if (!isdigit(input[i])) {
            return false;
        }
    }
    return true;
}

//*******************************
// Strings::toInt
//*******************************
// parses leading whitespace, an optional sign and digits. anything else (or an empty string) returns def.
int Strings::toInt(const string &s, int def) {
    const char *start = s.c_str();
    char *end = nullptr;
    errno = 0;
    long value = strtol(start, &end, 10);
    if (end == start || errno == ERANGE || value > INT_MAX || value < INT_MIN) {
        return def;
    }
    return static_cast<int>(value);
}

//*******************************
// Strings::compareCaseInsensitive
//*******************************
bool Strings::compareCaseInsensitive(string first, string second) {
    return lcase(first) == lcase(second);
}

//*******************************
// Strings::floatToString
//*******************************
string Strings::floatToString(float f, int n) {
    ostringstream stringStream;
    stringStream << fixed << setprecision(n) << f;
    return stringStream.str();
}

//*******************************
// Strings::commaSep
//*******************************
string Strings::commaSep(const string &s, int pos) {
    vector<string> v;
    char c = ',';
    int i = 0;
    int j = s.find(c);

    while (j >= 0) {
        v.push_back(s.substr(i, j - i));
        i = ++j;
        j = s.find(c, j);

        if (j < 0) {
            v.push_back(s.substr(i, s.length()));
        }
    }
    if (pos < static_cast<int>(v.size())) {
        return v[pos];
    }
    return "";
}

//*******************************
// Strings::ltrim
//*******************************
string Strings::ltrim(const string &s) {
    size_t start = s.find_first_not_of(" \n\r\t\f\v");
    return (start == string::npos) ? "" : s.substr(start);
}

//*******************************
// Strings::rtrim
//*******************************
string Strings::rtrim(const string &s) {
    size_t end = s.find_last_not_of(" \n\r\t\f\v");
    return (end == string::npos) ? "" : s.substr(0, end + 1);
}

//*******************************
// Strings::trim
//*******************************
string Strings::trim(const string &s) {
    return rtrim(ltrim(s));
}

//*******************************
// Strings::getStringWithinChar
//*******************************
string Strings::getStringWithinChar(string s, char del) {
    int first = s.find(del);
    int last = s.find_last_of(del);
    return s.substr(first + 1, last - first - 1);
}

//*******************************
// Strings::removeCharsFromString
//*******************************
void Strings::removeCharsFromString(string &str, string charsToRemove) {
    for (char ch : charsToRemove)
        str.erase(std::remove(str.begin(), str.end(), ch), str.end());
}

//*******************************
// Strings::getlineRemoveCR
// does a getline. if it's a Windows file the CR at the end is removed.
//*******************************
istream &Strings::getlineRemoveCR(istream &is, string &str) {
    istream &ret = getline(is, str);
    if (!str.empty() && *str.rbegin() == '\r')
        str.erase(str.length() - 1, 1);
    return ret;
}

//*******************************
// Strings::removeComment
// remove "#" to end of line
//*******************************
void Strings::removeComment(string &str) {
    auto it = str.find("#");
    if (it != str.npos)
        str.erase(it);
}

//*******************************
// Strings::cleanPublisherString
// remove any trailing "." or space or " ."
//*******************************
void Strings::cleanPublisherString(string &pub) {
    if (pub.size() > 0 && pub.back() == '.')
        pub.pop_back();
    if (pub.size() > 0 && pub.back() == ' ')
        pub.pop_back();
}

//*******************************
// Strings::getTokens
//*******************************
vector<string> Strings::getTokens(const string &str, char delim) {
    istringstream ss(str);
    string token;
    vector<string> ret;
    while (getline(ss, token, delim)) {
        if (token != "")
            ret.push_back(token);
    }
    return ret;
}

namespace {

// the upper-case form of a code point below U+0800 (the ranges upperUtf8 documents); the point itself otherwise
unsigned upperPoint(unsigned c) {
    if (c < 0x80)
        return (c >= 'a' && c <= 'z') ? c - 32 : c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) // Latin-1: a-grave .. thorn
        return c - 32;
    if (c == 0xFF)
        return 0x178;
    if (c == 0x131) // dotless i
        return 'I';
    if ((c >= 0x100 && c <= 0x137 && c != 0x130 && c != 0x131 && c != 0x138) || (c >= 0x14A && c <= 0x177))
        return (c & 1) ? c - 1 : c; // Latin Extended-A: the lower case is the odd one of a pair
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
        return (c & 1) ? c : c - 1; // ... and the even one in these runs
    if (c == 0x3AC)
        return 0x386;
    if (c >= 0x3AD && c <= 0x3AF)
        return c - 37;
    if (c == 0x3CC)
        return 0x38C;
    if (c == 0x3CD || c == 0x3CE)
        return c - 63;
    if (c == 0x3C2)
        return 0x3A3; // final sigma
    if ((c >= 0x3B1 && c <= 0x3CB) && c != 0x3C2)
        return c - 32; // Greek alpha .. omega (+ the two dialytika letters)
    if (c >= 0x430 && c <= 0x44F)
        return c - 32; // Cyrillic
    if (c >= 0x450 && c <= 0x45F)
        return c - 80;
    return c;
}

} // namespace

//*******************************
// Strings::upperUtf8
//*******************************
string Strings::upperUtf8(const string &s) {
    string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            out += static_cast<char>(upperPoint(b));
            i++;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) {
            const unsigned c = ((b & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            const unsigned u = upperPoint(c);
            if (u < 0x80) {
                out += static_cast<char>(u);
            } else {
                out += static_cast<char>(0xC0 | (u >> 6));
                out += static_cast<char>(0x80 | (u & 0x3F));
            }
            i += 2;
        } else {
            out += s[i++]; // a longer sequence or a broken byte: copied as it is
        }
    }
    return out;
}

} // namespace ableem
