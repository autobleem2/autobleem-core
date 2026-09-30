// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two small pure rect rules of the lists (ab_gui G5t): where a row's trailing badge sits, and a box centred on a panel.
// Header-only, so a program (the Store extension) and the tests share the one formula.
//
#pragma once

#include <ableem/ui/types.h>

namespace abgui {

// how far a row's trailing badge sits from the list panel's inner right edge (logical px)
constexpr int BadgeInset = 24;

// The badge of a row, `w` x `h`, at its right end: its right edge `inset` px inside `innerRight` (the list panel's
// inner right edge, an x), vertically centred in the row (`rowTop`, `rowHeight`; an odd leftover px goes below).
inline ableem::Rect trailingBadgeRect(int innerRight, int rowTop, int rowHeight, int w, int h, int inset = BadgeInset) {
    return ableem::Rect(innerRight - inset - w, rowTop + (rowHeight - h) / 2, w, h);
}

// a box of `w` x `h` centred on `outer` (an odd leftover px goes right and below); the box may be bigger than `outer`
inline ableem::Rect centredIn(const ableem::Rect &outer, int w, int h) {
    return ableem::Rect(outer.x + (outer.w - w) / 2, outer.y + (outer.h - h) / 2, w, h);
}

} // namespace abgui
