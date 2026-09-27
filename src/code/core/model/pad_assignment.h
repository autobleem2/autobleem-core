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

// decidePadAssignmentChange()'s answer: whether to show `current` (or, from checkPadAssignmentEmptyNotice(),
// whether to show "no controllers") right now.
struct PadAssignmentDecision {
    bool show = false;
};

// Carries what a caller needs across calls: the assignment last actually shown, and - while an empty
// reading is pending a decision - the tick it was first seen (0 = nothing pending). Kept by the caller,
// one instance per place that tracks "what's shown" (the launcher keeps exactly one).
struct PadAssignmentState {
    PadAssignment lastShown;
    long emptySince = 0;
};

// Whether a newly observed assignment (`current`) is worth telling the user about *right now*, given
// what has been shown so far (`state`, updated in place) and the current time `now` (an opaque,
// monotonically increasing tick count - the launcher's frame clock).
//
// SDL fires PadAdded for every pad already present at start-up, and a re-enumeration (unplug/replug, or
// the display release/reacquire around a game launch) can pass through an instant with no pads
// enumerated at all before they reappear - neither should pop a notification on its own. But unlike a
// two-pad change (still non-empty, so still an ordinary comparison against `lastShown`), an unplug that
// leaves *zero* pads can never be told apart, from a single event alone, from a blip that will resolve
// itself a moment later: SDL sends exactly one PadRemoved for a lone pad's unplug, never a second
// "still gone" event to confirm it. So an empty reading is never shown here - it only starts (or leaves
// running) a pending timer in `state.emptySince`; checkPadAssignmentEmptyNotice(), called once a frame
// regardless of events, is what actually shows "no controllers" once that timer expires.
//  - `current` unchanged from `lastShown` -> never shown; clears any pending timer (the assignment the
//    user already sees is exactly what's connected again - see "a pad that comes back" below);
//  - `current` empty and different from `lastShown` -> not shown yet; `state.emptySince` is set to `now`
//    if nothing was already pending, otherwise left alone (the clock runs from the *first* empty
//    reading, not the latest one an SDL re-enumeration burst might repeat);
//  - anything else different from `lastShown` (including a still non-empty change, e.g. one of two pads
//    unplugged) -> shown at once, `lastShown` updated, any pending timer cleared.
inline PadAssignmentDecision decidePadAssignmentChange(const PadAssignment &current, PadAssignmentState &state,
                                                        long now) {
    if (current == state.lastShown) {
        state.emptySince = 0;
        return {false};
    }
    if (current.empty()) {
        if (state.emptySince == 0)
            state.emptySince = now;
        return {false};
    }
    state.lastShown = current;
    state.emptySince = 0;
    return {true};
}

// Called once a frame (regardless of any pad event, the way the launcher already polls pad battery
// levels) to see whether a pending empty reading has been pending long enough - `delay` ticks since
// `state.emptySince` - to show as "no controllers". A pad that reappears before then goes through
// decidePadAssignmentChange() above, which clears `state.emptySince` first, so this never fires for a
// re-enumeration blip. Shown at most once per empty spell: firing it also sets `lastShown` to the empty
// assignment, so a later frame with nothing changed takes the "unchanged" branch above instead of
// repeating the notice; a real pad returning afterwards is then a `current != lastShown` change again -
// shown at once, as any other new assignment is.
inline PadAssignmentDecision checkPadAssignmentEmptyNotice(PadAssignmentState &state, long now, long delay) {
    if (state.emptySince == 0 || now - state.emptySince < delay)
        return {false};
    state.lastShown = PadAssignment{};
    state.emptySince = 0;
    return {true};
}
