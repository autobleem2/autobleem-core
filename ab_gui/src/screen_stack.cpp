// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::ScreenStack: clear, draw, present - and the screen transitions between the frames. See the header.
//
#include <ab_gui/screen_stack.h>

#include <ab_gui/screen.h>

#include <ableem/engine/ext_trace.h>
#include <ableem/ui/gui_screen.h>
#include <ableem/ui/input.h>
#include <ableem/ui/texture.h>

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace abgui {

namespace {

const ableem::Color OpaqueBlack(0, 0, 0, 255);
// the most frames a last screen's fade to black draws (ScreenStack::Transitions::playToBlack)
const int MaxFadeFrames = 600;

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

// drawing into a render target (pushTarget) until the scope ends, however the drawing ends; `offscreen` counts the
// targets open, so a frame nested in such a drawing goes to the screen instead (ScreenStack::run)
class TargetScope {
public:
    TargetScope(ableem::Renderer &renderer, ableem::Texture *target, int &offscreen)
        : renderer_(renderer), offscreen_(offscreen), counts_(target != nullptr) {
        renderer_.pushTarget(target);
        if (counts_)
            ++offscreen_;
    }
    ~TargetScope() {
        if (counts_)
            --offscreen_;
        renderer_.popTarget();
    }
    TargetScope(const TargetScope &) = delete;
    TargetScope &operator=(const TargetScope &) = delete;

private:
    ableem::Renderer &renderer_;
    int &offscreen_;
    bool counts_;
};

const ableem::Color &clearColorOf(const Screen &screen) {
    return screen.frameColor.set ? screen.frameColor.color : OpaqueBlack;
}

} // namespace

//********************
// ScreenStack::Transitions
//********************
// The screens shown (GuiScreen::show tells it as their observer), what they declared, the two pictures and the player.
struct ScreenStack::Transitions : public ableem::GuiScreenObserver {
    Transitions(ScreenStack &stack_, ableem::Renderer *renderer_)
        : stack(stack_), renderer(renderer_), player(*stack_.tweens_) {}

    ScreenStack &stack;
    ableem::Renderer *renderer; // none (a test's recording display): the transitions are timed but never composed
    TransitionPlayer player;
    ableem::Input *input = nullptr;

    // the screens shown, the first opened first; `drawer` is the screen as it draws (known from its first frame)
    struct Open {
        const ableem::GuiScreen *screen;
        Screen *drawer;
    };
    std::vector<Open> open;
    std::map<const ableem::GuiScreen *, ScreenTransitions> declarations;
    bool startSet = false;
    Transition start;

    // the two pictures: the old one (the screen under one that opens, or one that closed) and the new one's frames
    ableem::Texture oldPicture, newPicture;
    bool hasOld = false;
    unsigned long shown = 0;   // the frames shown on the window (presented() less the silent snapshots)
    unsigned long oldAt = 0;   // `shown` when the old picture was drawn
    unsigned long oldLost = 0; // the renderer's targetsLost() then
    int offscreen = 0;         // pictures being drawn into a target right now

    void screenOpens(ableem::GuiScreen &screen) override;
    void screenCloses(ableem::GuiScreen &screen) override;

    Open *find(const ableem::GuiScreen *screen) {
        for (auto it = open.rbegin(); it != open.rend(); ++it)
            if (it->screen == screen)
                return &*it;
        return nullptr;
    }
    bool target(ableem::Texture &t);
    bool drawInto(ableem::Texture &t, Screen &drawer);
    bool captureOld(Screen &drawer);
    bool oldUsable() const { return hasOld && renderer && oldLost == renderer->targetsLost(); }
    void drawLayer(ableem::Texture &picture, const TransitionLayer &layer);
    void composeOnScreen(bool withOld, bool withNew);
    void playToBlack(const ableem::GuiScreen &screen, Screen &drawer, const Transition &t);
};

bool ScreenStack::Transitions::target(ableem::Texture &t) {
    if (!renderer)
        return false;
    if (!t.valid()) {
        t = ableem::Texture::createTarget(*renderer, renderer->width(), renderer->height());
        if (!t.valid())
            return false;
    }
    return true;
}

// one picture of `drawer` into `t`: cleared to its colour, its drawing - what its frame would show
bool ScreenStack::Transitions::drawInto(ableem::Texture &t, Screen &drawer) {
    if (!target(t))
        return false;
    DepthScope depth(stack.depth_); // a frame started inside (a busy tick) is a nested one, presented on its own
    TargetScope into(*renderer, &t, offscreen);
    stack.display_->setClearColor(clearColorOf(drawer));
    stack.display_->clear();
    drawer.draw();
    return true;
}

bool ScreenStack::Transitions::captureOld(Screen &drawer) {
    hasOld = false;
    try {
        hasOld = drawInto(oldPicture, drawer);
    } catch (...) {
        hasOld = false; // the old picture is black then; the screen's own frames will show its error
    }
    if (hasOld) {
        oldAt = shown;
        oldLost = renderer->targetsLost();
    }
    return hasOld;
}

void ScreenStack::Transitions::drawLayer(ableem::Texture &picture, const TransitionLayer &layer) {
    if (!layer.drawn)
        return;
    picture.setBlendMode(ableem::BlendMode::Blend);
    picture.setAlphaMod(static_cast<unsigned char>(layer.alpha));
    renderer->copy(picture, nullptr, layer.rect);
    picture.setAlphaMod(255);
    if (layer.dim > 0) {
        renderer->setDrawColor(ableem::Color(0, 0, 0, static_cast<unsigned char>(layer.dim)));
        renderer->setBlendMode(ableem::BlendMode::Blend);
        renderer->fillRect(ableem::Rect(static_cast<int>(std::lround(layer.rect.x)),
                                        static_cast<int>(std::lround(layer.rect.y)),
                                        static_cast<int>(std::lround(layer.rect.w)),
                                        static_cast<int>(std::lround(layer.rect.h))));
    }
}

// the window: black, then the two pictures where the transition puts them now
void ScreenStack::Transitions::composeOnScreen(bool withOld, bool withNew) {
    stack.display_->setClearColor(OpaqueBlack);
    stack.display_->clear();
    const TransitionFrame f =
        composeTransition(player.transition(), player.backwards(), player.progress(),
                          static_cast<float>(renderer->width()), static_cast<float>(renderer->height()), withOld, withNew);
    if (f.oldOnTop) {
        drawLayer(newPicture, f.newPicture);
        drawLayer(oldPicture, f.oldPicture);
    } else {
        drawLayer(oldPicture, f.oldPicture);
        drawLayer(newPicture, f.newPicture);
    }
}

//*******************************
// screenOpens / screenCloses
//*******************************
void ScreenStack::Transitions::screenOpens(ableem::GuiScreen &screen) {
    const bool nothingUnder = open.empty();
    Transition t = stack.declared(screen).in;
    if (nothingUnder && startSet) {
        t = start; // once: the launcher after the splash
        startSet = false;
    }
    // what is on display now: the picture of a screen that closed, when nothing was presented since - else the screen
    // under this one, drawn again
    const bool oldOnDisplay = oldUsable() && oldAt == shown;
    player.finish();
    Screen *under = nothingUnder ? nullptr : open.back().drawer;
    open.push_back(Open{&screen, nullptr});
    if (!player.enabled() || !t.moves()) {
        hasOld = false;
        return;
    }
    if (!oldOnDisplay) {
        hasOld = false;
        if (under)
            captureOld(*under);
    }
    player.arm(&screen, t, false);
}

void ScreenStack::Transitions::screenCloses(ableem::GuiScreen &screen) {
    player.finish();
    Screen *drawer = nullptr;
    for (auto it = open.end(); it != open.begin();) {
        --it;
        if (it->screen == &screen) {
            drawer = it->drawer;
            open.erase(it);
            break;
        }
    }
    hasOld = false;
    const Transition t = stack.declared(screen).closing();
    if (!player.enabled() || !t.moves())
        return;
    if (open.empty()) {
        // nothing under it: only a fade to black means anything - played here, on its own frames (the splash)
        if (t.kind == TransitionKind::Fade && drawer)
            playToBlack(screen, *drawer, t);
        return;
    }
    if (!drawer)
        return; // it never drew a frame: there is no picture of it to take away
    captureOld(*drawer);
    player.arm(open.back().screen, t, true); // the screen under it, drawn live, comes back
}

// the last screen's out with nothing after it: its own picture, drawn every frame, fades to black before show() returns
// (a press, or a quit, ends it at once)
void ScreenStack::Transitions::playToBlack(const ableem::GuiScreen &screen, Screen &drawer, const Transition &t) {
    if (!renderer || !player.arm(&screen, t, true))
        return;
    // a clock that stands still never ends the tween: the frames are capped too (a fade is well under a second)
    for (int frames = 0; player.armed(); ++frames) {
        if (frames >= MaxFadeFrames) {
            player.finish();
            break;
        }
        stack.tweens_->update();
        if (!player.frame(&screen))
            break;
        {
            TraceFrame traceFrame;
            const bool live = captureOld(drawer);
            DepthScope depth(stack.depth_);
            composeOnScreen(live, false);
        }
        stack.display_->present();
        stack.presented_++;
        shown++;
        player.presented();
        if (input && (input->padEventPending() || input->quitRequested()))
            player.finish();
    }
    hasOld = false;
}

//********************
// ScreenStack
//********************
ScreenStack::ScreenStack(ableem::Renderer &renderer)
    : own_(new RendererDisplay(renderer)), display_(own_.get()), busy_(*this), tweens_(new Tweens),
      transitions_(new Transitions(*this, &renderer)) {}

ScreenStack::ScreenStack(Display &display)
    : display_(&display), busy_(*this), tweens_(new Tweens), transitions_(new Transitions(*this, nullptr)) {}

ScreenStack::~ScreenStack() {
    detach();
}

//*******************************
// ScreenStack::frame
//*******************************
void ScreenStack::frame(const Draw &draw) {
    run(nullptr, draw, nullptr);
}

void ScreenStack::frame(const ableem::Color &clearColor, const Draw &draw) {
    run(&clearColor, draw, nullptr);
}

void ScreenStack::screenFrame(Screen &screen) {
    if (Transitions::Open *open = transitions_->find(&screen))
        open->drawer = &screen; // what draws its picture when the next screen opens over it, or when it closes
    const Draw drawing = [&screen] { screen.draw(); };
    run(screen.frameColor.set ? &screen.frameColor.color : nullptr, drawing, &screen);
}

void ScreenStack::run(const ableem::Color *clearColor, const Draw &draw, Screen *screen) {
    Transitions &tr = *transitions_;
    bool composed = false;
    // a silent snapshot (the launcher's backdrop, taken while a menu is still up) is never shown: the transitions
    // neither start nor end on it, and the picture on display stays what it was
    const bool snapshot = tr.renderer && depth_ == 0 && tr.renderer->silentCapturePending();
    if (depth_ == 0) {
        // the tweens to this frame's time (G5o1), outside the frame: an end callback may start one of its own
        tweens_->update();
        // a transition onto this screen composes the frame; any other frame ends it first
        if (!snapshot)
            composed = tr.player.frame(screen ? static_cast<const ableem::GuiScreen *>(screen) : nullptr);
    }
    TraceFrame traceFrame; // the hand-off trap (BUG-31): a clear or present outside the stack is marked
    if (ableem::ext_trace::active())
        ableem::ext_trace::note("stack frame depth=" + std::to_string(depth_) + " tweens=" +
                                std::to_string(tweens_->count()) + " clear=" + (clearColor ? "own" : "opaque black") +
                                (composed ? " transition" : ""));
    {
        // a frame nested in a picture being drawn into a target (a busy tick from a load in it) goes to the screen
        std::unique_ptr<TargetScope> toScreen;
        if (depth_ > 0 && tr.offscreen > 0 && tr.renderer)
            toScreen = std::make_unique<TargetScope>(*tr.renderer, nullptr, tr.offscreen);
        DepthScope scope(depth_);
        if (composed && tr.target(tr.newPicture)) {
            {
                // the screen's frame into the new picture, as it would have gone to the screen
                TargetScope into(*tr.renderer, &tr.newPicture, tr.offscreen);
                display_->setClearColor(clearColor ? *clearColor : OpaqueBlack);
                display_->clear();
                if (draw)
                    draw();
            }
            tr.composeOnScreen(tr.oldUsable(), true);
        } else {
            // never the colour a last drawing left set (BUG-31: a hint's white or a bar's fill cleared a frame white, and
            // that frame was presented before anything opaque covered it): a screen that wants another says so
            display_->setClearColor(clearColor ? *clearColor : OpaqueBlack);
            display_->clear();
            if (draw)
                draw();
        }
    }
    display_->present();
    presented_++;
    if (!snapshot)
        tr.shown++;
    if (composed)
        tr.player.presented(); // its first frame is up: the transition's time starts now
}

//*******************************
// ScreenStack - the transitions' API
//*******************************
void ScreenStack::declare(const ableem::GuiScreen &screen, const ScreenTransitions &transitions) {
    transitions_->declarations[&screen] = transitions;
}

void ScreenStack::forget(const ableem::GuiScreen &screen) {
    Transitions &tr = *transitions_;
    tr.declarations.erase(&screen);
    for (auto it = tr.open.begin(); it != tr.open.end();) {
        if (it->screen == &screen)
            it = tr.open.erase(it); // gone without closing (an exception through show()): never drawn again
        else
            ++it;
    }
    if (tr.player.target() == &screen)
        tr.player.finish();
}

ScreenTransitions ScreenStack::declared(const ableem::GuiScreen &screen) const {
    const auto it = transitions_->declarations.find(&screen);
    return it != transitions_->declarations.end() ? it->second : defaultScreenTransitions();
}

void ScreenStack::setAnimations(bool on) {
    transitions_->player.setEnabled(on);
    if (!on)
        transitions_->hasOld = false;
}

bool ScreenStack::animations() const {
    return transitions_->player.enabled();
}

void ScreenStack::setStartTransition(const Transition &t) {
    transitions_->start = t;
    transitions_->startSet = true;
}

bool ScreenStack::bringsIn(const ableem::GuiScreen &screen) const {
    const TransitionPlayer &player = transitions_->player;
    return player.armed() && player.target() == &screen && !player.backwards();
}

bool ScreenStack::transitioning() const {
    return transitions_->player.running();
}

void ScreenStack::finishTransition() {
    transitions_->player.finish();
}

void ScreenStack::attach(ableem::Input &input) {
    detach();
    transitions_->input = &input;
    input.setFrameProbe([this]() { return transitioning(); });
    input.setPressObserver([this]() { finishTransition(); });
    ableem::GuiScreen::setObserver(transitions_.get());
}

void ScreenStack::detach() {
    Transitions &tr = *transitions_;
    if (tr.input) {
        tr.input->setFrameProbe(nullptr);
        tr.input->setPressObserver(nullptr);
        tr.input = nullptr;
    }
    if (ableem::GuiScreen::observer() == &tr)
        ableem::GuiScreen::setObserver(nullptr);
}

void ScreenStack::releaseTargets() {
    Transitions &tr = *transitions_;
    tr.player.finish();
    tr.oldPicture = ableem::Texture();
    tr.newPicture = ableem::Texture();
    tr.hasOld = false;
    tr.startSet = false;
}

} // namespace abgui
