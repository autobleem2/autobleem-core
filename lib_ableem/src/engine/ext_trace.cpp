// lib_ableem - engine: the extension hand-off trap. See the header.
#include "ableem/engine/ext_trace.h"

#include <ableem/engine/log.h>
#include <chrono>
#include <cstdlib>

namespace ableem {

namespace ext_trace {

namespace {

const long WindowMs = 2000;

struct State {
    long windowEnd = 0; // nowMs() the window closes at; 0 = none
    long windowStart = 0;
    unsigned long frame = 0; // the frame in the window
    bool inExtension = false;
    bool extFrameSeen = false; // the extension's first frame of this run is behind us
    int stackDepth = 0;
    std::string notes; // the frame being drawn
};

State &state() {
    static State s;
    return s;
}

} // namespace

bool enabled() {
    static const bool on = std::getenv("AB_TRACE_EXT") != nullptr;
    return on;
}

long nowMs() {
    using namespace std::chrono;
    return static_cast<long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

bool active() {
    return enabled() && state().windowEnd != 0 && nowMs() < state().windowEnd;
}

void line(const std::string &text) {
    if (!enabled())
        return;
    PLOG_INFO << "[TRACE_EXT] t=" << nowMs() << "ms " << text;
}

void begin(const std::string &what) {
    if (!enabled())
        return;
    State &s = state();
    s.windowStart = nowMs();
    s.windowEnd = s.windowStart + WindowMs;
    s.frame = 0;
    s.notes.clear();
    line("--- window (" + std::to_string(WindowMs) + " ms): " + what);
}

void note(const std::string &text) {
    if (!active())
        return;
    State &s = state();
    s.notes += " | " + text;
}

void frameDone() {
    if (!active())
        return;
    State &s = state();
    std::string mark;
    if (s.inExtension && !s.extFrameSeen) {
        s.extFrameSeen = true;
        mark = " EXT-FIRST-FRAME";
    } else if (s.inExtension) {
        mark = " ext-frame";
    }
    line("frame " + std::to_string(s.frame++) + mark + s.notes);
    s.notes.clear();
}

void setInExtension(bool running) {
    if (!enabled())
        return;
    State &s = state();
    s.inExtension = running;
    if (running)
        s.extFrameSeen = false;
}

bool inExtension() {
    return state().inExtension;
}

void frameBegin() {
    state().stackDepth++;
}

void frameEnd() {
    if (state().stackDepth > 0)
        state().stackDepth--;
}

bool inStackFrame() {
    return state().stackDepth > 0;
}

StepTimer::StepTimer(const char *what) : what_(what), startMs_(enabled() ? nowMs() : 0) {
    if (enabled())
        line(std::string(what_) + " start");
}

StepTimer::~StepTimer() {
    if (enabled())
        line(std::string(what_) + " done, took " + std::to_string(nowMs() - startMs_) + " ms");
}

} // namespace ext_trace

} // namespace ableem
