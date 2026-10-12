#include "ableem/engine/clean_game_name.h"

#include <cctype>

namespace ableem {

namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

// the position of the bracket that opens the group closing at s[close], npos when it never opens
size_t groupStart(const std::string &s, size_t close) {
    char closer = s[close];
    char opener = closer == ')' ? '(' : '[';
    int depth = 0;
    for (size_t i = close + 1; i-- > 0;) {
        if (s[i] == closer)
            depth++;
        else if (s[i] == opener && --depth == 0)
            return i;
    }
    return std::string::npos;
}

} // namespace

std::string cleanGameName(const std::string &name) {
    std::string s = name;
    for (;;) {
        while (!s.empty() && isSpace(s.back()))
            s.pop_back();
        if (s.empty() || (s.back() != ')' && s.back() != ']'))
            break;
        size_t start = groupStart(s, s.size() - 1);
        if (start == std::string::npos)
            break;
        s.erase(start);
    }

    std::string result;
    for (char c : s) {
        if (isSpace(c)) {
            if (!result.empty() && result.back() != ' ')
                result += ' ';
        } else {
            result += c;
        }
    }
    if (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result.empty() ? name : result;
}

} // namespace ableem
