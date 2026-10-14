//
// BUG-10: test_sdk_layout compares sizes and offsets, which a class cannot change by moving its virtual functions out
// of (or into) a .cpp: GuiActionMenu lost its out-of-line vtable in G3z and an extension built before it failed in
// dlopen ("undefined symbol: _ZTV13GuiActionMenu"). This reads the SDK headers of the polymorphic classes an
// extension subclasses or calls and hashes what decides their vtable: the class's declaration (its bases) and every
// line of its body that says virtual, override or final (with the body, when it is inline). A change fails until
// AB_SDK_ABI is bumped and sdk_vtable_table.h updated. Headers are read as text, so it runs on every host.
//
#include "doctest/doctest.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gui/extension.h"

#ifndef AB_TEST_DATA_DIR
#error "AB_TEST_DATA_DIR comes from ab_add_test"
#endif

namespace {

// the core's root: AB_TEST_DATA_DIR is <root>/tests/data
std::string coreRoot() {
    return std::string(AB_TEST_DATA_DIR) + "/../..";
}

std::vector<std::string> linesOf(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
        lines.push_back(line);
    return lines;
}

// a line without its // comment, whitespace squeezed to single spaces and trimmed
std::string normal(const std::string &line) {
    std::string code = line.substr(0, line.find("//"));
    std::string out;
    for (char c : code) {
        if (c == ' ' || c == '\t' || c == '\r') {
            if (!out.empty() && out.back() != ' ')
                out += ' ';
        } else {
            out += c;
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

bool hasWord(const std::string &line, const std::string &word) {
    for (size_t at = line.find(word); at != std::string::npos; at = line.find(word, at + 1)) {
        auto idChar = [](char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_'; };
        bool before = at == 0 || !idChar(line[at - 1]);
        bool after = at + word.size() >= line.size() || !idChar(line[at + word.size()]);
        if (before && after)
            return true;
    }
    return false;
}

// the line that declares `name` (class or struct, not a forward declaration)
size_t declarationLine(const std::vector<std::string> &lines, const std::string &name) {
    for (size_t i = 0; i < lines.size(); i++) {
        std::string l = normal(lines[i]);
        for (const char *kw : {"class ", "struct "}) {
            size_t at = l.find(std::string(kw) + name);
            if (at == std::string::npos || !hasWord(l.substr(at), name) || l.back() == ';')
                continue;
            if (at > 0 && l[at - 1] != ' ' && l[at - 1] != '>')
                continue;
            return i;
        }
    }
    return lines.size();
}

uint64_t fnv1a(const std::string &text) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// the declaration up to its '{' and the virtual / override / final lines of the body, as one text
std::string vtableSurface(const std::string &file, const std::string &name) {
    std::vector<std::string> lines = linesOf(coreRoot() + "/" + file);
    size_t first = declarationLine(lines, name);
    if (first == lines.size())
        return "";
    std::string surface;
    int depth = 0;
    bool opened = false;
    for (size_t i = first; i < lines.size(); i++) {
        std::string l = normal(lines[i]);
        if (!opened || hasWord(l, "virtual") || hasWord(l, "override") || hasWord(l, "final"))
            surface += l + "\n";
        for (char c : l) {
            if (c == '{') {
                depth++;
                opened = true;
            } else if (c == '}') {
                depth--;
            }
        }
        if (opened && depth <= 0)
            break;
    }
    return surface;
}

} // namespace

// X(file, class, expected hash)
#include "sdk_vtable_table.h"

TEST_CASE("the SDK's polymorphic classes keep their vtable surface (change it only with an AB_SDK_ABI bump)") {
    std::string changed;
#define X(file, name, expected)                                                                                      \
    {                                                                                                                \
        std::string surface = vtableSurface(file, name);                                                             \
        std::ostringstream hash;                                                                                     \
        hash << std::hex << fnv1a(surface);                                                                          \
        if (surface.empty())                                                                                         \
            changed += std::string("\n  ") + name + " (" + file + "): not found";                                    \
        else if (hash.str() != expected)                                                                             \
            changed += std::string("\n  ") + name + " (" + file + "): table " + expected + ", now " + hash.str();  \
    }
    AB_SDK_VTABLE_TABLE(X)
#undef X
    INFO("a polymorphic SDK class changed its bases or virtual functions (or moved them in or out of a .cpp) - bump "
         "AB_SDK_ABI (now " << AB_SDK_ABI << "), add a history line in gui/extension.h and update the table:"
                            << changed);
    CHECK(changed.empty());
}
