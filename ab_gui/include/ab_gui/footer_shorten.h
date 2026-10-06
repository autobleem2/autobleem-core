// SPDX-License-Identifier: GPL-3.0-or-later
//
// Footer label shortening: when a footer's hints do not fit even in the smallest font, the labels are cut
// ("Back" -> "B..") before the footer falls back to bare button chips (abgui::Style::footer). Pure
// string/width logic - the width measure is passed in, so it is tested without a font
// (tests/classic/test_panel_style_footer.cpp).
//
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace abgui {

// code points in a UTF-8 string (continuation bytes 10xxxxxx are not counted)
inline size_t footerUtf8Length(const std::string &s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80)
            n++;
    return n;
}

// the first `count` code points of `s`
inline std::string footerUtf8Prefix(const std::string &s, size_t count) {
    size_t n = 0, i = 0;
    while (i < s.size()) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (n == count)
                break;
            n++;
        }
        i++;
    }
    return s.substr(0, i);
}

// Shortens `labels` (one per hint) until `measure(labels)` - the width of the whole hint row - is <= `room`.
// Each step cuts one code point from the longest label (the last of equals), which then ends in "..";
// a label keeps at least MinLetters letters before the dots, and one already that short is left alone.
// Returns true when the row fits (labels unchanged if it fit at once); false when even the shortest cut
// does not fit - `labels` then holds the shortest form and the caller draws icons only.
constexpr size_t FooterMinLetters = 2;

inline bool shortenFooterLabels(std::vector<std::string> &labels, int room,
                                const std::function<int(const std::vector<std::string> &)> &measure) {
    if (measure(labels) <= room)
        return true;
    const std::vector<std::string> full = labels;
    std::vector<size_t> kept(labels.size()); // letters kept before the dots; 0 = not cut yet
    for (;;) {
        // the longest as displayed now (code points), among those a further cut keeps MinLetters letters of
        size_t best = labels.size(), bestLen = 0, bestNext = 0;
        for (size_t i = 0; i < labels.size(); i++) {
            const size_t total = footerUtf8Length(full[i]);
            const size_t next = kept[i] == 0 ? (total > 3 ? total - 3 : 0) : kept[i] - 1; // a first cut must gain
            if (next < FooterMinLetters)
                continue;
            const size_t len = footerUtf8Length(labels[i]);
            if (len >= bestLen) {
                best = i;
                bestLen = len;
                bestNext = next;
            }
        }
        if (best == labels.size())
            return false;
        kept[best] = bestNext;
        std::string letters = footerUtf8Prefix(full[best], bestNext);
        while (!letters.empty() && letters.back() == ' ')
            letters.pop_back(); // no "Play .."
        labels[best] = letters + "..";
        if (measure(labels) <= room)
            return true;
    }
}

// The 4:3 footer rule (CONSOLE-17): the hints arrive most important first, so when the row does not fit the LAST ones
// are left out (they keep working - only their label is not shown), the way the Store's 4:3 footer does. Returns how
// many leading hints fit in `room` (`measure(n)` = the width of the first n hints), at least 1 when even one fits,
// 0 when not even the first does - the caller then cuts its label instead.
inline size_t footerHintsThatFit(size_t count, int room, const std::function<int(size_t)> &measure) {
    for (size_t n = count; n > 0; n--)
        if (measure(n) <= room)
            return n;
    return 0;
}

} // namespace abgui
