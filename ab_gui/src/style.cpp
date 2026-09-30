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
constexpr int Style::OwnAlpha;
constexpr int Style::StyleAlpha;

namespace {
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
    if (!drawFrame(ctx, "panel", panel))
        sheet(ctx.renderer(), panel);
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
    const Frame frame = ctx.frame(name);
    if (!frame.valid())
        return false;
    Color tint(255, 255, 255, 255);
    if (!frame.tint.empty() && colorByName(frame.tint, tint))
        tint.a = 255; // the colour multiplies the image's; its alpha is the image's own
    abgui::drawFrame(ctx.renderer(), frame, box, tint, alpha);
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
    rule(ctx.renderer(), panel, panel.y + headerHeight - 8);
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
    disabled(ctx.renderer(), rect);
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
void Style::progress(ableem::Renderer &renderer, const Rect &track, unsigned long long done,
                     unsigned long long total, Tone trackTone, int trackAlpha, Tone fillTone, int fillAlpha) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(tone(trackTone, trackAlpha == StyleAlpha ? progressTrackAlpha : trackAlpha));
    renderer.fillRect(track);
    if (total == 0)
        return;
    const unsigned long long shown = done < total ? done : total;
    const int width = static_cast<int>(static_cast<unsigned long long>(track.w) * shown / total);
    renderer.setDrawColor(tone(fillTone, fillAlpha));
    renderer.fillRect(Rect(track.x, track.y, width, track.h));
}

void Style::progress(Context &ctx, const Rect &track, unsigned long long done, unsigned long long total,
                     Tone trackTone, int trackAlpha, Tone fillTone, int fillAlpha) const {
    progress(ctx.renderer(), track, done, total, trackTone, trackAlpha, fillTone, fillAlpha);
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
    spinner(ctx.renderer(), cx, cy, radius, dotSize, lead);
}

void Style::spinner(ableem::Renderer &renderer, const Rect &box, int lead) const {
    const int size = min(box.w, box.h);
    const int dotSize = min(spinnerDot, max(3, size / 10)); // never bigger than the metrics' own
    const int radius = min(spinnerRadius, max(6, size / 2 - dotSize - 2));
    spinner(renderer, box.x + box.w / 2, box.y + box.h / 2, radius, dotSize, lead);
}

void Style::spinner(Context &ctx, const Rect &box, int lead) const {
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

void Style::vrule(ableem::Renderer &renderer, int x, int y, int h, int alpha) const {
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(tone(Tone::Edge, alpha == StyleAlpha ? edgeAlpha : alpha));
    renderer.fillRect(Rect(x, y, 1, h));
}

void Style::vrule(Context &ctx, int x, int y, int h, int alpha) const {
    vrule(ctx.renderer(), x, y, h, alpha);
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
        pendingIcons.push_back(icon);
        if (label.empty() || label == "/") {
            pos = next;
            continue;
        }
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
int Style::button(Context &ctx, const string &key, int x, int y, int height) const {
    ableem::Texture icon = ctx.glyph(key);
    ableem::Renderer &renderer = ctx.renderer();
    if (icon.valid()) {
        ableem::Size s = icon.size();
        Rect dst(x, y + (height - s.h) / 2, s.w, s.h);
        ableem::Texture outline = ctx.glyphOutline(key);
        if (outline.valid()) {
            Rect outlineDst(dst.x - 2, dst.y - 2, s.w + 5, s.h + 5);
            renderer.copy(outline, nullptr, &outlineDst);
        }
        renderer.copy(icon, nullptr, &dst);
        return s.w;
    }
    // a chip: the name in capitals, a box around it
    const string name = upper(key);
    const ableem::Font &font = ctx.font(FontRole::Small);
    const int tw = ctx.textWidth(font, name);
    const int chipH = height - 6;
    const int chipW = tw + 14;
    Rect chip(x, y + (height - chipH) / 2, chipW, chipH);
    // the `chip` frame is the plate under the name (G5d); a theme with none keeps the box drawn in code
    if (!drawFrame(ctx, "chip", chip)) {
        renderer.setBlendMode(ableem::BlendMode::Blend);
        renderer.setDrawColor(Color(255, 255, 255, 24));
        renderer.fillRect(chip);
        renderer.setDrawColor(Color(edge.r, edge.g, edge.b, 200));
        renderer.drawRect(chip);
    }
    ctx.drawText(font, name, chip.x + 7, chip.y + (chipH - font.lineHeight()) / 2, text);
    return chipW;
}

int Style::buttonWidth(Context &ctx, const string &key, int height) const {
    ableem::Texture icon = ctx.glyph(key);
    if (icon.valid())
        return icon.size().w;
    (void)height;
    return ctx.textWidth(ctx.font(FontRole::Small), upper(key)) + 14;
}

int Style::buttons(Context &ctx, const string &markers, int x, int y, int height) const {
    return layoutButtons(ctx, markers, x, y, height, true);
}

int Style::buttonsWidth(Context &ctx, const string &markers, int height) const {
    return layoutButtons(ctx, markers, 0, 0, height, false);
}

int Style::layoutButtons(Context &ctx, const string &markers, int x, int y, int height, bool draw) const {
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
                x += (draw ? button(ctx, plain, x, y, height) : buttonWidth(ctx, plain, height)) + 6;
            }
        }
        if (open == string::npos)
            break;
        size_t close = markers.find('|', open + 2);
        if (close == string::npos)
            break;
        const string key = markers.substr(open + 2, close - open - 2);
        x += (draw ? button(ctx, key, x, y, height) : buttonWidth(ctx, key, height)) + 6;
        pos = close + 1;
    }
    return x - startX;
}

//*******************************
// Style::footer
//*******************************
namespace {
// the shared order of the footer's hints, by the hint's first button
int buttonRank(const string &icon) {
    static const char *order[] = {"X", "O", "T", "S", "Start", "Select", "L1", "R1", "L2", "R2", "Enter", "Esc", "Tab"};
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++)
        if (icon == order[i])
            return static_cast<int>(i);
    return 100;
}
} // namespace

void Style::footer(Context &ctx, const Rect &footer, const vector<HintItem> &given, const string &status,
                   bool withRule) const {
    if (withRule)
        rule(ctx.renderer(), footer, footer.y);
    vector<HintItem> hints = given;
    stable_sort(hints.begin(), hints.end(), [](const HintItem &a, const HintItem &b) {
        return buttonRank(a.icons.empty() ? "" : a.icons[0]) < buttonRank(b.icons.empty() ? "" : b.icons[0]);
    });
    const int iconH = buttonHeight;
    const int y = footer.y + footerTop;
    const int IconOnlyGap = 16; // between hints once labels are dropped (C1: no fallback after the Small font)
    // what a button takes: a face button its image (buttonHeight square), a named one its chip
    auto buttonWidth = [&](const string &key) {
        if (key == "X" || key == "O" || key == "T" || key == "S")
            return buttonHeight;
        return ctx.textWidth(ctx.font(FontRole::Small), upper(key)) + 14;
    };
    // the status at the right edge, in the description colour; the hints get what is left
    int right = footer.x + footer.w - rowInset;
    const ableem::Font &statusFont = ctx.font(FontRole::Row);
    if (!status.empty()) {
        const int w = ctx.textWidth(statusFont, status);
        ctx.drawText(statusFont, status, right - w, y, description);
        right -= w + 36;
    }
    // the largest font the hints fit in, then the gap between them; `iconsOnly` drops every hint's own label
    // (not the button chip's own text, e.g. "L2" - only h.label) once even the smallest font does not fit,
    // so a hint is never drawn past `room` - see the fallback below
    const int room = right - (footer.x + rowInset);
    auto widthAt = [&](const ableem::Font &font, int gap, bool iconsOnly) {
        int w = 0;
        for (const HintItem &h : hints) {
            for (const string &icon : h.icons)
                w += buttonWidth(icon) + 6;
            w += iconsOnly ? gap : 2 + ctx.textWidth(font, h.label) + gap;
        }
        return w - gap;
    };
    ableem::Font font = ctx.font(FontRole::Row);
    int gap = 36;
    bool iconsOnly = false;
    if (widthAt(font, gap, false) > room) {
        gap = 22;
        if (widthAt(font, gap, false) > room) {
            font = ctx.font(FontRole::RowSmall);
            if (widthAt(font, gap, false) > room) {
                font = ctx.font(FontRole::Small);
                if (widthAt(font, gap, false) > room) {
                    // even the smallest font's labels do not fit: cut the labels ("Back" -> "B..", the longest
                    // first) and only when even the shortest do not fit, icons only - they are fixed width, so
                    // that always fits unless there are too many hints for even bare icons, which is outside
                    // this fallback's job (UIREV-3) and is left to clip as before
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

void Style::footer(Context &ctx, const Rect &footerRect, const string &line, bool withRule) const {
    string status;
    vector<HintItem> items = parseHints(line, status);
    footer(ctx, footerRect, items, status, withRule);
}

} // namespace abgui
