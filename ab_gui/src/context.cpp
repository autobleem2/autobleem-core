// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Context: the calls the drawing makes, over the program's providers. See the header.
//
#include <ab_gui/context.h>

using namespace std;

namespace abgui {

//*******************************
// Context::font
//*******************************
const ableem::Font &Context::font(FontRole role) const {
    return fontProvider ? fontProvider(role) : none_;
}

//*******************************
// Context::glyph / glyphOutline
//*******************************
ableem::Texture Context::glyph(const string &key) const {
    return glyphProvider ? glyphProvider(key) : ableem::Texture();
}

ableem::Texture Context::glyphOutline(const string &key) const {
    return glyphOutlineProvider ? glyphOutlineProvider(key) : ableem::Texture();
}

//*******************************
// Context::drawText / textWidth
//*******************************
void Context::drawText(const ableem::Font &font, const string &text, int x, int y, const ableem::Color &color) const {
    if (textDrawer) {
        textDrawer(font, text, x, y, color);
        return;
    }
    if (font.valid())
        font.drawColor(*renderer_, x, y, color, text);
}

int Context::textWidth(const ableem::Font &font, const string &text) const {
    if (textMeasurer)
        return textMeasurer(font, text);
    return font.valid() ? font.width(text) : 0;
}

//*******************************
// Context::translate / style
//*******************************
string Context::translate(const string &text) const {
    return translator ? translator(text) : text;
}

Style Context::style() const {
    return styleProvider ? styleProvider() : Style();
}

//*******************************
// Context::play
//*******************************
void Context::play(UiSound sound) const {
    if (soundPlayer)
        soundPlayer(sound);
}

//*******************************
// Context::ticks / delay
//*******************************
unsigned int Context::ticks() const {
    if (clock)
        return clock();
    return platform_ ? platform_->ticks() : 0;
}

void Context::delay(unsigned int ms) const {
    if (platform_)
        platform_->delay(ms);
}

//*******************************
// Context::drawBackdrop / panelRect
//*******************************
void Context::drawBackdrop() const {
    if (backdropDrawer) {
        backdropDrawer();
        return;
    }
    renderer_->setDrawColor(ableem::Color(0, 0, 0, 255));
    renderer_->clear();
}

ableem::Rect Context::panelRect() const {
    if (panelProvider)
        return panelProvider();
    const int margin = style().margin;
    return ableem::Rect(margin, margin, renderer_->width() - 2 * margin, renderer_->height() - 2 * margin);
}

void Context::drawLine(const ableem::Font &font, const string &text, int x, int y, LineAlign align) const {
    if (lineDrawer) {
        lineDrawer(font, text, x, y, align);
        return;
    }
    if (align == LineAlign::Centre)
        x = (renderer_->width() - textWidth(font, text)) / 2;
    drawText(font, text, x, y, style().text);
}

bool Context::setTextShadow(bool on) const {
    return shadowSwitch ? shadowSwitch(on) : false;
}

ableem::Rect Context::drawLogo() const {
    return logoDrawer ? logoDrawer() : ableem::Rect();
}

//*******************************
// Context::setCompactPanel / clearCompactPanel / currentPanelRect
//*******************************
void Context::setCompactPanel(const ableem::Rect &rect) {
    compactRect_ = rect;
    compact_ = true;
    if (panelSwitch)
        panelSwitch(&compactRect_);
}

void Context::clearCompactPanel() {
    compact_ = false;
    if (panelSwitch)
        panelSwitch(nullptr);
}

ableem::Rect Context::currentPanelRect() const {
    return compact_ ? compactRect_ : panelRect();
}

//*******************************
// Context::frame
//*******************************
Frame Context::frame(const string &name) const {
    return frameProvider ? frameProvider(name) : Frame();
}

//*******************************
// Context::icon / iconHalo
//*******************************
ableem::Texture Context::icon(const string &name) const {
    return iconProvider ? iconProvider(name) : ableem::Texture();
}

ableem::Texture Context::iconHalo(const string &name) const {
    return iconHaloProvider ? iconHaloProvider(name) : ableem::Texture();
}

//*******************************
// Context::spinnerAnim
//*******************************
SpinnerAnim Context::spinnerAnim() const {
    if (!spinnerProvider)
        return SpinnerAnim();
    SpinnerAnim anim = spinnerProvider();
    return anim.valid() ? anim : SpinnerAnim();
}

} // namespace abgui
