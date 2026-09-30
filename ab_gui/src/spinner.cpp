// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui spinner strip: the pure frame rules, loading the strip, and the SpinnerStrip. See the header.
//
#include <ab_gui/spinner.h>

#include <ab_gui/frame.h>

#include <ableem/engine/log.h>

using namespace std;

namespace abgui {

//*******************************
// SpinnerAnim / the pure rules
//*******************************
bool SpinnerAnim::valid() const {
    return strip.valid() && frames >= 1 && fps >= 1 && strip.size().w / frames >= 1;
}

int spinnerFrameIndex(unsigned long long elapsedMs, int fps, int frames) {
    if (frames < 1 || fps < 1)
        return 0;
    return static_cast<int>(elapsedMs * static_cast<unsigned long long>(fps) / 1000ULL %
                            static_cast<unsigned long long>(frames));
}

ableem::Rect spinnerFrameRect(const ableem::Size &stripSize, int frames, int index) {
    if (frames < 1)
        return ableem::Rect(0, 0, 0, 0);
    const int w = stripSize.w / frames;
    const int i = ((index % frames) + frames) % frames;
    return ableem::Rect(i * w, 0, w, stripSize.h);
}

ableem::Rect spinnerDestRect(const ableem::Size &frame, int cx, int cy) {
    return ableem::Rect(cx - frame.w / 2, cy - frame.h / 2, frame.w, frame.h);
}

//*******************************
// loadSpinnerStrip
//*******************************
ableem::Texture loadSpinnerStrip(ableem::Renderer &renderer, const SpinnerSpec &spec) {
    float scale = 1.0f;
    const string file = pickImageFile(spec.file, spec.file2x, renderer.outputScale(), scale);
    if (file.empty())
        return ableem::Texture();
    if (scale == 1.0f)
        return ableem::Texture::loadFile(renderer, file); // the 1x file: the very call a theme image always made
    return ableem::Texture::loadFile(renderer, file, scale);
}

//*******************************
// SpinnerStrip
//*******************************
void SpinnerStrip::assign(const SpinnerSpec &spec) {
    spec_ = spec;
    usable_ = (!spec.file.empty() || !spec.file2x.empty()) && spec.frames >= 1 && spec.fps >= 1;
    texture_ = ableem::Texture();
    tried_ = false;
}

void SpinnerStrip::release() {
    texture_ = ableem::Texture();
    tried_ = false;
}

SpinnerAnim SpinnerStrip::anim(ableem::Renderer &renderer) {
    SpinnerAnim a;
    if (!usable_)
        return a;
    if (!tried_) {
        tried_ = true; // one try per load: a bad file is logged once, not every frame
        texture_ = loadSpinnerStrip(renderer, spec_);
        if (!texture_.valid()) {
            PLOG_WARNING << "Spinner strip: " << (spec_.file.empty() ? spec_.file2x : spec_.file) << " does not load";
        }
    }
    a.strip = texture_;
    a.frames = spec_.frames;
    a.fps = spec_.fps;
    if (!a.valid()) {
        a.strip = ableem::Texture(); // a strip narrower than its frame count is drawn as the ring
        a.frames = a.fps = 0;
    }
    return a;
}

} // namespace abgui
