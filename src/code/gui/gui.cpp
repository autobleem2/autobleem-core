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
#include <ableem/engine/ext_trace.h>
#include <ableem/engine/log.h>
#include <ableem/engine/startup_timer.h>
#include <ableem/ui/debug_driver.h>
#include <ab_gui/panel.h>
#include <ab_gui/splash_picture.h>
#include "../core/model/picture_mask.h"

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

namespace {
// Gui::deferPadSetup(): the constructor leaves the pads to display(false); padSetupPending: not done yet
bool padSetupDeferred = false;
bool padSetupPending = false;

// the pad mappings the launcher and the pscbios wizard share; probePads() reads the first that exists (and starts
// SDL's joystick subsystems itself, so no pad event comes before it)
void setUpPads(ableem::Input &input) {
    ableem::StartupTimer timer("pad-setup");
    input.loadMappings(Env::padMappingFiles());
    input.probePads();
    padSetupPending = false;
}
} // namespace

void Gui::deferPadSetup() {
    padSetupDeferred = true;
}

Gui::Gui()
    : ableem::GuiBase(windowTitle_, ScreenWidth, ScreenHeight, outputScale(), multisampleSamples(), fullscreen()),
      assets_(renderer(), AppBase::get().theme(), AppBase::get().config()),
      text_(renderer(), AppBase::get().theme(), assets_.themeFont, assets_.buttonTextureMap),
      uiContext_(renderer(), input(), platform()), stack_(renderer()) {
    wireUiContext();
    renderer().setRestCanvas(CrtCanvasW, CrtCanvasH); // kept for a 4:3 output (applied at once on one, else after a live switch to one)
    if (padSetupDeferred)
        padSetupPending = true; // display(false) does it, on the splash
    else
        setUpPads(input());
    renderer().setPerfOverlay(AppBase::get().config().inifile.values["perfoverlay"] == "true");
}

//********************
// Gui::wireUiContext
//********************
// the face buttons' images: the launcher's hint icons for X/O/T/S (the theme's buttons when a theme has none),
// the d-pad arrows of the icon table (ThemeAssets' dpad*: the theme's launcher.icons, else the
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
        return assets.hintSquare.valid() ? assets.hintSquare : assets.buttonTextureMap["S"];
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
    TextRenderer::setSwitchContext(&uiContext_); // the theme's switch images for renderTextLineOptions (G5m)
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

    // the launcher's snapshot while a screen opened from it runs (G5r5), else the theme's background picture over black
    // (what renderBackground draws) - also when the snapshot's pixels were lost with the render targets
    uiContext_.backdropDrawer = [this]() {
        if (backdrop_.draw(renderer()))
            return;
        ableem::ext_trace::note("backdrop FALLBACK: transparent clear + theme background");
        renderer().setDrawColor(Color(0x00, 0x00, 0x00, 0x00));
        renderer().clear();
        renderer().copy(assets_.backgroundImg, nullptr, &assets_.backgroundRect);
    };
    // the full classic panel: the theme's menu panel, its bottom at least at the foot of the theme's status line
    // (where the footer band ends: the hints sat footerTop (14) below its top, at the status line's text y). A
    // compact panel is Gui's own state (setCompactPanel), never this rect.
    uiContext_.panelProvider = [this]() {
        const auto &classic = AppBase::get().theme().classic();
        Rect panel =
            text_.onCanvas(Rect(classic.menuPanel.x, classic.menuPanel.y, classic.menuPanel.w, classic.menuPanel.h));
        const int textY = text_.onCanvas(Rect(0, classic.statusBar.textY, 0, 0)).y; // on a 4:3 canvas it moves up
        const int statusFoot = textY + PanelStyle::FooterHeight - 14;
        if (statusFoot > panel.y + panel.h)
            panel.h = statusFoot - panel.y;
        return panel;
    };
    // the theme's logo at its place, and that place (the "please wait" picture's spinner goes under it)
    // (over the launcher's snapshot, which carries its own logo element, none is drawn - the place is still handed out)
    uiContext_.logoDrawer = [this]() {
        if (!backdrop_.held())
            renderLogo(false);
        return assets_.logoRect;
    };
    // a short list's compact panel (abgui::List, Gui::setCompactPanel): the text renderer's rows follow it while it is
    // set (the rect is the Context's own, valid until it is dropped); classicPanel() asks the Context
    uiContext_.panelSwitch = [this](const Rect *rect) { text_.setPanelOverride(rect); };
    // every frame through the one stack: clear, draw, present (step G3c)
    uiContext_.setStack(stack_);
    // the screen transitions (UIREV-48): the stack hears every screen open and close, and every press (which finishes a
    // running transition); while one runs every pass draws a frame
    stack_.attach(input());
    // the theme's frames by name (step G4a): none unless the theme's own theme.json has launcher.frames
    uiContext_.frameProvider = [this](const string &name) { return frames_.frame(renderer(), name); };
    // the theme's icons by name and their halos (step G5a): every name falls back to the default's, then the built-in
    uiContext_.iconProvider = [this](const string &name) { return icons_.icon(renderer(), name); };
    uiContext_.iconHaloProvider = [this](const string &name) { return icons_.halo(renderer(), name); };
    // the theme's own busy spinner strip (step G5p): none unless its theme.json has launcher.spinner - the ring of dots
    uiContext_.spinnerProvider = [this]() { return spinner_.anim(renderer()); };
    // the theme's own `disabled` role (step G5t): unset unless its theme.json has launcher.colors.disabled
    uiContext_.veilProvider = [this]() { return disabledVeil_; };
    // the theme's own inactive-state alphas (step G5r9): every value unset unless its theme.json has launcher.inactive
    uiContext_.inactiveProvider = [this]() { return inactiveAlphas_; };
    // the theme's own panel sheet (step G6c2): unset unless its theme.json has launcher.colors.sheet
    uiContext_.sheetProvider = [this]() { return panelSheet_; };
}

//*******************************
// themeSpinner
//*******************************
// the spinner strip of the theme in `dir` as the SpinnerStrip takes it: only that theme's own - never the default
// theme's, so a theme without launcher.spinner keeps the code-drawn ring
static abgui::SpinnerSpec themeSpinner(const string &dir) {
    abgui::SpinnerSpec spec;
    ableem::ThemeSpinner s;
    if (ableem::loadThemeSpinner(dir, s)) {
        spec.file = s.image;
        spec.file2x = s.image2x;
        spec.frames = s.frames;
        spec.fps = s.fps;
        PLOG_INFO << "Theme spinner: " << s.frames << " frames at " << s.fps << " fps from " << dir;
    }
    return spec;
}

//*******************************
// themeFrames
//*******************************
// the frames of the theme in `dir` as the FrameSet takes them: only that theme's own - never the default theme's, so a
// theme without launcher.frames draws exactly as before
static map<string, abgui::FrameSpec> themeFrames(const string &dir, const ableem::LauncherTheme &launcher) {
    map<string, abgui::FrameSpec> specs;
    // a "bridge:" image is one of the shared set a converted 1.0 theme points at (G6c): the launcher's bridge/
    // resources
    const string bridgeDir = Env::getWorkingPath() + sep + "bridge";
    for (const ableem::ThemeFrame &f : ableem::loadThemeFrames(dir, bridgeDir)) {
        abgui::FrameSpec spec;
        spec.file = f.image;
        spec.file2x = f.image2x;
        spec.slice = abgui::Insets(f.slice.left, f.slice.top, f.slice.right, f.slice.bottom);
        spec.bleed = abgui::Insets(f.bleed.left, f.bleed.top, f.bleed.right, f.bleed.bottom);
        spec.fill = f.fill;
        spec.tint = f.tint;
        // a tint that is a launcher.colors colour with no Style role (`selection`, the cover glow's): resolved here
        spec.tintResolved = PanelStyle::frameTintColor(launcher, f.tint, spec.tintColor);
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
    loadSplashAssets();
    loadRestAssets(reloadMusic);
}

//*******************************
// Gui::loadSplashAssets
//*******************************
void Gui::loadSplashAssets() {
    text_.clearTextCache(); // keyed on the font handles about to be replaced
    assets_.loadForSplash();
    // the theme's own `disabled` role (G5t): the veil's colour and alpha - nothing when it sets none
    const ableem::ThemeDisabledVeil veil = ableem::loadThemeDisabledVeil(AppBase::get().theme().loadedPath());
    disabledVeil_ = abgui::DisabledVeil();
    if (veil.set) {
        disabledVeil_.set = true;
        disabledVeil_.color = Color(veil.color.r, veil.color.g, veil.color.b, 255);
        disabledVeil_.alpha = static_cast<unsigned char>(veil.alpha);
    }
    // the theme's own panel sheet (G6c2): the colour and alpha under every panel - nothing when it sets none
    const ableem::ThemeSheet sheet = ableem::loadThemeSheet(AppBase::get().theme().loadedPath());
    panelSheet_ = abgui::PanelSheet();
    if (sheet.set) {
        panelSheet_.set = true;
        panelSheet_.color = Color(sheet.color.r, sheet.color.g, sheet.color.b, 255);
        panelSheet_.alpha = static_cast<unsigned char>(sheet.alpha);
    }
    // the theme's own inactive-state alphas (G5r9): nothing set when it has no launcher.inactive
    const ableem::ThemeInactiveAlphas inactive = ableem::loadThemeInactiveAlphas(AppBase::get().theme().loadedPath());
    inactiveAlphas_.resume = inactive.resume;
    inactiveAlphas_.tab = inactive.tab;
    inactiveAlphas_.barTrack = inactive.barTrack;
    // Options -> Interface -> "Animations" (UIREV-48): off, every screen change is instant
    stack_.setAnimations(AppBase::get().config().inifile.values["animations"] != "false");

    // the classic screens' text halo, on unless the theme says otherwise; the launcher sets its own
    // around its frame and puts this one back
    TextRenderer::Shadow shadow;
    const ableem::Opt<bool> &textShadow = AppBase::get().theme().classic().textShadow;
    shadow.enabled = !textShadow.set || textShadow;
    text_.setShadow(shadow);
    text_.setFonts(&assets_.themeFonts);
}

//*******************************
// Gui::loadRestAssets
//*******************************
void Gui::loadRestAssets(bool reloadMusic) {
    text_.clearTextCache(); // themeFonts are opened now
    assets_.loadRest();
    frames_.assign(themeFrames(AppBase::get().theme().loadedPath(),
                               AppBase::get().theme().launcher())); // the textures load when first drawn
    icons_.assign(ThemeAssets::iconSpecs(AppBase::get().theme()), ThemeAssets::iconHalo(AppBase::get().theme()));
    // the theme's own launcher logo and resume picture mask (G5q, G5s): nothing when it sets none
    const ableem::ThemeLauncherLogo logo = ableem::loadThemeLogo(AppBase::get().theme().loadedPath());
    launcherLogo_ = logo.set ? ThemeAssets::loadImage(renderer(), logo.file) : Texture();
    launcherLogoRect_ = launcherLogo_.valid() ? Rect(logo.x, logo.y, logo.w, logo.h) : Rect();
    resumeMask_ = ThemeAssets::loadImage(renderer(), ableem::loadThemeResumeMask(AppBase::get().theme().loadedPath()));
    spinner_.assign(themeSpinner(AppBase::get().theme().loadedPath())); // the strip loads when first drawn
    AppBase::get().audio().loadTheme(reloadMusic);
    text_.setFonts(&assets_.themeFonts);
    text_.setCheckIconRightMargin(assets_.checkIconRightMargin);
}

//*******************************
// Gui::resumePictureWindow
//*******************************
Rect Gui::resumePictureWindow() {
    Rect window(25, 33, 68, 52);
    const auto &picture = AppBase::get().theme().launcher().menuIcons.resumePicture;
    if (picture.set)
        window = Rect(picture.x, picture.y, picture.w, picture.h);
    return window;
}

//*******************************
// Gui::maskedResumePicture
//*******************************
// The mask is multiplied in once, here, not per frame: the picture is drawn over the whole target without blending
// (its colours and an opaque alpha), then the mask over it in BlendMode::Mask (colours kept, alpha = picture alpha x
// mask alpha). The result is straight-alpha, drawn with the normal blend like any picture.
Texture Gui::maskedResumePicture(const Texture &picture) {
    if (!picture.valid() || !resumeMask_.valid())
        return picture;
    const Rect window = resumePictureWindow();
    const PictureMask::Size size = PictureMask::composeSize(window.w, window.h);
    if (size.w <= 0 || size.h <= 0)
        return picture;
    Texture target = Texture::createTarget(renderer(), size.w, size.h);
    if (!target.valid())
        return picture;
    renderer().pushTarget(&target);
    renderer().setBlendMode(ableem::BlendMode::None);
    Texture source = picture; // a shared handle: the blend mode is put back below
    source.setBlendMode(ableem::BlendMode::None);
    renderer().setDrawColor(Color(0, 0, 0, 0));
    renderer().fillRect();
    renderer().copy(source, nullptr, nullptr);
    source.setBlendMode(ableem::BlendMode::Blend);
    resumeMask_.setBlendMode(ableem::BlendMode::Mask);
    renderer().copy(resumeMask_, nullptr, nullptr);
    renderer().popTarget();
    renderer().setBlendMode(ableem::BlendMode::Blend);
    target.setBlendMode(ableem::BlendMode::Blend);
    return target;
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
    bool splash = false;
    if (!resume) {
        // Options -> Interface -> "Splash screen": off skips the boot splash (AB_NO_SPLASH does too)
        splash = AppBase::get().config().inifile.values["splashscreen"] != "false";
#ifdef AB_DEBUG_HOST
        // AB_NO_SPLASH=1: straight through - the DebugDriver's tests (tools/ab_drive.py) start that way
        if (const char *skip = getenv("AB_NO_SPLASH"))
            splash = splash && *skip != '1';
#endif
    }

    if (splash) {
        // the theme preload: only what the splash draws now; the rest of the theme (music included) and the pads are
        // the splash's first steps of work, ahead of whatever the program gave it (GuiSplash::setWork)
        {
            ableem::StartupTimer timer("theme-preload");
            loadSplashAssets();
        }
        std::vector<std::function<void()>> first;
        first.push_back([this]() {
            ableem::StartupTimer timer("theme-rest");
            loadRestAssets();
        });
        if (padSetupPending)
            first.push_back([this]() { setUpPads(input()); });
        GuiSplash::pushWorkFront(std::move(first));
    } else {
        ableem::StartupTimer timer(resume ? "theme-reload" : "theme-load"); // images, fonts, the music
        loadAssets();
        if (padSetupPending)
            setUpPads(input());
    }

    if (!resume) {
        if (splash) {
            GuiSplash splashScreen(*this);
            splashScreen.show();
            // the plan's decision 12: after the splash the first screen (the launcher) drops in from the top; without
            // the splash it fades in from black (the launcher's own fade-in)
            stack_.setStartTransition(abgui::Transition::drop());
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
    backdrop_.clear();         // the launcher's snapshot is a texture of the renderer that goes
    assets_.unload();          // before the renderer goes: SDL frees the textures with it
    frames_.release();         // the same for the frames' textures (the specs stay; they load again when next drawn)
    icons_.release();          // and the icons' textures and halos
    launcherLogo_ = Texture(); // and the logo and the resume mask
    resumeMask_ = Texture();
    spinner_.release();      // and the spinner strip's
    stack_.releaseTargets(); // and the screen transitions' two pictures
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
void Gui::renderFreeSpace(const string &title) {
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
    // the line, then without the drive's size (the free part only), then that in the smallest font: the first that
    // fits to the right of the title - on a narrow (4:3) canvas the full line ran into the title (CONSOLE-17)
    const string full = _("Free space") + ": " + space;
    const size_t slash = space.find(" / ");
    const string shortLine = slash == string::npos ? full : _("Free space") + ": " + space.substr(0, slash);
    const int FreeSpaceGap = 24; // the least between the title and the line
    const int right = panel.x + panel.w - PanelStyle::RowInset;
    const int titleEnd = title.empty() ? panel.x
                                       : panel.x + PanelStyle::RowInset +
                                             text_.textWidth(assets_.themeFonts[FONT_28_BOLD], title) + FreeSpaceGap;
    const ableem::Font *font = &assets_.themeFonts[FONT_22_MED];
    string line = full;
    if (right - text_.textWidth(*font, line) < titleEnd) {
        line = shortLine;
        if (right - text_.textWidth(*font, line) < titleEnd)
            font = &assets_.themeFonts[FONT_15_BOLD];
    }
    const int y = panel.y + 18 + (assets_.themeFonts[FONT_28_BOLD].lineHeight() - font->lineHeight()) / 2;
    text_.renderText_WithColor(*font, line, right - text_.textWidth(*font, line), y, style.text, XALIGN_LEFT);
}

//*******************************
// Gui::renderBackground
//*******************************
// the Context's backdrop (wireUiContext: the theme's background picture over black)
void Gui::renderBackground() {
    uiContext_.drawBackdrop();
}

//*******************************
// Gui::setLauncherBackdrop / clearLauncherBackdrop / hasLauncherBackdrop
//*******************************
void Gui::setLauncherBackdrop(const Texture &frame) {
    backdrop_.set(frame, renderer().targetsLost());
}

void Gui::clearLauncherBackdrop() {
    backdrop_.clear();
}

bool Gui::hasLauncherBackdrop() const {
    return backdrop_.held();
}

//*******************************
// Gui::showSplashPicture
//*******************************
// One frame of a picture across the whole screen - splash/retroarch.jpg, splash/autobleem.jpg - drawn
// on black when the file is missing, so the frame is never the carousel an emulator is about to cover.
void Gui::showSplashPicture(const string &name) {
    stack_.frame(Color(0, 0, 0, 255), [this, &name]() {
        // any 4:3 output (720x480 CRT, 640x480, 800x600, 1024x768 ...) shows the picture's 4:3 twin
        // ("autobleem-4x3.jpg") when there is one, across the whole 4:3 canvas
        const string path = abgui::splashPicturePath(Env::getWorkingPath() + sep + "splash" + sep + name,
                                                     renderer().fourByThreeOutput(),
                                                     [](const string &p) { return DirEntry::exists(p); });
        if (DirEntry::exists(path)) {
            Texture picture = Texture::loadFile(renderer(), path);
            Rect full(0, 0, renderer().width(), renderer().height());
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

// a footer is one row and the window makes room for it: the panel is as wide as footerLine's footer needs
void Gui::setCompactPanel(int rows, const ableem::Font &font, const std::string &footerLine) {
    uiContext_.setCompactPanel(abgui::Panel::compact(uiContext_, rows, font, footerLine).rect());
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
