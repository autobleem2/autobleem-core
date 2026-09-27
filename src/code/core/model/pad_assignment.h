//
// Which PS1 controller port a connected pad lands on.
//
#pragma once

#include <string>
#include <vector>

// Both PS1 emulators we ship (pcsx-ab, pcsx-abnxt) assign port 1/2 by ascending SDL joystick
// device-index at the moment they start (no GUID pinning, no saved order - see
// in_sdl2gc_probe()/check_and_reprobe() in each emulator's frontend/libpicofe/in_sdl2gc.c).
// ableem::Input::pads() enumerates in that same ascending-index order, so `index` here is a pad's
// position in that vector (0-based) - what pcsx-ab/abnxt will call port `index + 1`.
//
// A plain enum, not a string: the label text is a UI concern (translated at the call site with a
// literal _("Player 1") / _("Player 2") / _("not used by the PS1 emulator") - a runtime string
// through _() is invisible to tools/lang_tools.py's extract, which only recognises a literal).
enum class PsPlayerSlot {
    Player1,
    Player2,
    Unused, // a third+ pad, or an out-of-range position
};

// `count` is how many pads are connected right now; an out-of-range `index` (negative, or >= count)
// is Unused too.
inline PsPlayerSlot psPlayerSlot(int index, int count) {
    if (index < 0 || index >= count)
        return PsPlayerSlot::Unused;
    if (index == 0)
        return PsPlayerSlot::Player1;
    if (index == 1)
        return PsPlayerSlot::Player2;
    return PsPlayerSlot::Unused;
}

// C11: the same question with Options -> "Swap Player 1 / Player 2" taken into account. `swapped` is a
// *positional* swap, nothing more - it says "SDL index 0 and SDL index 1 trade places", exactly what
// AB_PAD_ORDER="1,0" tells each emulator to do (LaunchService, core/services/launch.cpp) - there is no pad
// identity here (no GUID/serial pinning): a hot-plug still reorders exactly as it does today, only mirrored
// through whichever slot is on top. **The swap only takes effect with two or more pads connected**
// (`count >= 2`) - with a lone pad it is always Player 1, swap or not, so a player who left the row on and
// plays alone still gets a game that responds (both emulators' own C11 code follows the same rule: pcsx-ab's
// in_sdl2gc_probe() counts the pads it is about to accept before it decides whether to apply pad_order,
// pcsx-abnxt's pads_changed() checks its pad_count the same way). So with two-plus pads, index 0 is Player 2
// and index 1 is Player 1 when swapped; with zero or one pad, or index out of range, this is identical to
// the unswapped 2-arg form.
inline PsPlayerSlot psPlayerSlot(int index, int count, bool swapped) {
    if (!swapped || count < 2)
        return psPlayerSlot(index, count);
    if (index < 0 || index >= count)
        return PsPlayerSlot::Unused;
    if (index == 0)
        return PsPlayerSlot::Player2;
    if (index == 1)
        return PsPlayerSlot::Player1;
    return PsPlayerSlot::Unused;
}

// The Player 1 / Player 2 pads right now, as opaque identifiers (the caller's choice of what
// identifies a physical pad - e.g. its GUID plus its name; this struct never looks inside them,
// only compares them). Used to tell whether a pad (dis)connect actually changed who plays as P1/P2,
// so a UI's "pad connected" hint does not fire on every SDL re-enumeration - only when what it would
// show has changed from what was last shown.
struct PadAssignment {
    std::vector<std::string> ids; // ids[0] = Player 1's pad id (if any), ids[1] = Player 2's (if any)

    bool operator==(const PadAssignment &other) const { return ids == other.ids; }
    bool operator!=(const PadAssignment &other) const { return !(*this == other); }
    bool empty() const { return ids.empty(); }
};

// decidePadAssignmentChange()'s answer: whether to show `current` as a new assignment, and whether
// this call's "no pads" reading should count as already-suppressed the next time round.
struct PadAssignmentDecision {
    bool show = false;
    bool suppressedEmpty = false;
};

// Whether a newly observed assignment (`current`) is worth telling the user about, given the last
// one actually shown (`lastShown`) and whether the reading right before this one was suppressed for
// being momentarily empty (`previousWasSuppressedEmpty` - the caller passes back this call's own
// `suppressedEmpty`, carried from the previous call).
//
// SDL fires PadAdded for every pad already present at start-up, and a re-enumeration (unplug/replug,
// or the display release/reacquire around a game launch) can pass through an instant with no pads
// enumerated at all before they reappear - neither should pop a notification on its own:
//  - `current` unchanged from `lastShown` -> never shown;
//  - `current` newly empty, and the previous reading was not already a suppressed-empty one -> not
//    shown yet, but remembered (suppressedEmpty = true) so a *second* consecutive empty reading (the
//    pads really are gone, not just re-enumerating) is shown as "no controllers" rather than silently
//    swallowed forever;
//  - anything else different from `lastShown` -> shown.
inline PadAssignmentDecision decidePadAssignmentChange(const PadAssignment &current, const PadAssignment &lastShown,
                                                       bool previousWasSuppressedEmpty) {
    if (current == lastShown)
        return {false, false};
    if (current.empty() && !previousWasSuppressedEmpty)
        return {false, true};
    return {true, false};
}
