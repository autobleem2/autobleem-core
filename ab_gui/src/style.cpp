// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Style: the shared look of the panels, menus and dialogs. See the header.
//
#include <ab_gui/style.h>

#include <ab_gui/context.h>
#include <ab_gui/footer_shorten.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <vector>

using namespace std;
using ableem::Color;
using ableem::Rect;

namespace abgui {

// C++14: a static constexpr member that is odr-used (bound to a const reference, std::max) needs its definition
constexpr int Style::DefaultHeaderHeight;
constexpr int Style::DefaultFooterHeight;
constexpr int Style::DefaultRowHeight;
constexpr int Style::DefaultRowInset;
constexpr int Style::DefaultMargin;
constexpr int Style::DefaultSelectionBar;
constexpr int Style::ChipHeight;
constexpr int Style::ChipMinWidth;
constexpr int Style::ChipPadding;
constexpr int Style::PictureGap;
constexpr int Style::FooterGap;
constexpr int Style::FooterStatusGap;
constexpr int Style::OwnAlpha;
constexpr int Style::StyleAlpha;
constexpr int InactiveAlphas::Unset;

namespace {
// the widest a footer's status can get on this list: in a counter "n/m" the n becomes m (the list's last row), then
// every digit an 8 - "Game 3/24" reserves "Game 88/88". The footer leaves the hints that much room whatever row is
// selected, so their size never jumps while the list scrolls and the counter is never covered (UIREV-3, the owner)
string widestStatus(const string &status) {
    string out = status;
    for (size_t slash = out.find('/'); slash != string::npos; slash = out.find('/', slash + 1)) {
        size_t start = slash;
        while (start > 0 && isdigit(static_cast<unsigned char>(out[start - 1])))
            start--;
        size_t end = slash + 1;
        while (end < out.size() && isdigit(static_cast<unsigned char>(out[end])))
            end++;
        if (start == slash || end == slash + 1)
            continue; // not a counter
        out.replace(start, slash - start, out.substr(slash + 1, end - slash - 1));
        slash = start + (end - slash - 1);
    }
    for (char &c : out)
        if (isdigit(static_cast<unsigned char>(c)))
            c = '8';
    return out;
}

string upper(const string &key) {
    string name = key;
    for (char &c : name)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return name;
}
} // namespace

//*******************************
// Style::outlineOf
//*******************************
ableem::Texture Style::outlineOf(ableem::Renderer &renderer, const ableem::Image &image) {
    if (!image.valid())
        return ableem::Texture();
    const ableem::Size s = image.size();
    const int w = s.w + 5, h = s.h + 5;
    vector<float> shape(static_cast<size_t>(w * h), 0.0f);
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++)
            shape[static_cast<size_t>((y + 2) * w + x + 2)] = image.pixel(x, y).a / 255.0f;

    static const int offsets[9][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}, {2, 2}};
    const float passAlpha = 150.0f / 255.0f;
    ableem::Texture tex = ableem::Texture::createStreaming(renderer, w, h);
    if (!tex.valid())
        return tex;
    tex.setBlendMode(ableem::BlendMode::Blend);
    {
        ableem::PixelLock px = tex.lock();
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float clear = 1.0f;
                for (const auto &o : offsets) {
                    const int sx = x - o[0], sy = y - o[1];
                    if (sx >= 0 && sy >= 0 && sx < w && sy < h)
                        clear *= 1.0f - passAlpha * shape[static_cast<size_t>(sy * w + sx)];
                }
                px.set(x, y, ableem::Color(0, 0, 0, static_cast<unsigned char>(std::lround((1.0f - clear) * 255))));
            }
    }
    return tex;
}

//*******************************
// Style::fromColors
//*******************************
Style Style::fromColors(const ColorRoles &roles) {
    Style s;
    if (roles.text.set)
        s.text = roles.text.color;
    if (roles.secondary.set)
        s.secondary = roles.secondary.color;
    s.hint = roles.hint.set ? roles.hint.color : s.secondary;

    // the roles: each one's colour, or the colour it names, or its fallback - resolved by name so a role may
    // name another role ("value": "row"); a chain longer than the roles themselves is a loop, cut to text
    using Field = RoleColor ColorRoles::*;
    struct Def {
        const char *key;
        Field field;
        const char *fallback;
        Color Style::*out;
    };
    static const Def defs[] = {
        {"row", &ColorRoles::row, "secondary", &Style::row},
        {"rowSelected", &ColorRoles::rowSelected, "text", &Style::rowSelected},
        {"heading", &ColorRoles::heading, "secondary", &Style::heading},
        {"value", &ColorRoles::value, "row", &Style::value},
        {"description", &ColorRoles::description, "secondary", &Style::description},
        {"footer", &ColorRoles::footer, "text", &Style::footerText},
        {"selectionBand", &ColorRoles::selectionBand, "text", &Style::selectionBand},
        {"edge", &ColorRoles::edge, "secondary", &Style::edge},
    };
    const int count = static_cast<int>(sizeof(defs) / sizeof(defs[0]));
    auto known = [&](const string &name) {
        if (name == "text" || name == "secondary" || name == "hint" || name == "selection")
            return true;
        for (const Def &d : defs)
            if (name == d.key)
                return true;
        return false;
    };
    std::function<Color(const string &, int)> byName = [&](const string &name, int depth) -> Color {
        if (depth > count)
            return s.text;
        if (name == "text")
            return s.text;
        if (name == "secondary")
            return s.secondary;
        if (name == "hint")
            return s.hint;
        if (name == "selection")
            return roles.selection.set ? roles.selection.color : s.text;
        for (const Def &d : defs) {
            if (name != d.key)
                continue;
            const RoleColor &role = roles.*(d.field);
            if (role.color.set)
                return role.color.color;
            if (!role.ref.empty() && role.ref != name && known(role.ref)) // a misspelt name counts as unset
                return byName(role.ref, depth + 1);
            return byName(d.fallback, depth + 1);
        }
        return s.text;
    };
    for (const Def &d : defs)
        s.*(d.out) = byName(d.key, 0);
    return s;
}

//*******************************
// Style::dim
//*******************************
void Style::dim(ableem::Renderer &renderer) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(Color(0, 0, 0, dimAlpha));
    renderer.fillRect();
}

void Style::dim(Context &ctx) const {
    dim(ctx.renderer());
}

//*******************************
// Style::sheet
//*******************************
void Style::sheet(ableem::Renderer &renderer, const Rect &panel) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(Color(0, 0, 0, sheetAlpha));
    renderer.fillRect(panel);
    renderer.setDrawColor(Color(edge.r, edge.g, edge.b, edgeAlpha));
    renderer.drawRect(panel);
}

void Style::sheet(Context &ctx, const Rect &panel) const {
    const PanelSheet under = ctx.panelSheet();
    if (!under.set) {
        if (!drawFrame(ctx, "panel", panel))
            sheet(ctx.renderer(), panel);
        return;
    }
    // the theme's sheet colour under the frame; a rim-only frame (G6c, `"fill": false`) leaves it showing
    ctx.renderer().setBlendMode(ableem::BlendMode::Blend);
    ctx.renderer().setDrawColor(under.drawn());
    ctx.renderer().fillRect(panel);
    if (!drawFrame(ctx, "panel", panel)) {
        ctx.renderer().setDrawColor(Color(edge.r, edge.g, edge.b, edgeAlpha));
        ctx.renderer().drawRect(panel);
    }
}

//*******************************
// Style::toast
//*******************************
void Style::toast(Context &ctx, const Rect &panel) const {
    if (!drawFrame(ctx, "toast", panel))
        sheet(ctx, panel); // the "panel" frame, else the code-drawn sheet - on the theme's sheet colour when it has one
}

//*******************************
// Style::colorByName / drawFrame
//*******************************
bool Style::colorByName(const string &name, Color &out) const {
    static const struct {
        const char *name;
        Color Style::*color;
    } colours[] = {
        {"text", &Style::text},
        {"secondary", &Style::secondary},
        {"hint", &Style::hint},
        {"row", &Style::row},
        {"rowSelected", &Style::rowSelected},
        {"heading", &Style::heading},
        {"value", &Style::value},
        {"description", &Style::description},
        {"footer", &Style::footerText},
        {"selectionBand", &Style::selectionBand},
        {"edge", &Style::edge},
    };
    for (const auto &c : colours)
        if (name == c.name) {
            out = this->*(c.color);
            return true;
        }
    return false;
}

bool Style::drawFrame(Context &ctx, const string &name, const Rect &box) const {
    return drawFrame(ctx, name, box, 255);
}

bool Style::drawFrame(Context &ctx, const string &name, const Rect &box, unsigned char alpha) const {
    return drawFrame(ctx, name, box, alpha, 1.0f);
}

bool Style::drawFrame(Context &ctx, const string &name, const Rect &box, unsigned char alpha, float scale) const {
    const Frame frame = ctx.frame(name);
    if (!frame.valid())
        return false;
    Color tint(255, 255, 255, 255);
    if (!frame.tint.empty()) {
        if (colorByName(frame.tint, tint))
            tint.a = 255; // the colour multiplies the image's; its alpha is the image's own
        else if (frame.tintResolved) {
            tint = frame.tintColor; // a colour the program resolved (`selection`, G5k)
            tint.a = 255;
        }
    }
    abgui::drawFrame(ctx.renderer(), scaledFrame(frame, scale), box, tint, alpha);
    return true;
}

bool Style::drawFirstFrame(Context &ctx, const vector<string> &names, const Rect &box) const {
    for (const string &name : names)
        if (drawFrame(ctx, name, box))
            return true;
    return false;
}

//*******************************
// Style::rule
//*******************************
void Style::rule(ableem::Renderer &renderer, const Rect &panel, int y) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(Color(edge.r, edge.g, edge.b, edgeAlpha));
    renderer.fillRect(Rect(panel.x + rowInset, y, panel.w - 2 * rowInset, 1));
}

void Style::rule(Context &ctx, const Rect &panel, int y) const {
    rule(ctx.renderer(), panel, y);
}

//*******************************
// Style::header
//*******************************
int Style::header(Context &ctx, const Rect &panel, const string &title) const {
    ctx.drawText(ctx.font(FontRole::Title), title, panel.x + rowInset, panel.y + titleTop, text);
    rule(ctx, panel, panel.y + headerHeight - 8);
    return panel.y + headerHeight;
}

//*******************************
// Style::selection
//*******************************
void Style::selection(ableem::Renderer &renderer, const Rect &rect) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(Color(selectionBand.r, selectionBand.g, selectionBand.b, bandAlpha));
    renderer.fillRect(rect);
    renderer.setDrawColor(selectionBand);
    renderer.fillRect(Rect(rect.x, rect.y, selectionBar, rect.h));
}

void Style::selection(Context &ctx, const Rect &rect) const {
    if (drawFrame(ctx, "selection", rect))
        return;
    selection(ctx.renderer(), rect);
}

bool Style::selectionFramed(Context &ctx) const {
    return ctx.frame("selection").valid();
}

//*******************************
// Style::label
//*******************************
void Style::label(ableem::Renderer &renderer, const Rect &rect) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(Color(edge.r, edge.g, edge.b, labelAlpha));
    renderer.fillRect(rect);
}

void Style::label(Context &ctx, const Rect &rect) const {
    if (drawFrame(ctx, "heading", rect))
        return;
    label(ctx.renderer(), rect);
}

//*******************************
// Style::disabled
//*******************************
void Style::disabled(ableem::Renderer &renderer, const Rect &rect) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(Color(0, 0, 0, disabledAlpha));
    renderer.fillRect(rect);
}

void Style::disabled(Context &ctx, const Rect &rect) const {
    const DisabledVeil veil = ctx.disabledVeil();
    if (!veil.set) {
        disabled(ctx.renderer(), rect);
        return;
    }
    ctx.renderer().setBlendMode(ableem::BlendMode::Blend);
    ctx.renderer().setDrawColor(veil.drawn());
    ctx.renderer().fillRect(rect);
}

const ableem::Color &Style::disabledColor(Context &ctx, const ableem::Color &normal) const {
    return ctx.disabledVeil().set ? description : normal;
}

//*******************************
// Style::scrollMarker
//*******************************
void Style::scrollMarker(ableem::Renderer &renderer, int cx, int cy, int direction) const {
    // the point at cy, the rows widening away from it: an up marker grows downwards (it drew upside down
    // until 2026-09-24 - the point sat at the far end)
    renderer.setDrawColor(text);
    for (int i = 0; i < 5; i++)
        renderer.fillRect(Rect(cx - i, cy - direction * i, 2 * i + 1, 1));
}

void Style::scrollMarker(Context &ctx, int cx, int cy, int direction) const {
    scrollMarker(ctx.renderer(), cx, cy, direction);
}

//*******************************
// Style::tone
//*******************************
Color Style::tone(Tone role, int alpha) const {
    Color c(0, 0, 0, 0);
    switch (role) {
    case Tone::None:
        return c;
    case Tone::Black:
        c = Color(0, 0, 0, 255);
        break;
    case Tone::White:
        c = Color(255, 255, 255, 255);
        break;
    case Tone::Text:
        c = text;
        break;
    case Tone::Secondary:
        c = secondary;
        break;
    case Tone::Edge:
        c = edge;
        break;
    case Tone::SelectionBand:
        c = selectionBand;
        break;
    }
    if (alpha >= 0)
        c.a = static_cast<unsigned char>(min(alpha, 255));
    return c;
}

//*******************************
// Style::box
//*******************************
void Style::box(ableem::Renderer &renderer, const Rect &rect, Tone fill, int fillAlpha, Tone edgeTone,
                int edgeAlpha) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    if (fill != Tone::None) {
        renderer.setDrawColor(tone(fill, fillAlpha));
        renderer.fillRect(rect);
    }
    if (edgeTone != Tone::None) {
        renderer.setDrawColor(tone(edgeTone, edgeAlpha));
        renderer.drawRect(rect);
    }
}

void Style::box(Context &ctx, const Rect &rect, Tone fill, int fillAlpha, Tone edgeTone, int edgeAlpha) const {
    box(ctx.renderer(), rect, fill, fillAlpha, edgeTone, edgeAlpha);
}

//*******************************
// Style::plate
//*******************************
void Style::plate(ableem::Renderer &renderer, const Rect &rect, const Color &color) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(color);
    renderer.fillRect(rect);
}

void Style::plate(Context &ctx, const Rect &rect, const Color &color) const {
    plate(ctx.renderer(), rect, color);
}

//*******************************
// Style::key / field / caret
//*******************************
void Style::key(ableem::Renderer &renderer, const Rect &rect, KeyState state, bool function) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    if (state == KeyState::Selected) {
        renderer.setDrawColor(Color(text.r, text.g, text.b, keySelectedAlpha));
        renderer.fillRect(rect);
        renderer.setDrawColor(text);
        renderer.drawRect(rect);
        return;
    }
    // the function keys a shade darker than the letters, as on a phone's keyboard
    const unsigned char fill = state == KeyState::Lit ? keyLitAlpha : function ? keyFunctionAlpha : keyAlpha;
    renderer.setDrawColor(Color(255, 255, 255, fill));
    renderer.fillRect(rect);
    renderer.setDrawColor(Color(secondary.r, secondary.g, secondary.b, keyEdgeAlpha));
    renderer.drawRect(rect);
}

void Style::key(Context &ctx, const Rect &rect, KeyState state, bool function) const {
    // the frame of the key's state; a state with none falls back to `key` (a selected key gets today's outline
    // over it), and no frame at all runs the old drawing unchanged
    const char *name = state == KeyState::Selected ? "keySelected"
                       : state == KeyState::Lit    ? "keyLit"
                       : function                  ? "keyFunction"
                                                   : "key";
    if (drawFrame(ctx, name, rect))
        return;
    if (state != KeyState::Normal || function) {
        if (drawFrame(ctx, "key", rect)) {
            if (state == KeyState::Selected) {
                ableem::Renderer &renderer = ctx.renderer();
                renderer.setBlendMode(ableem::BlendMode::Blend);
                renderer.setDrawColor(text);
                renderer.drawRect(rect);
            }
            return;
        }
    }
    key(ctx.renderer(), rect, state, function);
}

void Style::field(ableem::Renderer &renderer, const Rect &rect) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(Color(255, 255, 255, fieldAlpha));
    renderer.fillRect(rect);
    renderer.setDrawColor(Color(secondary.r, secondary.g, secondary.b, fieldEdgeAlpha));
    renderer.drawRect(rect);
}

void Style::field(Context &ctx, const Rect &rect) const {
    if (!drawFrame(ctx, "field", rect))
        field(ctx.renderer(), rect);
}

void Style::caret(ableem::Renderer &renderer, int x, int y, int height) const {
    renderer.setDrawColor(text);
    renderer.fillRect(Rect(x, y, caretWidth, height));
}

void Style::caret(Context &ctx, int x, int y, int height) const {
    caret(ctx.renderer(), x, y, height);
}

//*******************************
// Style::progress
//*******************************
void Style::progress(ableem::Renderer &renderer, const Rect &track, unsigned long long done, unsigned long long total,
                     Tone trackTone, int trackAlpha, Tone fillTone, int fillAlpha) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(tone(trackTone, trackAlpha == StyleAlpha ? progressTrackAlpha : trackAlpha));
    renderer.fillRect(track);
    if (total == 0)
        return;
    const int width = progressFillWidth(track.w, done, total);
    renderer.setDrawColor(tone(fillTone, fillAlpha));
    renderer.fillRect(Rect(track.x, track.y, width, track.h));
}

int Style::progressFillWidth(int trackWidth, unsigned long long done, unsigned long long total) {
    if (total == 0)
        return 0;
    const unsigned long long shown = done < total ? done : total;
    return static_cast<int>(static_cast<unsigned long long>(trackWidth) * shown / total);
}

void Style::progress(Context &ctx, const Rect &track, unsigned long long done, unsigned long long total, Tone trackTone,
                     int trackAlpha, Tone fillTone, int fillAlpha) const {
    // the style's own track alpha is the theme's `barTrack` inactive alpha when it sets one (G5r9)
    if (trackAlpha == StyleAlpha)
        trackAlpha = InactiveAlphas::orToday(ctx.inactiveAlphas().barTrack, progressTrackAlpha);
    const bool trackFramed = ctx.frame("progressTrack").valid();
    const bool fillFramed = ctx.frame("progressFill").valid();
    if (!trackFramed && !fillFramed) {
        progress(ctx.renderer(), track, done, total, trackTone, trackAlpha, fillTone, fillAlpha);
        return;
    }
    ableem::Renderer &renderer = ctx.renderer();
    if (trackFramed) {
        drawFrame(ctx, "progressTrack", track);
    } else {
        renderer.setBlendMode(ableem::BlendMode::Blend);
        renderer.setDrawColor(tone(trackTone, trackAlpha));
        renderer.fillRect(track);
    }
    if (total == 0)
        return;
    const int width = progressFillWidth(track.w, done, total);
    if (fillFramed) {
        if (width > 0)
            drawFrame(ctx, "progressFill", Rect(track.x, track.y, width, track.h));
    } else {
        renderer.setBlendMode(ableem::BlendMode::Blend);
        renderer.setDrawColor(tone(fillTone, fillAlpha));
        renderer.fillRect(Rect(track.x, track.y, width, track.h));
    }
}

//*******************************
// Style::progressBox
//*******************************
Rect Style::progressBoxFillRect(const Rect &bar, double fraction, bool framed) {
    if (framed) {
        const double f = fraction < 0 ? 0 : (fraction > 1 ? 1 : fraction);
        return Rect(bar.x, bar.y, static_cast<int>(bar.w * f), bar.h);
    }
    return Rect(bar.x + ProgressBoxInset, bar.y + ProgressBoxInset,
                static_cast<int>((bar.w - 2 * ProgressBoxInset) * fraction), bar.h - 2 * ProgressBoxInset);
}

void Style::progressBox(ableem::Renderer &renderer, const Rect &bar, double fraction) const {
    renderer.setDrawColor(Color(edge.r, edge.g, edge.b, ProgressBoxEdgeAlpha));
    renderer.drawRect(bar);
    renderer.setDrawColor(text);
    renderer.fillRect(progressBoxFillRect(bar, fraction, false));
}

void Style::progressBox(Context &ctx, const Rect &bar, double fraction) const {
    const bool trackFramed = ctx.frame("progressTrack").valid();
    const bool fillFramed = ctx.frame("progressFill").valid();
    if (!trackFramed && !fillFramed) {
        progressBox(ctx.renderer(), bar, fraction);
        return;
    }
    ableem::Renderer &renderer = ctx.renderer();
    if (trackFramed) {
        drawFrame(ctx, "progressTrack", bar);
    } else {
        renderer.setDrawColor(Color(edge.r, edge.g, edge.b, ProgressBoxEdgeAlpha));
        renderer.drawRect(bar);
    }
    if (fillFramed) {
        const Rect fill = progressBoxFillRect(bar, fraction, true);
        if (fill.w > 0)
            drawFrame(ctx, "progressFill", fill);
    } else {
        renderer.setDrawColor(text);
        renderer.fillRect(progressBoxFillRect(bar, fraction, false));
    }
}

//*******************************
// Style::spinner
//*******************************
void Style::spinner(ableem::Renderer &renderer, int cx, int cy, int radius, int dotSize, int lead) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    for (int i = 0; i < spinnerDots; i++) {
        const int behind = (lead - i + spinnerDots) % spinnerDots; // 0 for the leading dot
        const int alpha = 255 - behind * spinnerFade;
        const double a = i * 3.14159265 / 6.0;
        const int x = cx + static_cast<int>(radius * cos(a)) - dotSize / 2;
        const int y = cy + static_cast<int>(radius * sin(a)) - dotSize / 2;
        renderer.setDrawColor(Color(text.r, text.g, text.b, static_cast<unsigned char>(alpha)));
        renderer.fillRect(Rect(x, y, dotSize, dotSize));
    }
}

void Style::spinner(Context &ctx, int cx, int cy, int radius, int dotSize, int lead) const {
    if (spinnerStrip(ctx, cx, cy, ctx.ticks()))
        return;
    spinner(ctx.renderer(), cx, cy, radius, dotSize, lead);
}

bool Style::spinnerStrip(Context &ctx, int cx, int cy, unsigned long long elapsedMs) const {
    const SpinnerAnim anim = ctx.spinnerAnim();
    if (!anim.valid())
        return false;
    const int index = spinnerFrameIndex(elapsedMs, anim.fps, anim.frames);
    const Rect src = spinnerFrameRect(anim.strip.size(), anim.frames, index);
    ableem::Size frame;
    frame.w = src.w;
    frame.h = src.h;
    const Rect dst = spinnerDestRect(frame, cx, cy);
    ctx.renderer().setBlendMode(ableem::BlendMode::Blend);
    ctx.renderer().copy(anim.strip, &src, &dst);
    return true;
}

void Style::spinner(ableem::Renderer &renderer, const Rect &box, int lead) const {
    const int size = min(box.w, box.h);
    const int dotSize = min(spinnerDot, max(3, size / 10)); // never bigger than the metrics' own
    const int radius = min(spinnerRadius, max(6, size / 2 - dotSize - 2));
    spinner(renderer, box.x + box.w / 2, box.y + box.h / 2, radius, dotSize, lead);
}

void Style::spinner(Context &ctx, const Rect &box, int lead) const {
    if (spinnerStrip(ctx, box.x + box.w / 2, box.y + box.h / 2, ctx.ticks()))
        return;
    spinner(ctx.renderer(), box, lead);
}

//*******************************
// Style::tab / vrule
//*******************************
void Style::tab(ableem::Renderer &renderer, int x, int y, int w) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(selectionBand);
    renderer.fillRect(Rect(x, y, w, tabHeight));
}

void Style::tab(Context &ctx, int x, int y, int w) const {
    tab(ctx.renderer(), x, y, w);
}

//*******************************
// Style::tabCell
//*******************************
void Style::tabCell(ableem::Renderer &renderer, const Rect &cell) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(Color(selectionBand.r, selectionBand.g, selectionBand.b, bandAlpha));
    renderer.fillRect(cell);
    renderer.setDrawColor(selectionBand);
    renderer.fillRect(Rect(cell.x, cell.y + cell.h - selectionBar, cell.w, selectionBar));
}

void Style::tabCell(Context &ctx, const Rect &cell) const {
    if (drawFrame(ctx, "tab", cell))
        return;
    tabCell(ctx.renderer(), cell);
}

void Style::vrule(ableem::Renderer &renderer, int x, int y, int h, int alpha) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(tone(Tone::Edge, alpha == StyleAlpha ? edgeAlpha : alpha));
    renderer.fillRect(Rect(x, y, 1, h));
}

void Style::vrule(Context &ctx, int x, int y, int h, int alpha) const {
    vrule(ctx.renderer(), x, y, h, alpha);
}

//*******************************
// dPadParts
//*******************************
// the owner's rule ("a button combination is one button" does not cover the d-pad): "Left+Right" or "Up+Down" are
// separate arrows, each its own chip. Returns the parts of such a key, or {} when it is any other key.
static vector<string> dPadParts(const string &key) {
    vector<string> parts;
    size_t from = 0;
    while (true) {
        const size_t plus = key.find_first_of("+/", from);
        const string part = key.substr(from, plus == string::npos ? string::npos : plus - from);
        if (part != "Left" && part != "Right" && part != "Up" && part != "Down")
            return {};
        parts.push_back(part);
        if (plus == string::npos)
            break;
        from = plus + 1;
    }
    return parts.size() > 1 ? parts : vector<string>();
}

//*******************************
// Style::parseHints
//*******************************
vector<HintItem> Style::parseHints(const string &line, string &status) {
    auto trim = [](string s) {
        // spaces and the "|" separators the old lines carried
        const string junk = " |\t";
        size_t a = s.find_first_not_of(junk);
        size_t b = s.find_last_not_of(junk);
        return a == string::npos ? string() : s.substr(a, b - a + 1);
    };
    vector<HintItem> items;
    vector<string> pendingIcons; // icons whose text was only a separator: they belong to the next hint
    size_t pos = line.find("|@");
    status = trim(line.substr(0, pos == string::npos ? line.size() : pos));
    while (pos != string::npos) {
        size_t end = line.find('|', pos + 2);
        if (end == string::npos)
            break;
        string icon = line.substr(pos + 2, end - pos - 2);
        size_t next = line.find("|@", end + 1);
        string text = line.substr(end + 1, next == string::npos ? string::npos : next - end - 1);
        string label = trim(text);
        // d-pad directions are never a combination: "Left+Right" is the two arrows, each its own chip
        const vector<string> arrows = dPadParts(icon);
        if (arrows.empty())
            pendingIcons.push_back(icon);
        else
            pendingIcons.insert(pendingIcons.end(), arrows.begin(), arrows.end());
        if (label.empty() || label == "/") {
            pos = next;
            continue;
        }
        // the two shoulder keys of an L1/R1 or L2/R2 pair are one chip, however the line wrote them - and an
        // ALTERNATIVE ("L2/R2": either button pages), never the combination "L2+R2" (both held together); a marker
        // that is itself "L2+R2" never gets here as two icons and stays the combination
        // (either order: "R1/L1" keeps the order the line wrote)
        if (pendingIcons.size() == 2 &&
            ((pendingIcons[0] == "L1" && pendingIcons[1] == "R1") ||
             (pendingIcons[0] == "R1" && pendingIcons[1] == "L1") ||
             (pendingIcons[0] == "L2" && pendingIcons[1] == "R2") ||
             (pendingIcons[0] == "R2" && pendingIcons[1] == "L2")))
            pendingIcons = {pendingIcons[0] + "/" + pendingIcons[1]};
        items.push_back({pendingIcons, label});
        pendingIcons.clear();
        pos = next;
    }
    if (!pendingIcons.empty())
        items.push_back({pendingIcons, ""});
    return items;
}

//*******************************
// Style::button / buttons
//*******************************
// the parts of a combined key: "Left+Right" -> {"Left", "Right"}; one part for a plain key
static vector<string> keyParts(const string &key, char separator = '+') {
    vector<string> parts;
    size_t from = 0;
    while (true) {
        const size_t plus = key.find(separator, from);
        if (plus == string::npos || plus == 0 || plus + 1 >= key.size()) {
            parts.push_back(key.substr(from));
            break;
        }
        parts.push_back(key.substr(from, plus - from));
        from = plus + 1;
    }
    return parts;
}

// a combination of keys that all have a picture (Left+Right): the pictures side by side in one chip
struct Style::PictureChip {
    vector<string> parts;
    vector<ableem::Texture> icons;
    int iconsW = 0; // the pictures and the gaps between them
    int chipW = 0;
    int chipH = 0;
    int slashW = 0; // an alternative ("L2/R2"): the width of the small "/" between two pictures, else 0
};

bool Style::pictureChip(Context &ctx, const string &key, int height, PictureChip &out) const {
    // "A+B" is a combination, "A/B" an alternative (either button): both are one chip, the alternative with a small
    // "/" between its pictures
    const bool alternative = key.find('+') == string::npos && key.find('/') != string::npos;
    out.parts = keyParts(key, alternative ? '/' : '+');
    out.icons.clear();
    out.slashW = 0;
    if (out.parts.size() < 2)
        return false;
    out.iconsW = 0;
    int tallest = 0;
    for (const string &part : out.parts) {
        ableem::Texture icon = ctx.glyph(part);
        if (!icon.valid())
            return false;
        out.icons.push_back(icon);
        out.iconsW += icon.size().w;
        tallest = max(tallest, icon.size().h);
    }
    out.iconsW += PictureGap * static_cast<int>(out.parts.size() - 1);
    if (alternative) {
        out.slashW = ctx.textWidth(ctx.font(FontRole::Small), "/");
        out.iconsW += (out.slashW + PictureGap) * static_cast<int>(out.parts.size() - 1); // the "/" and its gap
    }
    out.chipW = out.iconsW + 2 * ChipPadding;
    out.chipH = min(height, max(ChipHeight, tallest + 4));
    return true;
}

// a text chip's size: the common ChipHeight, at least ChipMinWidth wide
static int chipWidthFor(int textW) {
    const int pad = Style::ChipPadding;
    const int minW = Style::ChipMinWidth;
    return max(minW, textW + 2 * pad);
}

int Style::button(Context &ctx, const string &key, int x, int y, int height) const {
    return drawButton(ctx, key, x, y, height, 255);
}

// a picture drawn at `alpha` (the handle is shared with every other drawing of it: the alpha goes back to 255)
static void copyFaded(ableem::Renderer &renderer, ableem::Texture texture, const Rect &dst, int alpha) {
    texture.setAlphaMod(static_cast<unsigned char>(alpha));
    renderer.copy(texture, nullptr, &dst);
    texture.setAlphaMod(255);
}

int Style::drawButton(Context &ctx, const string &key, int x, int y, int height, int alpha) const {
    const vector<string> arrows = dPadParts(key);
    if (!arrows.empty()) { // "Left+Right": two arrows side by side, each a button of its own
        const int startX = x;
        for (const string &arrow : arrows)
            x += drawButton(ctx, arrow, x, y, height, alpha) + 6;
        return x - 6 - startX;
    }
    ableem::Texture icon = ctx.glyph(key);
    ableem::Renderer &renderer = ctx.renderer();
    // every button - picture or chip - is centred on the line y + height / 2 (an odd remainder rounds the same way)
    const int cy = y + height / 2;
    if (icon.valid()) {
        const ableem::Size s = icon.size();
        Rect dst(x, cy - s.h / 2, s.w, s.h);
        ableem::Texture outline = ctx.glyphOutline(key);
        if (outline.valid()) {
            Rect outlineDst(dst.x - 2, dst.y - 2, s.w + 5, s.h + 5);
            copyFaded(renderer, outline, outlineDst, alpha);
        }
        copyFaded(renderer, icon, dst, alpha);
        return s.w;
    }
    const ableem::Font &font = ctx.font(FontRole::Small);
    PictureChip pic;
    int chipW;
    int chipH;
    string name;
    const bool pictures = pictureChip(ctx, key, height, pic);
    if (pictures) {
        chipW = pic.chipW;
        chipH = pic.chipH;
    } else {
        // a chip: the name in capitals, a box around it
        name = upper(key);
        chipW = chipWidthFor(ctx.textWidth(font, name));
        chipH = min(ChipHeight, height);
    }
    Rect chip(x, cy - chipH / 2, chipW, chipH);
    // the `chip` frame is the plate under the name (G5d); a theme with none keeps the box drawn in code
    if (!drawFrame(ctx, "chip", chip, static_cast<unsigned char>(alpha))) {
        renderer.setBlendMode(ableem::BlendMode::Blend);
        renderer.setDrawColor(Color(255, 255, 255, 24 * alpha / 255));
        renderer.fillRect(chip);
        renderer.setDrawColor(Color(edge.r, edge.g, edge.b, 200 * alpha / 255));
        renderer.drawRect(chip);
    }
    if (pictures) {
        int ix = chip.x + ChipPadding;
        for (size_t i = 0; i < pic.icons.size(); i++) {
            const ableem::Size s = pic.icons[i].size();
            Rect dst(ix, cy - s.h / 2, s.w, s.h);
            ableem::Texture outline = ctx.glyphOutline(pic.parts[i]);
            if (outline.valid()) {
                Rect outlineDst(dst.x - 2, dst.y - 2, s.w + 5, s.h + 5);
                copyFaded(renderer, outline, outlineDst, alpha);
            }
            copyFaded(renderer, pic.icons[i], dst, alpha);
            ix += s.w + PictureGap;
            if (pic.slashW > 0 && i + 1 < pic.icons.size()) { // an alternative: a small "/" between the pictures
                ctx.drawText(font, "/", ix, chip.y + (chipH - font.lineHeight()) / 2, secondary);
                ix += pic.slashW + PictureGap;
            }
        }
        return chipW;
    }
    const int tw = ctx.textWidth(font, name);
    ctx.drawText(font, name, chip.x + (chipW - tw) / 2, chip.y + (chipH - font.lineHeight()) / 2, text);
    return chipW;
}

int Style::buttonWidth(Context &ctx, const string &key, int height) const {
    const vector<string> arrows = dPadParts(key);
    if (!arrows.empty()) {
        int w = -6;
        for (const string &arrow : arrows)
            w += buttonWidth(ctx, arrow, height) + 6;
        return w;
    }
    ableem::Texture icon = ctx.glyph(key);
    if (icon.valid())
        return icon.size().w;
    PictureChip pic;
    if (pictureChip(ctx, key, height, pic))
        return pic.chipW;
    return chipWidthFor(ctx.textWidth(ctx.font(FontRole::Small), upper(key)));
}

int Style::buttons(Context &ctx, const string &markers, int x, int y, int height) const {
    return layoutButtons(ctx, markers, x, y, height, true, 255);
}

int Style::buttonsFaded(Context &ctx, const string &markers, int x, int y, int alpha, int height) const {
    return layoutButtons(ctx, markers, x, y, height, true, max(0, min(255, alpha)));
}

int Style::buttonsWidth(Context &ctx, const string &markers, int height) const {
    return layoutButtons(ctx, markers, 0, 0, height, false, 255);
}

int Style::layoutButtons(Context &ctx, const string &markers, int x, int y, int height, bool draw, int alpha) const {
    const ableem::Font &font = ctx.font(FontRole::RowSmall);
    const int startX = x;
    size_t pos = 0;
    while (pos < markers.size()) {
        size_t open = markers.find("|@", pos);
        // the text before the marker (a "/", a "+", or a plain word like RESET)
        string plain = markers.substr(pos, open == string::npos ? string::npos : open - pos);
        size_t a = plain.find_first_not_of(' ');
        if (a != string::npos) {
            plain = plain.substr(a, plain.find_last_not_of(' ') - a + 1);
            if (plain == "/" || plain == "+") {
                if (draw)
                    ctx.drawText(font, plain, x + 6, y + (height - font.lineHeight()) / 2, secondary);
                x += ctx.textWidth(font, plain) + 12;
            } else {
                x += (draw ? drawButton(ctx, plain, x, y, height, alpha) : buttonWidth(ctx, plain, height)) + 6;
            }
        }
        if (open == string::npos)
            break;
        size_t close = markers.find('|', open + 2);
        if (close == string::npos)
            break;
        const string key = markers.substr(open + 2, close - open - 2);
        x += (draw ? drawButton(ctx, key, x, y, height, alpha) : buttonWidth(ctx, key, height)) + 6;
        pos = close + 1;
    }
    return x - startX;
}

//*******************************
// Style::footer
//*******************************
int Style::hintRank(const string &icon) {
    const size_t plus = icon.find_first_of("+/"); // a combination or an alternative ranks as its first key
    if (plus != string::npos && plus > 0)
        return hintRank(icon.substr(0, plus));
    // the d-pad (G5r3) comes right after the face buttons, before Start/Select and the shoulders
    static const char *order[] = {"X",     "O",  "T",  "S",  "Left", "Right", "Up",    "Down", "Start",
                                  "Select", "L1", "R1", "L2", "R2",   "Enter", "Esc", "Tab"};
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++)
        if (icon == order[i])
            return static_cast<int>(i);
    return 100;
}

vector<HintItem> Style::sortedHints(vector<HintItem> hints) {
    stable_sort(hints.begin(), hints.end(), [](const HintItem &a, const HintItem &b) {
        return hintRank(a.icons.empty() ? "" : a.icons[0]) < hintRank(b.icons.empty() ? "" : b.icons[0]);
    });
    return hints;
}

int Style::hintIconsWidth(Context &ctx, const HintItem &hint, int height) const {
    int w = 0;
    for (const string &icon : hint.icons)
        w += buttonWidth(ctx, icon, height) + 6;
    return w;
}

void Style::footer(Context &ctx, const Rect &footer, const vector<HintItem> &given, const string &status,
                   bool withRule) const {
    if (withRule && !drawFrame(ctx, "footer", footer)) // the theme's band, else the rule along its top
        rule(ctx, footer, footer.y);
    vector<HintItem> hints = sortedHints(given);
    const int iconH = buttonHeight;
    const int y = footer.y + footerTop;
    const int IconOnlyGap = 16; // between hints once labels are dropped (C1: no fallback after the Small font)
    // the status at the right edge, in the description colour; the hints get what is left
    int right = footer.x + footer.w - rowInset;
    const ableem::Font &statusFont = ctx.font(FontRole::Row);
    if (!status.empty()) {
        const int w = ctx.textWidth(statusFont, status);
        ctx.drawText(statusFont, status, right - w, y, description);
        right -= max(w, ctx.textWidth(statusFont, widestStatus(status))) + FooterStatusGap;
    }
    // the largest font the hints fit in, then the gap between them; `iconsOnly` drops every hint's own label
    // (not the button chip's own text, e.g. "L2" - only h.label) once even the smallest font does not fit,
    // so a hint is never drawn past `room` - see the fallback below
    const int room = right - (footer.x + rowInset);
    auto widthAt = [&](const ableem::Font &font, int gap, bool iconsOnly) {
        int w = 0;
        for (const HintItem &h : hints) {
            w += hintIconsWidth(ctx, h, iconH); // each key's real width: its glyph when the theme has one, else the chip
            w += iconsOnly ? gap : 2 + ctx.textWidth(font, h.label) + gap;
        }
        return w - gap;
    };
    ableem::Font font = ctx.font(FontRole::Row);
    int gap = FooterGap;
    bool iconsOnly = false;
    if (widthAt(font, gap, false) > room) {
        gap = 22;
        if (widthAt(font, gap, false) > room) {
            font = ctx.font(FontRole::RowSmall);
            // a 4:3 canvas (800x600, 640x480): no smaller font - the last hints are left out instead (below), the
            // labels that stay keep a size that can be read on a small screen. 16:9 is as before
            const bool narrow = ctx.renderer().width() * 3 <= ctx.renderer().height() * 4;
            size_t keep = 0;
            if (narrow && widthAt(font, gap, false) > room)
                keep = footerHintsThatFit(hints.size(), room, [&](size_t n) {
                    vector<HintItem> head(hints.begin(), hints.begin() + static_cast<ptrdiff_t>(n));
                    hints.swap(head);
                    const int w = widthAt(font, gap, false);
                    hints.swap(head);
                    return w;
                });
            if (keep > 0)
                hints.resize(keep);
            else if (widthAt(font, gap, false) > room) {
                font = ctx.font(FontRole::Small);
                if (widthAt(font, gap, false) > room) {
                    // even the smallest font does not fit on the one row the window could give it: cut the labels
                    // ("Back" -> "B..", the longest first) and only when even the shortest do not fit, icons only -
                    // they are fixed width, so that always fits unless there are too many hints for even bare
                    // icons, which is outside this fallback's job (UIREV-3) and is left to clip as before. Never
                    // two rows: the compact panel is made wide enough first (Panel::compactWidth)
                    vector<string> labels;
                    for (const HintItem &h : hints)
                        labels.push_back(h.label);
                    auto measureLabels = [&](const vector<string> &l) {
                        vector<HintItem> cut = hints;
                        for (size_t i = 0; i < cut.size(); i++)
                            cut[i].label = l[i];
                        hints.swap(cut);
                        const int w = widthAt(font, gap, false);
                        hints.swap(cut);
                        return w;
                    };
                    if (shortenFooterLabels(labels, room, measureLabels)) {
                        for (size_t i = 0; i < hints.size(); i++)
                            hints[i].label = labels[i];
                    } else {
                        iconsOnly = true;
                        gap = IconOnlyGap;
                    }
                }
            }
        }
    }
    const int fontH = font.lineHeight();
    int x = footer.x + rowInset;
    for (const HintItem &h : hints) {
        for (const string &key : h.icons)
            x += button(ctx, key, x, y, iconH) + 6;
        if (iconsOnly) {
            x += gap;
        } else {
            x += 2;
            ctx.drawText(font, h.label, x, y + (iconH - fontH) / 2, footerText);
            x += ctx.textWidth(font, h.label) + gap;
        }
    }
}

int Style::footerWidth(Context &ctx, const vector<HintItem> &given, const string &status) const {
    const vector<HintItem> hints = sortedHints(given);
    const ableem::Font &font = ctx.font(FontRole::Row);
    int w = 0;
    for (const HintItem &h : hints)
        w += hintIconsWidth(ctx, h, buttonHeight) + 2 + ctx.textWidth(font, h.label) + FooterGap;
    w -= hints.empty() ? 0 : FooterGap;
    if (!status.empty())
        w += ctx.textWidth(ctx.font(FontRole::Row), status) + FooterStatusGap;
    return w + 2 * rowInset;
}

int Style::footerWidth(Context &ctx, const string &line) const {
    string status;
    const vector<HintItem> items = parseHints(line, status);
    return footerWidth(ctx, items, status);
}

void Style::footer(Context &ctx, const Rect &footerRect, const string &line, bool withRule) const {
    string status;
    vector<HintItem> items = parseHints(line, status);
    footer(ctx, footerRect, items, status, withRule);
}

} // namespace abgui
