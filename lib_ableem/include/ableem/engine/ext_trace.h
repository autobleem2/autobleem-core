// lib_ableem - engine: the extension hand-off trap (BUG-31). OFF unless AB_TRACE_EXT is in the environment; then,
// for ~2 s from an extension screen being requested, opened or closed (begin()), one log line per presented frame
// says what the frame drew - tagged [TRACE_EXT], so `grep TRACE_EXT` on the log shows the sequence. When off, every
// call is one test of a cached flag. The renderer, the screen stack, the backdrop and the extension runtime feed it
// (note() while a frame is drawn, line() for a step); Renderer::present() ends the frame (frameDone()).
#pragma once

#include <string>

namespace ableem {

namespace ext_trace {

// AB_TRACE_EXT is set (read once)
bool enabled();
// enabled and inside a window: what callers test before building a note's text
bool active();

// milliseconds on a monotonic clock
long nowMs();

// opens (or restarts) a ~2 s window of per-frame lines, and logs `what` as the reason
void begin(const std::string &what);
// one line now, outside the frames (a load step, a timing); logged even outside a window while enabled
void line(const std::string &text);

// what the frame being drawn does, joined into its one line at frameDone()
void note(const std::string &text);
// Renderer::present(): the frame ends - its line is logged and the next frame starts
void frameDone();

// the extension's code is running (ExtensionRuntime::run/runEntry): its frames are marked, the first one specially
void setInExtension(bool running);
bool inExtension();

// a frame through abgui::ScreenStack is between frameBegin() and frameEnd(); a clear or present outside is a call
// "outside the loop" and is marked so
void frameBegin();
void frameEnd();
bool inStackFrame();

// logs a step with the time it took: "<what> took N ms"
class StepTimer {
public:
    explicit StepTimer(const char *what);
    ~StepTimer();
    StepTimer(const StepTimer &) = delete;
    StepTimer &operator=(const StepTimer &) = delete;

private:
    const char *what_;
    long startMs_;
};

} // namespace ext_trace

} // namespace ableem
