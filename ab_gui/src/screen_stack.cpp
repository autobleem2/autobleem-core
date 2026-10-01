// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::ScreenStack: clear, draw, present. See the header.
//
#include <ab_gui/screen_stack.h>

#include <ableem/engine/ext_trace.h>
#include <string>

namespace abgui {

namespace {

const ableem::Color OpaqueBlack(0, 0, 0, 255);

// the program's display: the renderer's own clear and present, so a frame through the stack is exactly the
// calls a screen made itself - the capture (captureNextFrame) and the DebugDriver's frame cache see it the same
class RendererDisplay : public ScreenStack::Display {
public:
    explicit RendererDisplay(ableem::Renderer &renderer) : renderer_(renderer) {}
    void setClearColor(const ableem::Color &color) override { renderer_.setDrawColor(color); }
    void clear() override { renderer_.clear(); }
    void present() override { renderer_.present(); }

private:
    ableem::Renderer &renderer_;
};

// depth_ back down however the drawing ends
class DepthScope {
public:
    explicit DepthScope(int &depth) : depth_(depth) { ++depth_; }
    ~DepthScope() { --depth_; }
    DepthScope(const DepthScope &) = delete;
    DepthScope &operator=(const DepthScope &) = delete;

private:
    int &depth_;
};

// the extension hand-off trap (ext_trace.h): a frame through the stack, however the drawing ends
class TraceFrame {
public:
    TraceFrame() { ableem::ext_trace::frameBegin(); }
    ~TraceFrame() { ableem::ext_trace::frameEnd(); }
    TraceFrame(const TraceFrame &) = delete;
    TraceFrame &operator=(const TraceFrame &) = delete;
};

} // namespace

ScreenStack::ScreenStack(ableem::Renderer &renderer)
    : own_(new RendererDisplay(renderer)), display_(own_.get()), busy_(*this), tweens_(new Tweens) {}

ScreenStack::ScreenStack(Display &display) : display_(&display), busy_(*this), tweens_(new Tweens) {}

ScreenStack::~ScreenStack() = default;

//*******************************
// ScreenStack::frame
//*******************************
void ScreenStack::frame(const Draw &draw) {
    run(nullptr, draw);
}

void ScreenStack::frame(const ableem::Color &clearColor, const Draw &draw) {
    run(&clearColor, draw);
}

void ScreenStack::run(const ableem::Color *clearColor, const Draw &draw) {
    // the tweens to this frame's time (G5o1), outside the frame: an end callback may start one of its own
    if (depth_ == 0)
        tweens_->update();
    TraceFrame traceFrame; // the hand-off trap (BUG-31): a clear or present outside the stack is marked
    if (ableem::ext_trace::active())
        ableem::ext_trace::note("stack frame depth=" + std::to_string(depth_) + " tweens=" +
                                std::to_string(tweens_->count()) + " clear=" +
                                (clearColor ? "own" : "opaque black"));
    {
        DepthScope scope(depth_);
        // never the colour a last drawing left set (BUG-31: a hint's white or a bar's fill cleared a frame white, and
        // that frame was presented before anything opaque covered it): a screen that wants another says so
        display_->setClearColor(clearColor ? *clearColor : OpaqueBlack);
        display_->clear();
        if (draw)
            draw();
    }
    display_->present();
    presented_++;
}

} // namespace abgui
