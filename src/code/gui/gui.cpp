//
// Created by screemer on 2018-12-19.
//

#include "gui.h"
#include <algorithm>
#include <cmath>
#include "screens/gui_splash.h"
#include "../app_base.h"
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <ableem/engine/log.h>
#include <ableem/ui/debug_driver.h>
#include <ab_gui/panel.h>

using namespace std;
using ableem::Button;
using ableem::Color;
using ableem::Event;
using ableem::Rect;
using ableem::Size;
using ableem::Texture;
//********************
// Gui::outputScale
//********************
// How much bigger than the 1280x720 canvas the window is. The console's display is 720p and its SDL is old, so
// there it is always 1. A Raspberry Pi on a 1080p (or bigger) display gets 1.5: the launcher then draws its
// covers and text at the display's resolution instead of being upscaled by the TV (see ableem::Renderer). On
// a dev host AB_OUTPUT_SCALE in the environment tries any scale in a window that size.
float Gui::outputScale() {
#if defined(AB_DEBUG_HOST)
    const char *env = getenv("AB_OUTPUT_SCALE");
    if (env && atof(env) >= 1.0) {
        return static_cast<float>(atof(env));
    }
#elif defined(AB_APPLIANCE) || defined(AB_PLATFORM_WIN)
    ableem::Size display = ableem::Platform::desktopDisplaySize();
    if (display.w >= 1920 && display.h >= 1080) {
        PLOG_INFO << "Display is " << display.w << "x" << display.h << ", drawing the 1280x720 UI at 1.5x";
        return 1.5f;
    }
#endif
    return 1.0f;
}

//********************
// Gui::multisampleSamples
//********************
// Anti-aliasing for the carousel's turned covers and everything else the renderer draws: MSAA on the
// window's GL context. 4x on a dev host and a Windows PC. None on an appliance: measured on a Pi 400 at 1080p
// (2026-09-18), even 2x misses vsync and halves the frame rate for stretches, where 0x holds 60 fps with an 18 ms worst
// frame - the covers get their smooth edges from the transparent margin PsCarouselGame composes them
// with instead. AB_MSAA in the environment overrides either, on the console too (off there by default).
int Gui::multisampleSamples() {
    const char *env = getenv("AB_MSAA");
    if (env) {
        return atoi(env);
    }
#if defined(AB_DEBUG_HOST) || defined(AB_PLATFORM_WIN)
    return 4;
#else
    return 0;
#endif
}

//********************
// Gui::fullscreen
//********************
// The whole screen on every real target - a console-like launcher has no window to be a window in. The
// console and the appliances already are (Wayland on the PSC, KMS/DRM on a Pi and the PC stick: the window
// is the display); on the Windows product it is SDL's desktop full screen. Only the dev build keeps its
// 1280x720 window (tools/win_drive.ps1 posts keys to it); AB_WINDOWED=1 in the environment asks a product
// build for one too, for a look.
bool Gui::fullscreen() {
#if defined(AB_DEBUG_HOST)
    return false;
#else
    return getenv("AB_WINDOWED") == nullptr;
#endif
}

//********************
// Gui::Gui
//********************
string Gui::windowTitle_ = "AutoBleem";

Gui::Gui()
    : ableem::GuiBase(windowTitle_, ScreenWidth, ScreenHeight, outputScale(), multisampleSamples(), fullscreen()),
      assets_(renderer(), AppBase::get().theme(), AppBase::get().config()),
      text_(renderer(), AppBase::get().theme(), assets_.themeFont, assets_.buttonTextureMap),
      uiContext_(renderer(), input(), platform()), stack_(renderer()) {
    wireUiContext();
    // the pad mappings the launcher and the pscbios wizard share; probePads() reads the first that exists
    input().loadMappings(Env::padMappingFiles());
    input().probePads();
    renderer().setPerfOverlay(AppBase::get().config().inifile.values["perfoverlay"] == "true");
}

//********************
// Gui::wireUiContext
//********************
// the face buttons' images: the launcher's hint icons for X/O/T (the theme's buttons when a theme has none),
// the theme's square, the d-pad arrows of the icon table (ThemeAssets' dpad*: the theme's launcher.icons, else the
// default's, else the launcher's own evoimg/dpad_*.png - ab_gui G5a); an invalid texture for every other key, which
// Style draws as a chip
static Texture faceIcon(ThemeAssets &assets, const string &key) {
    if (key == "X")
        return assets.hintCross.valid() ? assets.hintCross : assets.buttonTextureMap["X"];
    if (key == "O")
        return assets.hintCircle.valid() ? assets.hintCircle : assets.buttonTextureMap["O"];
    if (key == "T")
        return assets.hintTriangle.valid() ? assets.hintTriangle : assets.buttonTextureMap["T"];
    if (key == "S")
        return assets.buttonTextureMap["S"];
    if (key == "Up")
        return assets.dpadUp;
    if (key == "Down")
        return assets.dpadDown;
    if (key == "Left")
        return assets.dpadLeft;
    if (key == "Right")
        return assets.dpadRight;
    return Texture();
}

// faceIcon's outline (UIREV-2): only the d-pad arrows need one - X/O/T/S already carry the theme's own art and read
// fine on every theme's hint bar; none when the theme's icons carry their own glow ("iconHalo": false, G5a)
static Texture faceIconOutline(ThemeAssets &assets, const string &key) {
    if (key == "Up")
        return assets.dpadUpOutline;
    if (key == "Down")
        return assets.dpadDownOutline;
    if (key == "Left")
        return assets.dpadLeftOutline;
    if (key == "Right")
        return assets.dpadRightOutline;
    return Texture();
}

// every provider reads assets_/text_/the theme when it is called, never before: the fonts and textures are
// replaced on a theme load and dropped while a game has the display
void Gui::wireUiContext() {
    uiContext_.fontProvider = [this](abgui::FontRole role) -> const ableem::Font & {
        switch (role) {
        case abgui::FontRole::Title:
            return assets_.themeFonts[FONT_28_BOLD];
        case abgui::FontRole::Row:
            return assets_.themeFonts[FONT_22_MED];
        case abgui::FontRole::RowSmall:
            return assets_.themeFonts[FONT_20_BOLD];
        case abgui::FontRole::Small:
            return assets_.themeFonts[FONT_15_BOLD];
        case abgui::FontRole::Classic:
            return assets_.themeFont;
        }
        return assets_.themeFonts[FONT_22_MED];
    };
    uiContext_.glyphProvider = [this](const string &key) { return faceIcon(assets_, key); };
    uiContext_.glyphOutlineProvider = [this](const string &key) { return faceIconOutline(assets_, key); };
    uiContext_.textDrawer = [this](const ableem::Font &font, const string &line, int x, int y, const Color &color) {
        text_.renderText_WithColor(font, line, x, y, color, XALIGN_LEFT);
    };
    // a plain line in the font's own colour (a text page's blank and centred lines): the renderer's renderText
    uiContext_.lineDrawer = [this](const ableem::Font &font, const string &line, int x, int y,
                                   abgui::Context::LineAlign align) {
        text_.renderText(font, line, x, y, align == abgui::Context::LineAlign::Centre ? XALIGN_CENTER : XALIGN_LEFT);
    };
    // the halo under the text on or off, the previous state back (a dialog sets its style's and restores the theme's)
    uiContext_.shadowSwitch = [this](bool on) {
        TextRenderer::Shadow shadow = text_.shadow();
        const bool was = shadow.enabled;
        shadow.enabled = on;
        text_.setShadow(shadow);
        return was;
    };
    uiContext_.textMeasurer = [this](const ableem::Font &font, const string &line) {
        return text_.textWidth(font, line);
    };
    uiContext_.translator = [](const string &line) { return ableem::translate(line); };
    uiContext_.styleProvider = []() { return PanelStyle::styleFromTheme(AppBase::get().theme().launcher()); };
    // the theme's five UI sounds (AppAudio reloads them with the theme - asked for at the moment one plays)
    uiContext_.soundPlayer = [](abgui::UiSound sound) {
        AppAudio &audio = AppBase::get().audio();
        switch (sound) {
        case abgui::UiSound::Cursor:
            audio.cursor.play();
            break;
        case abgui::UiSound::Cancel:
            audio.cancel.play();
            break;
        case abgui::UiSound::HomeUp:
            audio.home_up.play();
            break;
        case abgui::UiSound::HomeDown:
            audio.home_down.play();
            break;
        case abgui::UiSound::Resume:
            audio.resume.play();
            break;
        }
    };
    // the clock stays unset: the widgets time by the platform's ticks, as the screens do today

    // the theme's background picture over black (what renderBackground draws)
    uiContext_.backdropDrawer = [this]() {
        renderer().setDrawColor(Color(0x00, 0x00, 0x00, 0x00));
        renderer().clear();
        renderer().copy(assets_.backgroundImg, nullptr, &assets_.backgroundRect);
    };
    // the full classic panel: the theme's menu panel, its bottom at least at the foot of the theme's status line
    // (where the footer band ends: the hints sat footerTop (14) below its top, at the status line's text y). A
    // compact panel is Gui's own state (setCompactPanel), never this rect.
    uiContext_.panelProvider = []() {
        const auto &classic = AppBase::get().theme().classic();
        Rect panel(classic.menuPanel.x, classic.menuPanel.y, classic.menuPanel.w, classic.menuPanel.h);
        const int statusFoot = classic.statusBar.textY + PanelStyle::FooterHeight - 14;
        if (statusFoot > panel.y + panel.h)
            panel.h = statusFoot - panel.y;
        return panel;
    };
    // the theme's logo at its place, and that place (the "please wait" picture's spinner goes under it)
    uiContext_.logoDrawer = [this]() {
        renderLogo(false);
        return assets_.logoRect;
    };
    // a short list's compact panel (abgui::List, Gui::setCompactPanel): the text renderer's rows follow it while it is
    // set (the rect is the Context's own, valid until it is dropped); classicPanel() asks the Context
    uiContext_.panelSwitch = [this](const Rect *rect) { text_.setPanelOverride(rect); };
    // every frame through the one stack: clear, draw, present (step G3c)
    uiContext_.setStack(stack_);
    // the theme's frames by name (step G4a): none unless the theme's own theme.json has launcher.frames
    uiContext_.frameProvider = [this](const string &name) { return frames_.frame(renderer(), name); };
    // the theme's icons by name and their halos (step G5a): every name falls back to the default's, then the built-in
    uiContext_.iconProvider = [this](const string &name) { return icons_.icon(renderer(), name); };
    uiContext_.iconHaloProvider = [this](const string &name) { return icons_.halo(renderer(), name); };
}

//*******************************
// themeFrames
//*******************************
// the frames of the theme in `dir` as the FrameSet takes them: only that theme's own - never the default theme's, so a
// theme without launcher.frames draws exactly as before
static map<string, abgui::FrameSpec> themeFrames(const string &dir) {
    map<string, abgui::FrameSpec> specs;
    for (const ableem::ThemeFrame &f : ableem::loadThemeFrames(dir)) {
        abgui::FrameSpec spec;
        spec.file = f.image;
        spec.file2x = f.image2x;
        spec.slice = abgui::Insets(f.slice.left, f.slice.top, f.slice.right, f.slice.bottom);
        spec.bleed = abgui::Insets(f.bleed.left, f.bleed.top, f.bleed.right, f.bleed.bottom);
        spec.fill = f.fill;
        spec.tint = f.tint;
        specs[f.name] = spec;
    }
    if (!specs.empty()) {
        PLOG_INFO << "Theme frames: " << specs.size() << " from " << dir;
    }
    return specs;
}

//*******************************
// Gui::getInstance
//*******************************
shared_ptr<Gui> Gui::getInstance() {
    static shared_ptr<Gui> s{new Gui};
    return s;
}

//*******************************
// Gui::splash
//*******************************
void Gui::splash(const string &message) {
    shared_ptr<Gui> gui(Gui::getInstance());
    gui->drawText(message);
}

//*******************************
// Gui::loadAssets
//*******************************
void Gui::loadAssets(bool reloadMusic) {
    text_.clearTextCache(); // keyed on the font handles about to be replaced
    assets_.load();
    frames_.assign(themeFrames(AppBase::get().theme().loadedPath())); // the textures load when first drawn
    icons_.assign(ThemeAssets::iconSpecs(AppBase::get().theme()), ThemeAssets::iconHalo(AppBase::get().theme()));
    AppBase::get().audio().loadTheme(reloadMusic);

    // the classic screens' text halo, on unless the theme says otherwise; the launcher sets its own
    // around its frame and puts this one back
    TextRenderer::Shadow shadow;
    const ableem::Opt<bool> &textShadow = AppBase::get().theme().classic().textShadow;
    shadow.enabled = !textShadow.set || textShadow;
    text_.setShadow(shadow);
    text_.setFonts(&assets_.themeFonts);
    text_.setCheckIconRightMargin(assets_.checkIconRightMargin);
}

//*******************************
// Gui::hideMouseCursor
//*******************************
void Gui::hideMouseCursor() {
    if (!platform().isDevHost()) {
        platform().hideAndGrabCursor();
    }
}

//*******************************
// Gui::criticalException
//*******************************
void Gui::criticalException(const string &text) {
    drawText(text);
    while (true) {
        input().waitForEvent(250); // nothing to draw until a press
        Event e;
        while (input().poll(e)) {
            if (e.type == Event::Type::Quit)
                return;
            else if (e.type == Event::Type::KeyUp && e.key == ableem::Key::Escape)
                return;

            if (e.type == Event::Type::ButtonDown) {
                return;
            }
        }
    }
}

//*******************************
// Gui::display
//*******************************
void Gui::display(bool resume) {
    PLOG_INFO << platform().versionString();

    if (!platform().hasDisplay()) {
        acquireDisplay(); // released for an emulator - see releaseDisplay()
    }
    platform().setScaleQuality(2);

    if (resume) {
        // back from a game: the theme and every cover reload before the launcher can draw - the spinner
        // on black meanwhile (GuiLauncher::render ends it with its first frame)
        beginBusy(_("Loading..."), [this]() { stack_.frame(Color(0, 0, 0, 255), []() {}); });
    }
    loadAssets();

    if (!resume) {
        // Options -> Interface -> "Splash screen": off skips the boot splash (AB_NO_SPLASH does too)
        if (AppBase::get().config().inifile.values["splashscreen"] != "false") {
            GuiSplash splashScreen(*this);
            splashScreen.show();
        }
        hideMouseCursor();
    }
}

//*******************************
// Gui::finish
//*******************************
void Gui::finish() {
    AppBase::get().audio().shutdown();
    assets_.backgroundImg = Texture();
}

//*******************************
// Gui::releaseDisplay
//*******************************
void Gui::releaseDisplay() {
    text_.clearTextCache();
    assets_.unload();  // before the renderer goes: SDL frees the textures with it
    frames_.release(); // the same for the frames' textures (the specs stay; they load again when next drawn)
    icons_.release();  // and the icons' textures and halos
    GuiBase::releaseDisplay();
}

//*******************************
// Gui::beginBusy / busyTick / endBusy / tickBusy
//*******************************
// ab_gui's abgui::Busy (step G3l), the stack's: the backdrop, the dimmed spinner, the message, the bar, the 40 ms
// pace and the input flush at the end.
void Gui::beginBusy(const string &message, const std::function<void()> &redraw) {
    stack_.busy().begin(message, redraw);
}

void Gui::busyTick() {
    stack_.busy().tick();
}

void Gui::endBusy() {
    stack_.busy().end();
}

void Gui::setBusyProgress(int done, int total) {
    stack_.busy().setProgress(done, total);
}

void Gui::tickBusy() {
    getInstance()->busyTick();
}

//*******************************
// Gui::renderFreeSpace
//*******************************
void Gui::renderFreeSpace() {
    // at the header's right edge, in the secondary colour, level with the title (the theme's
    // classic.freeSpaceText used to place it; the panel's header decides now)
    PanelStyle style = panelStyle();
    Rect panel = classicPanel();
    // a statvfs every frame for a number that changes when a game is copied: once every 2 s is plenty
    static string space;
    static unsigned int spaceAt = 0;
    const unsigned int now = platform().ticks();
    if (space.empty() || now - spaceAt >= 2000) {
        space = System::getAvailableSpace();
        spaceAt = now;
    }
    const string line = _("Free space") + ": " + space;
    const ableem::Font &font = assets_.themeFonts[FONT_22_MED];
    const int y = panel.y + 18 + (assets_.themeFonts[FONT_28_BOLD].lineHeight() - font.lineHeight()) / 2;
    text_.renderText_WithColor(font, line, panel.x + panel.w - PanelStyle::RowInset - text_.textWidth(font, line), y,
                               style.text, XALIGN_LEFT);
}

//*******************************
// Gui::renderBackground
//*******************************
// the Context's backdrop (wireUiContext: the theme's background picture over black)
void Gui::renderBackground() {
    uiContext_.drawBackdrop();
}

//*******************************
// Gui::showSplashPicture
//*******************************
// One frame of a picture across the whole screen - splash/retroarch.jpg, splash/autobleem.jpg - drawn
// on black when the file is missing, so the frame is never the carousel an emulator is about to cover.
void Gui::showSplashPicture(const string &name) {
    stack_.frame(Color(0, 0, 0, 255), [this, &name]() {
        const string path = Env::getWorkingPath() + sep + "splash" + sep + name;
        if (DirEntry::exists(path)) {
            Texture picture = Texture::loadFile(renderer(), path);
            Rect full(0, 0, ScreenWidth, ScreenHeight);
            renderer().copy(picture, nullptr, &full);
        }
    });
}

//*******************************
// Gui::renderLogo
//*******************************
int Gui::renderLogo(bool small) {
    if (!small) {
        renderer().copy(assets_.logo, nullptr, &assets_.logoRect);
        return 0;
    }
    return classicPanel().y + PanelStyle::HeaderHeight;
}

//*******************************
// Gui::panelStyle / classicPanel
//*******************************
PanelStyle Gui::panelStyle() {
    return PanelStyle::fromTheme(AppBase::get().theme().launcher());
}

// the panel's geometry and drawing are ab_gui's abgui::Panel (step G3b); the compact panel is the Context's
// (abgui::List sets it, step G3m), and TextRenderer's rows follow it through the Context's panelSwitch (wireUiContext)
static abgui::Panel currentPanel(Gui &gui) {
    return abgui::Panel(gui.classicPanel(), gui.uiContext().style());
}

void Gui::setCompactPanel(int rows, const ableem::Font &font) {
    uiContext_.setCompactPanel(abgui::Panel::compact(uiContext_, rows, font).rect());
}

void Gui::clearCompactPanel() {
    uiContext_.clearCompactPanel();
}

Rect Gui::classicPanel() {
    // the compact panel while one is set, else the Context's panel rect (wireUiContext: the theme's menu panel down to
    // the status line's foot)
    return uiContext_.currentPanelRect();
}

Rect Gui::classicContent() {
    return currentPanel(*this).content();
}

Rect Gui::classicFooter() {
    return currentPanel(*this).footer();
}

int Gui::classicRowsThatFit(const ableem::Font &font) {
    return currentPanel(*this).rowsThatFit(uiContext_, font);
}

void Gui::renderScrollMarkers(bool moreAbove, bool moreBelow) {
    currentPanel(*this).scrollMarkers(uiContext_, moreAbove, moreBelow);
}

//*******************************
// Gui::renderTextBar
//*******************************
void Gui::renderTextBar() {
    currentPanel(*this).sheet(uiContext_);
}

//*******************************
// Gui::renderHeader
//*******************************
int Gui::renderHeader(const string &title) {
    return currentPanel(*this).header(uiContext_, title);
}

//*******************************
// Gui::renderStatus
//*******************************
void Gui::renderStatus(const string &text, int /*posy*/) {
    currentPanel(*this).footer(uiContext_, text);
}

//*******************************
// Gui::drawText
//*******************************
void Gui::drawText(const string &text, const string &topLine) {
    stack_.busy().waitScreen(text, topLine);
}
