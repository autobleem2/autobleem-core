// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Tween, Timeline, TweenOwner and Tweens: the easings, the pure time rules and the runner. See the header.
//
#include <ab_gui/tween.h>

#include <ab_gui/context.h>

#include <ableem/ui/debug_driver.h>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace std;

namespace abgui {

//*******************************
// the easings
//*******************************
namespace ease {

float linear(float t) {
    return t;
}

// core/model/timing.h's easeOutCubic, word for word (test_ab_gui_tween compares the two)
float outCubic(float t) {
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float inCubic(float t) {
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    return t * t * t;
}

float inOutCubic(float t) {
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    if (t < 0.5f)
        return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u / 2.0f;
}

float outBack(float t) {
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    const float c1 = OvershootStrength;
    const float c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// pulseWave's cosine over one period; t 1 is the next period's start, 0 as pulseWave's phase 0 is
float pulse(float t) {
    if (t <= 0.0f || t >= 1.0f)
        return 0.0f;
    return 0.5f - 0.5f * std::cos(6.2831853f * t);
}

} // namespace ease

float tweenLerp(float from, float to, float eased) {
    if (eased == 0.0f)
        return from;
    if (eased == 1.0f)
        return to;
    return from + (to - from) * eased;
}

namespace {

// a + b, Forever when either is or when the sum would pass it
unsigned int addTime(unsigned int a, unsigned int b) {
    if (a == Tween::Forever || b == Tween::Forever)
        return Tween::Forever;
    const unsigned long long sum = static_cast<unsigned long long>(a) + b;
    return sum >= Tween::Forever ? Tween::Forever : static_cast<unsigned int>(sum);
}

int needRank(ableem::Input::FrameNeed need) {
    switch (need) {
    case ableem::Input::FrameNeed::Active:
        return 2;
    case ableem::Input::FrameNeed::Ambient:
        return 1;
    default:
        return 0;
    }
}

} // namespace

//*******************************
// Tween
//*******************************
constexpr unsigned int Tween::Forever;

Tween::Tween(float &target, float from, float to, unsigned int durationMs)
    : target_(&target), from_(from), to_(to), duration_(durationMs), easing_(&abgui::ease::outCubic) {}

Tween &Tween::delay(unsigned int ms) {
    delay_ = ms;
    return *this;
}

Tween &Tween::ease(Easing curve) {
    easing_ = curve ? curve : &abgui::ease::linear;
    return *this;
}

Tween &Tween::loop() {
    loop_ = true;
    return *this;
}

Tween &Tween::yoyo() {
    yoyo_ = true;
    return *this;
}

Tween &Tween::ambient() {
    ambient_ = true;
    return *this;
}

Tween &Tween::onEnd(function<void()> callback) {
    onEnd_ = move(callback);
    return *this;
}

unsigned int Tween::length() const {
    if (loop_ && duration_ > 0)
        return Forever;
    return addTime(delay_, yoyo_ ? addTime(duration_, duration_) : duration_);
}

bool Tween::endedAt(unsigned int elapsedMs) const {
    const unsigned int len = length();
    return len != Forever && elapsedMs >= len;
}

float Tween::valueAt(unsigned int elapsedMs) const {
    if (elapsedMs < delay_)
        return from_;
    unsigned long long gone = elapsedMs - delay_;
    const unsigned long long d = duration_;
    // the end: the curve's end (1 - `to` - for every easing but pulse, which comes back to 0), where a yoyo started
    const float end = tweenLerp(from_, to_, yoyo_ ? easing_(0.0f) : easing_(1.0f));
    if (d == 0)
        return end; // a zero-length tween (loop or not) is straight at its end
    const unsigned long long cycle = yoyo_ ? 2 * d : d;
    if (!loop_ && gone >= cycle)
        return end;
    if (loop_)
        gone %= cycle;
    const unsigned long long at = yoyo_ && gone > d ? cycle - gone : gone; // the way back: the curve run backwards
    const float t = static_cast<float>(at) / static_cast<float>(d);
    return tweenLerp(from_, to_, easing_(t));
}

//*******************************
// Timeline
//*******************************
Timeline &Timeline::add(const Tween &tween) {
    Step step;
    step.tween = make_shared<Tween>(tween);
    steps_.push_back(move(step));
    return *this;
}

Timeline &Timeline::add(const Timeline &timeline) {
    Step step;
    step.timeline = make_shared<Timeline>(timeline);
    steps_.push_back(move(step));
    return *this;
}

Timeline &Timeline::wait(unsigned int ms) {
    Step step;
    step.wait = ms;
    steps_.push_back(move(step));
    return *this;
}

Timeline &Timeline::delay(unsigned int ms) {
    delay_ = ms;
    return *this;
}

Timeline &Timeline::onEnd(function<void()> callback) {
    onEnd_ = move(callback);
    return *this;
}

unsigned int Timeline::length() const {
    unsigned int steps = 0;
    for (const Step &step : steps_) {
        const unsigned int len = step.tween      ? step.tween->length()
                                 : step.timeline ? step.timeline->length()
                                                 : step.wait;
        steps = order_ == Order::Sequence ? addTime(steps, len) : max(steps, len);
    }
    return addTime(delay_, steps);
}

//*******************************
// TweenOwner
//*******************************
TweenOwner::TweenOwner() : token_(make_shared<char>(0)) {}

void TweenOwner::cancel() {
    token_ = make_shared<char>(0); // the tweens' weak references to the old token expire with it
}

ableem::Input::FrameNeed strongerFrameNeed(ableem::Input::FrameNeed a, ableem::Input::FrameNeed b) {
    return needRank(a) >= needRank(b) ? a : b;
}

//*******************************
// Tweens: the runs
//*******************************
// A started tween or timeline is a run: its tweens flattened into leaves, each with its start in the run's time
// (the timelines' delays and the sequences' order summed in), and the timelines' end callbacks, each at its end.
namespace {

struct Leaf {
    Leaf(const Tween &tween, unsigned int offset, int depth) : tween(tween), offset(offset), depth(depth) {}
    Tween tween;
    unsigned int offset; // when it starts in the run's time (its own delay comes on top); Forever = never
    int depth;           // how deep in the timelines it sits (for the callbacks' order at the same time)
    bool ended = false;
};

struct GroupEnd {
    GroupEnd(unsigned int offset, int depth, function<void()> callback)
        : offset(offset), depth(depth), callback(move(callback)) {}
    unsigned int offset; // when the timeline ends in the run's time; Forever = never
    int depth;
    function<void()> callback;
    bool fired = false;
};

struct Run {
    TweenId id = NoTween;
    unsigned int start = 0;
    bool owned = false;
    weak_ptr<char> owner;
    bool ambient = true; // no leaf holds busy
    vector<Leaf> leaves;
    vector<GroupEnd> ends;

    bool orphaned() const { return owned && owner.expired(); }
    // something is left to write or to call
    bool live() const {
        if (orphaned())
            return false;
        for (const Leaf &leaf : leaves)
            if (!leaf.ended && leaf.offset != Tween::Forever)
                return true;
        for (const GroupEnd &end : ends)
            if (!end.fired && end.offset != Tween::Forever)
                return true;
        return false;
    }
};

// what ended, to be called once the pass is over
struct Pending {
    unsigned int ago; // how long before the pass's time it ended (the run's own time: no wrap to fear)
    int depth;
    size_t order;
    bool owned;
    weak_ptr<char> owner;
    function<void()> callback;
};

// the timeline's leaves and end into `run` from `base`; returns when it ends
unsigned int flatten(const Timeline &timeline, unsigned int base, int depth, Run &run) {
    const unsigned int start = addTime(base, timeline.delay());
    unsigned int cursor = start;
    unsigned int last = start;
    for (const Timeline::Step &step : timeline.steps()) {
        const unsigned int stepStart = timeline.order() == Timeline::Order::Sequence ? cursor : start;
        unsigned int stepEnd;
        if (step.tween) {
            run.leaves.push_back(Leaf(*step.tween, stepStart, depth + 1));
            if (step.tween->holdsBusy())
                run.ambient = false;
            stepEnd = addTime(stepStart, step.tween->length());
        } else if (step.timeline) {
            stepEnd = flatten(*step.timeline, stepStart, depth + 1, run);
        } else {
            stepEnd = addTime(stepStart, step.wait);
        }
        cursor = stepEnd;
        last = max(last, stepEnd);
    }
    const unsigned int end = timeline.order() == Timeline::Order::Sequence ? cursor : last;
    if (timeline.endCallback())
        run.ends.push_back(GroupEnd(end, depth, timeline.endCallback()));
    return end;
}

// one run at `elapsed` of its time: writes its tweens, marks what ended and queues their callbacks. `finishing`: an
// endless or never-starting leaf is dropped (it has no end to jump to)
void advance(Run &run, unsigned int elapsed, bool finishing, vector<Pending> &pending) {
    for (Leaf &leaf : run.leaves) {
        if (leaf.ended)
            continue;
        if (leaf.offset == Tween::Forever || elapsed < leaf.offset) {
            if (finishing)
                leaf.ended = true;
            continue;
        }
        const unsigned int local = elapsed - leaf.offset;
        if (finishing && leaf.tween.length() == Tween::Forever) {
            leaf.ended = true; // a loop has no end: dropped where it is
            continue;
        }
        if (!leaf.tween.startedAt(local))
            continue;
        *leaf.tween.target() = leaf.tween.valueAt(local);
        if (leaf.tween.endedAt(local)) {
            leaf.ended = true;
            if (leaf.tween.endCallback())
                pending.push_back(Pending{local - leaf.tween.length(), leaf.depth, 0, run.owned, run.owner,
                                          leaf.tween.endCallback()});
        }
    }
    for (GroupEnd &end : run.ends) {
        if (end.fired)
            continue;
        if (end.offset == Tween::Forever) {
            if (finishing)
                end.fired = true; // never comes
            continue;
        }
        if (elapsed >= end.offset) {
            end.fired = true;
            pending.push_back(Pending{elapsed - end.offset, end.depth, 0, run.owned, run.owner, end.callback});
        }
    }
}

// the run's last finite moment: where finish() jumps to
unsigned int finiteEnd(const Run &run) {
    unsigned int end = 0;
    for (const Leaf &leaf : run.leaves) {
        const unsigned int at = addTime(leaf.offset, leaf.tween.length());
        if (at != Tween::Forever)
            end = max(end, at);
    }
    for (const GroupEnd &group : run.ends)
        if (group.offset != Tween::Forever)
            end = max(end, group.offset);
    return end;
}

// the queued callbacks in the order they ended: the earliest first, at the same time the inner first, then as queued
void sortByEnd(vector<Pending> &pending) {
    for (size_t i = 0; i < pending.size(); ++i)
        pending[i].order = i;
    stable_sort(pending.begin(), pending.end(), [](const Pending &a, const Pending &b) {
        if (a.ago != b.ago)
            return a.ago > b.ago;
        if (a.depth != b.depth)
            return a.depth > b.depth;
        return a.order < b.order;
    });
}

// calls them as they stand; an owner gone meanwhile - a callback before closed its screen - skips its own
void callAll(const vector<Pending> &pending) {
    for (const Pending &p : pending) {
        if (p.owned && p.owner.expired())
            continue;
        p.callback();
    }
}

} // namespace

struct Tweens::Impl {
    vector<Run> runs;
    TweenId nextId = 1;
    unsigned int lastNow = 0;
};

Tweens::Tweens() : impl_(new Impl) {}

Tweens::~Tweens() {
    if (busyHeld_)
        ableem::DebugDriver::setBusy(false);
}

void Tweens::bind(const Context &ctx) {
    ctx_ = &ctx;
}

unsigned int Tweens::now() const {
    if (clock)
        return clock();
    if (ctx_)
        return ctx_->ticks();
    return impl_->lastNow;
}

//*******************************
// Tweens::start
//*******************************
TweenId Tweens::start(const Tween &tween) {
    return add(Timeline().add(tween), nullptr, now());
}

TweenId Tweens::start(const Tween &tween, const TweenOwner &owner) {
    return add(Timeline().add(tween), &owner, now());
}

TweenId Tweens::start(const Timeline &timeline) {
    return add(timeline, nullptr, now());
}

TweenId Tweens::start(const Timeline &timeline, const TweenOwner &owner) {
    return add(timeline, &owner, now());
}

TweenId Tweens::startAt(unsigned int startedAt, const Timeline &timeline, const TweenOwner &owner) {
    return add(timeline, &owner, startedAt);
}

TweenId Tweens::add(const Timeline &timeline, const TweenOwner *owner, unsigned int startedAt) {
    Run run;
    run.id = impl_->nextId++;
    run.start = startedAt;
    if (owner) {
        run.owned = true;
        run.owner = owner->token_;
    }
    flatten(timeline, 0, 0, run);
    const TweenId id = run.id;
    impl_->runs.push_back(move(run));
    syncBusy();
    return id;
}

//*******************************
// Tweens::update / cancel / finish
//*******************************
void Tweens::update() {
    update(now());
}

void Tweens::update(unsigned int now) {
    impl_->lastNow = now;
    vector<Pending> pending;
    vector<Run> &runs = impl_->runs;
    for (Run &run : runs)
        if (!run.orphaned())
            advance(run, now - run.start, false, pending);
    runs.erase(remove_if(runs.begin(), runs.end(), [](const Run &run) { return !run.live(); }), runs.end());
    syncBusy();
    sortByEnd(pending);
    callAll(pending); // last: a callback may start, cancel or finish runs
}

bool Tweens::cancel(TweenId id) {
    vector<Run> &runs = impl_->runs;
    auto it = find_if(runs.begin(), runs.end(), [id](const Run &run) { return run.id == id; });
    if (it == runs.end())
        return false;
    const bool wasLive = it->live();
    runs.erase(it);
    syncBusy();
    return wasLive;
}

bool Tweens::finish(TweenId id) {
    vector<Run> &runs = impl_->runs;
    auto it = find_if(runs.begin(), runs.end(), [id](const Run &run) { return run.id == id; });
    if (it == runs.end())
        return false;
    Run run = move(*it);
    runs.erase(it);
    vector<Pending> pending;
    const bool wasLive = run.live();
    if (wasLive) {
        advance(run, finiteEnd(run), true, pending);
        sortByEnd(pending);
    }
    syncBusy();
    callAll(pending);
    return wasLive;
}

void Tweens::finishNonAmbient() {
    vector<Run> &runs = impl_->runs;
    vector<Run> finishing;
    for (auto it = runs.begin(); it != runs.end();) {
        if (!it->ambient) {
            finishing.push_back(move(*it));
            it = runs.erase(it);
        } else {
            ++it;
        }
    }
    // run by run in the order they were started, each run's callbacks in the order they end in it
    vector<Pending> pending;
    for (Run &run : finishing) {
        if (!run.live())
            continue;
        vector<Pending> own;
        advance(run, finiteEnd(run), true, own);
        sortByEnd(own);
        pending.insert(pending.end(), own.begin(), own.end());
    }
    syncBusy();
    callAll(pending);
}

void Tweens::clear() {
    impl_->runs.clear();
    syncBusy();
}

//*******************************
// Tweens: the questions
//*******************************
bool Tweens::running(TweenId id) const {
    for (const Run &run : impl_->runs)
        if (run.id == id)
            return run.live();
    return false;
}

bool Tweens::busy() const {
    for (const Run &run : impl_->runs)
        if (!run.ambient && run.live())
            return true;
    return false;
}

bool Tweens::animating() const {
    for (const Run &run : impl_->runs)
        if (run.live())
            return true;
    return false;
}

size_t Tweens::count() const {
    size_t n = 0;
    for (const Run &run : impl_->runs)
        if (run.live())
            ++n;
    return n;
}

ableem::Input::FrameNeed Tweens::frameNeed() const {
    if (busy())
        return ableem::Input::FrameNeed::Active;
    if (animating())
        return ableem::Input::FrameNeed::Ambient;
    return ableem::Input::FrameNeed::Idle;
}

void Tweens::applyFrameNeed(ableem::Input &input) const {
    const ableem::Input::FrameNeed need = strongerFrameNeed(input.frameNeed(), frameNeed());
    if (need != input.frameNeed())
        input.setFrameNeed(need);
}

// the DebugDriver's busy counter: one step up while a non-ambient run is running, back down when none is
void Tweens::syncBusy() {
    const bool busyNow = busy();
    if (busyNow == busyHeld_)
        return;
    busyHeld_ = busyNow;
    ableem::DebugDriver::setBusy(busyNow);
}

} // namespace abgui
