// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Busy: the spinner over the backdrop while a long job runs, and the "please wait" picture. See the header.
//
#include <ab_gui/busy.h>
#include <ab_gui/context.h>
#include <ab_gui/screen_stack.h>

#include <ableem/ui/debug_driver.h>

#include <algorithm>

using namespace std;

namespace abgui {

// the constants' definitions (C++14: a static constexpr member bound to a reference needs one)
constexpr unsigned int Busy::FrameInterval;
constexpr unsigned int Busy::SpinnerStep;
constexpr int Busy::SpinnerDots;
constexpr int Busy::SpinnerRadius;
constexpr int Busy::SpinnerDot;
constexpr int Busy::SpinnerRise;
constexpr int Busy::MessageGap;
constexpr int Busy::BarWidth;
constexpr int Busy::BarHeight;
constexpr int Busy::BarGap;
constexpr int Busy::WaitSpinnerGap;
constexpr int Busy::WaitSpinnerFoot;
constexpr int Busy::WaitTopLineY;

Busy::Busy(ScreenStack &stack) : stack_(stack) {}

//*******************************
// Busy::begin / tick / setProgress / end
//*******************************
void Busy::begin(const string &message, const function<void()> &redraw) {
    if (!ctx_)
        return;
    message_ = message;
    done_ = total_ = 0;
    ableem::Renderer &renderer = ctx_->renderer();
    renderer.captureNextFrame();
    if (redraw)
        redraw(); // presents, and the capture is that frame
    backdrop_ = renderer.lastCapture();
    if (!active_)
        ableem::DebugDriver::setBusy(true); // the driver's `busy` / `wait_ready`: input is dropped meanwhile
    active_ = true;
    started_ = ctx_->ticks();
    lastFrame_ = 0;
    drawFrame();
}

void Busy::tick() {
    if (!active_ || !ctx_)
        return;
    if (!frameDue(ctx_->ticks(), lastFrame_))
        return;
    drawFrame();
}

void Busy::setProgress(int done, int total) {
    done_ = done;
    total_ = total;
    lastFrame_ = 0; // the next tick draws it
}

void Busy::end() {
    // CONSOLE-11: a busy frame reads no input while the job runs, so whatever the pads/keyboard queued meanwhile
    // piled up; a Cross pressed because the spinner looked stuck was left queued and handled as a real press the
    // moment the next poll() ran - on the console, Options' ~12 s reload started a game the player never meant to
    // start. Flush only on the busy -> not busy step (a screen calls end() every frame, including every idle one
    // where nothing is queued to lose); what the flush keeps and releases is Input's busy rule (CONSOLE-13).
    if (active_) {
        if (ctx_ && ctx_->hasInput())
            ctx_->input().flushInputEvents();
        ableem::DebugDriver::setBusy(false);
    }
    active_ = false;
    backdrop_ = ableem::Texture();
}

//*******************************
// Busy::drawFrame / drawSpinner
//*******************************
void Busy::drawFrame() {
    lastFrame_ = ctx_->ticks();
    // the pads' events pile up meanwhile; nothing reads them until the job is done
    stack_.frame(ableem::Color(0, 0, 0, 255), [this]() {
        ableem::Renderer &renderer = ctx_->renderer();
        if (backdrop_.valid())
            renderer.copy(backdrop_, nullptr, nullptr);
        const Style style = ctx_->style();
        style.dim(*ctx_);
        const ableem::Point centre = spinnerCentre(renderer.width(), renderer.height());
        drawSpinner(centre.x, centre.y, message_, ctx_->ticks() - started_);
        if (total_ > 0) {
            // the bar under the message, as the notification bubble draws its own
            const ableem::Rect track =
                barRect(renderer.width(), renderer.height(), ctx_->font(FontRole::Row).lineHeight());
            style.progress(renderer, track, static_cast<unsigned long long>(barDone(done_, total_)),
                           static_cast<unsigned long long>(total_));
        }
    });
}

void Busy::drawSpinner(int cx, int cy, const string &message, unsigned int elapsed) {
    const Style style = ctx_->style();
    // the theme's frame strip (G5p) when it has one, played from the job's start; else the ring of dots as always
    if (!style.spinnerStrip(*ctx_, cx, cy, elapsed))
        style.spinner(ctx_->renderer(), cx, cy, SpinnerRadius, SpinnerDot, spinnerLead(ctx_->ticks()));
    if (!message.empty()) {
        // centred on cx as the text renderer centres a line: half the canvas less half the width
        const ableem::Font &font = ctx_->font(FontRole::Row);
        ctx_->drawText(font, message, cx - ctx_->textWidth(font, message) / 2, messageTop(cy), style.text);
    }
}

//*******************************
// Busy::waitScreen
//*******************************
void Busy::waitScreen(const string &message, const string &topLine) {
    if (!ctx_)
        return;
    stack_.frame([&]() {
        ctx_->drawBackdrop();
        const ableem::Rect logo = ctx_->drawLogo();
        ableem::Renderer &renderer = ctx_->renderer();
        const int cx = renderer.width() / 2;
        // the spinner under the logo (the logo rect is the program's; below it, or the lower third of the screen)
        drawSpinner(cx, waitSpinnerY(renderer.height(), logo), message, ctx_->ticks()); // no job: the clock itself
        if (!topLine.empty()) {
            const ableem::Font &font = ctx_->font(FontRole::RowSmall);
            ctx_->drawText(font, topLine, cx - ctx_->textWidth(font, topLine) / 2, WaitTopLineY, ctx_->style().text);
        }
    });
}

//*******************************
// the pure rules
//*******************************
bool Busy::frameDue(unsigned int now, unsigned int lastFrame) {
    return lastFrame == 0 || now - lastFrame >= FrameInterval;
}

int Busy::spinnerLead(unsigned int ticks) {
    return static_cast<int>(ticks / SpinnerStep) % SpinnerDots;
}

ableem::Point Busy::spinnerCentre(int canvasWidth, int canvasHeight) {
    ableem::Point p;
    p.x = canvasWidth / 2;
    p.y = canvasHeight / 2 - SpinnerRise;
    return p;
}

int Busy::messageTop(int cy) {
    return cy + SpinnerRadius + MessageGap;
}

ableem::Rect Busy::barRect(int canvasWidth, int canvasHeight, int lineHeight) {
    const int messageY = messageTop(spinnerCentre(canvasWidth, canvasHeight).y);
    return ableem::Rect(canvasWidth / 2 - BarWidth / 2, messageY + lineHeight + BarGap, BarWidth, BarHeight);
}

int Busy::barDone(int done, int total) {
    return max(0, min(done, total));
}

int Busy::waitSpinnerY(int canvasHeight, const ableem::Rect &logo) {
    const int below = logo.y + logo.h;
    return min(canvasHeight - WaitSpinnerFoot, max(below + WaitSpinnerGap, canvasHeight * 2 / 3));
}

} // namespace abgui
