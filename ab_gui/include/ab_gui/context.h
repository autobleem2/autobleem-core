// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Context: what ab_gui's drawing needs from the program that uses it - the renderer, the fonts by role,
// the button glyphs, the text drawing, the translator and the current Style - with no singletons. A program
// builds one and keeps it (AutoBleem's Gui owns its own); every primitive and, later, every widget takes it.
//
// Everything but the renderer, the input and the platform is a provider, asked when it is needed and never
// cached: a font, a texture or a sound is only good until the program drops it (a theme reload, the display
// handed to an emulator and taken back), so a Context never holds one itself. The renderer, the input and the
// platform outlive all of that (GuiBase keeps them across the display's release), so it holds those.
// ab_gui knows nothing of AutoBleem and nothing of SDL: only lib_ableem's ableem types cross this interface.
//
#pragma once

#include <ab_gui/actions.h>
#include <ab_gui/frame.h>
#include <ab_gui/screen_stack.h>
#include <ab_gui/style.h>

#include <ableem/ui/font.h>
#include <ableem/ui/input.h>
#include <ableem/ui/platform.h>
#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>
#include <ableem/ui/types.h>

#include <functional>
#include <string>

namespace abgui {

// The UI's sound effects: the five of a console-style menu (the PlayStation Classic's set, which AutoBleem's
// themes carry) - a cursor step, a cancel, the two "home" sounds the lists play on a page or a jump to the
// first/last row, and the resume sound. Which one a widget plays is the widget's (as today's screens do).
enum class UiSound { Cursor, Cancel, HomeUp, HomeDown, Resume };

//********************
// Context
//********************
class Context {
public:
    using FontProvider = std::function<const ableem::Font &(FontRole)>;
    // a button's image by its marker key ("X", "O", "T", "S", "Up", ...); an invalid texture = no image
    using GlyphProvider = std::function<ableem::Texture(const std::string &)>;
    // one run of text at (x, y) - the program's own text drawing (AutoBleem's has the halo and the run cache)
    using TextDrawer = std::function<void(const ableem::Font &, const std::string &, int, int, const ableem::Color &)>;
    using TextMeasurer = std::function<int(const ableem::Font &, const std::string &)>;
    using Translator = std::function<std::string(const std::string &)>;
    using StyleProvider = std::function<Style()>;
    // plays one of the UI's sounds (the program's own - AutoBleem's are the theme's)
    using SoundPlayer = std::function<void(UiSound)>;
    // milliseconds from some fixed start - what the widgets time hold-repeat, blinking and animations by
    using Clock = std::function<unsigned int()>;
    // draws the screen's own background over the whole canvas (AutoBleem: the theme's background picture)
    using BackdropDrawer = std::function<void()>;
    // a rect of the program's in the canvas' coordinates (AutoBleem: the theme's classic menu panel)
    using RectProvider = std::function<ableem::Rect()>;

    // a drawing-only Context: no input, the clock only when one is set (a test's, say), else 0
    explicit Context(ableem::Renderer &renderer) : renderer_(&renderer) {}
    // what a program's screens get: the renderer, the input they read and the platform whose ticks they time
    // by - all three GuiBase's, which outlive every screen
    Context(ableem::Renderer &renderer, ableem::Input &input, ableem::Platform &platform)
        : renderer_(&renderer), input_(&input), platform_(&platform) {}

    ableem::Renderer &renderer() const { return *renderer_; }
    // the input the screens read (and set their frame need on); only with the three-argument constructor -
    // hasInput() says whether there is one
    bool hasInput() const { return input_ != nullptr; }
    ableem::Input &input() const { return *input_; }

    // What the program supplies. An unset provider falls back to something harmless (see the calls below).
    FontProvider fontProvider;
    GlyphProvider glyphProvider;
    // the dark outline drawn under a glyph (Style::outlineOf's texture), where it needs one to stay readable
    GlyphProvider glyphOutlineProvider;
    TextDrawer textDrawer;
    TextMeasurer textMeasurer;
    Translator translator;
    StyleProvider styleProvider;

    // What the drawing calls.
    // the font for `role`, or an invalid font when there is no provider
    const ableem::Font &font(FontRole role) const;
    // the glyph / its outline for `key`, or an invalid texture
    ableem::Texture glyph(const std::string &key) const;
    ableem::Texture glyphOutline(const std::string &key) const;
    // the text through the program's drawer, else the font's own plain drawing
    void drawText(const ableem::Font &font, const std::string &text, int x, int y, const ableem::Color &color) const;
    // the width of the text through the program's measurer, else the font's own
    int textWidth(const ableem::Font &font, const std::string &text) const;
    // the text through the translator, else unchanged
    std::string translate(const std::string &text) const;
    // the current look through the provider, else the defaults
    Style style() const;
    // the sound through the player, else nothing
    void play(UiSound sound) const;
    // the time in milliseconds: the clock when one is set, else the platform's ticks, else 0
    unsigned int ticks() const;
    // waits `ms` milliseconds on the platform (a hold-repeat's few ms between looks at the queue); nothing
    // without one
    void delay(unsigned int ms) const;
    // the screen's background through the drawer, else the canvas cleared to black (step G3b)
    void drawBackdrop() const;
    // the full classic panel's rect (abgui::Panel::full) through the provider, else the canvas inset by the
    // style's margin all round (step G3b)
    ableem::Rect panelRect() const;

private:
    ableem::Renderer *renderer_;
    ableem::Font none_; // what font() hands out without a provider

    // Appended after the members above (step G3a), so their offsets stay what they were.
public:
    SoundPlayer soundPlayer;
    Clock clock; // unset: the platform's ticks

private:
    ableem::Input *input_ = nullptr;
    ableem::Platform *platform_ = nullptr;

    // Appended (step G3b): the backdrop and the classic panel's rect.
public:
    BackdropDrawer backdropDrawer;
    RectProvider panelProvider;

    // Appended (step G3c): the screen stack every frame goes through - clear, the screen's drawing, present
    // (ScreenStack::frame). The program owns it (AutoBleem's Gui) and sets it; hasStack() says whether one is set.
    // Setting it binds the stack's busy spinner (stack().busy(), step G3l) to this Context.
    void setStack(ScreenStack &stack) {
        stack_ = &stack;
        stack.busy().bind(*this);
    }
    bool hasStack() const { return stack_ != nullptr; }
    ScreenStack &stack() const { return *stack_; }

private:
    ScreenStack *stack_ = nullptr;

    // Appended (step G3g): the program's one ActionMap - the pad and keys -> actions every abgui::Screen reads its
    // events through (the default map, the Confirm/Back swap off). One for the program, so a swap set from an
    // Options row holds on every screen.
public:
    ActionMap actions;

    // Appended (step G3h): a plain line of text in the font's own colour, left aligned at x or centred on the canvas
    // (x is then not used) - what a page's blank and centred lines are drawn with. AutoBleem's is the text renderer's
    // renderText without a colour; unset, the style's text colour through the text drawer.
    enum class LineAlign { Left, Centre };
    using LineDrawer = std::function<void(const ableem::Font &, const std::string &, int x, int y, LineAlign)>;
    LineDrawer lineDrawer;
    void drawLine(const ableem::Font &font, const std::string &text, int x, int y, LineAlign align) const;

    // Appended (step G3j): switches the halo under the text (Style::textShadow) on or off for the text drawn from now
    // on and returns what it was, so a dialog can set its own and put the program's back. AutoBleem's is the text
    // renderer's shadow; unset, nothing happens and false comes back.
    using ShadowSwitch = std::function<bool(bool)>;
    ShadowSwitch shadowSwitch;
    bool setTextShadow(bool on) const;

    // Appended (step G3l): draws the program's logo at its place and returns that place - what the "please wait"
    // picture (Busy::waitScreen) puts its spinner under. AutoBleem's is the theme's logo (Gui::renderLogo); unset,
    // nothing is drawn and an empty rect at (0, 0) comes back.
    using LogoDrawer = std::function<ableem::Rect()>;
    LogoDrawer logoDrawer;
    ableem::Rect drawLogo() const;

    // Appended (step G3m part 2): the compact panel a short list draws in (abgui::List) - while one is set it is the
    // panel the rows are in (currentPanelRect()), and the program's own row drawing follows it: the panelSwitch is told
    // the rect when one is set and null when it is dropped (AutoBleem's moves its text renderer's rows and Gui's
    // classicPanel()). Unset, only the Context keeps it.
    using PanelSwitch = std::function<void(const ableem::Rect *)>;
    PanelSwitch panelSwitch;
    void setCompactPanel(const ableem::Rect &rect);
    void clearCompactPanel();
    bool hasCompactPanel() const { return compact_; }
    // the compact panel while one is set, else panelRect()
    ableem::Rect currentPanelRect() const;

private:
    bool compact_ = false;
    ableem::Rect compactRect_;

    // Appended (step G4a): the frames of the current look by name ("panel", ...) - what Style::drawFrame asks for
    // before it draws a primitive's code-drawn shape. AutoBleem's is Gui's FrameSet, filled from the theme's own
    // launcher.frames. Like every provider it is asked at draw time (the textures go with the display); unset, or an
    // invalid Frame back, means no frame: the primitive draws as it always did.
public:
    using FrameProvider = std::function<Frame(const std::string &)>;
    FrameProvider frameProvider;
    Frame frame(const std::string &name) const;
};

} // namespace abgui
