//
// ThemeAssets: the asset half of what Gui used to be.
//
#include "theme_assets.h"
#include "gui.h" // Gui::tickBusy, the spinner between the loads
#include "panel_style.h" // PanelStyle::outlineOf, the d-pad arrows' dark halo (UIREV-2)
#include "../core/services/environment.h"
#include "../core/main.h"

#include <ableem/engine/theme_spec.h> // themeImageFile, the @2x choice (ab_gui G4f)

using namespace std;
using ableem::Texture;

//*******************************
// ThemeAssets::ThemeAssets
//*******************************
ThemeAssets::ThemeAssets(ableem::Renderer &renderer, Theme &theme, Config &config)
    : renderer_(renderer), theme_(theme), config_(config) {}

//*******************************
// ThemeAssets::load
//*******************************
void ThemeAssets::unload() {
    themeFonts.closeAll();
    fixedFonts().closeAll();
    themeFont = ableem::Font();
    backgroundImg = Texture();
    logo = Texture();
    cdJewel = Texture();
    bigBoxFrame = Texture();
    buttonTextureMap.clear();
    hintCross = hintCircle = hintTriangle = Texture();
    dpadUp = dpadDown = dpadLeft = dpadRight = Texture();
    dpadUpOutline = dpadDownOutline = dpadLeftOutline = dpadRightOutline = Texture();
}

//*******************************
// ThemeAssets::load
//*******************************
void ThemeAssets::load() {
    theme_.load(); // (re)reads theme.json, merged over themes/default, every file resolved
    const ClassicTheme &classic = theme_.classic();
    const LauncherTheme &launcher = theme_.launcher();

    backgroundImg = Texture(); // release the previous theme's textures before loading the new ones

    logoRect.x = classic.logo.x;
    logoRect.y = classic.logo.y;
    logoRect.w = classic.logo.w;
    logoRect.h = classic.logo.h;

    backgroundImg = loadImage(renderer_, classic.background);
    Gui::tickBusy();
    // drawn at its own size from the top-left corner (the theme's is the screen's); the splash used to be
    // the only place setting this, which left every program without a splash with no background at all.
    // Logical: an @2x background's size() is its pixels over 2, the 1x one's size
    ableem::Size backgroundSize = backgroundImg.size();
    backgroundRect = ableem::Rect(0, 0, backgroundSize.w, backgroundSize.h);
    logo = loadImage(renderer_, classic.logo.file);
    bigBoxFrame = loadImage(renderer_, Env::getWorkingPath() + sep + "evoimg/bigbox.png");
    if (config_.inifile.values["jewel"] != "none") {
        if (config_.inifile.values["jewel"] == "default") {
            cdJewel = loadImage(renderer_, Env::getWorkingPath() + sep + "evoimg/nofilter.png");
        } else {
            cdJewel =
                loadImage(renderer_, Env::getWorkingPath() + sep + "evoimg/frames/" + config_.inifile.values["jewel"]);
        }
    } else {
        cdJewel = Texture();
    }

    Gui::tickBusy();
    const auto &b = classic.buttons;
    buttonTextureMap["O"] = loadImage(renderer_, b.circle);
    buttonTextureMap["X"] = loadImage(renderer_, b.cross);
    buttonTextureMap["T"] = loadImage(renderer_, b.triangle);
    buttonTextureMap["S"] = loadImage(renderer_, b.square);
    buttonTextureMap["Select"] = loadImage(renderer_, b.select);
    buttonTextureMap["Start"] = loadImage(renderer_, b.start);
    buttonTextureMap["L1"] = loadImage(renderer_, b.l1);
    buttonTextureMap["R1"] = loadImage(renderer_, b.r1);
    buttonTextureMap["L2"] = loadImage(renderer_, b.l2);
    buttonTextureMap["R2"] = loadImage(renderer_, b.r2);
    buttonTextureMap["Check"] = loadImage(renderer_, b.check);
    buttonTextureMap["Uncheck"] = loadImage(renderer_, b.uncheck);
    checkIconRightMargin = 0;
    // measured on the 1x file (`path`), whichever one is drawn: its pixels are logical, like the texture's size()
    auto rightMargin = [](const string &path, const Texture &texture) {
        ableem::Rect bounds = Texture::opaqueBounds(path);
        ableem::Size whole = texture.size();
        return (bounds.w > 0 && whole.w > 0) ? whole.w - (bounds.x + bounds.w) : 0;
    };
    checkIconRightMargin =
        max(rightMargin(b.check, buttonTextureMap["Check"]), rightMargin(b.uncheck, buttonTextureMap["Uncheck"]));
    buttonTextureMap["Esc"] = loadImage(renderer_, b.esc);
    buttonTextureMap["Enter"] = loadImage(renderer_, b.enter);
    buttonTextureMap["Tab"] = loadImage(renderer_, b.tab);
    hintCross = loadImage(renderer_, launcher.hints.cross);
    hintCircle = loadImage(renderer_, launcher.hints.circle);
    hintTriangle = loadImage(renderer_, launcher.hints.triangle);
    const string evoimg = Env::getWorkingPath() + sep + "evoimg" + sep;
    dpadUp = loadImage(renderer_, evoimg + "dpad_up.png");
    dpadDown = loadImage(renderer_, evoimg + "dpad_down.png");
    dpadLeft = loadImage(renderer_, evoimg + "dpad_left.png");
    dpadRight = loadImage(renderer_, evoimg + "dpad_right.png");
    // the arrows' own dark halo (UIREV-2, L1): a loaded Texture cannot be read back pixel by pixel, so the
    // outline is built from a fresh Image decode of the same file, once here rather than per frame - the 1x file
    // even when an @2x arrow is drawn: the outline is placed by the arrow's logical size (Style::button)
    dpadUpOutline = PanelStyle::outlineOf(renderer_, ableem::Image::loadFile(evoimg + "dpad_up.png"));
    dpadDownOutline = PanelStyle::outlineOf(renderer_, ableem::Image::loadFile(evoimg + "dpad_down.png"));
    dpadLeftOutline = PanelStyle::outlineOf(renderer_, ableem::Image::loadFile(evoimg + "dpad_left.png"));
    dpadRightOutline = PanelStyle::outlineOf(renderer_, ableem::Image::loadFile(evoimg + "dpad_right.png"));

    // a theme without launcher fonts (and a default theme without them either) gets the shipped pair -
    // Open Sans Medium/Bold (OFL), the stand-in for the console's SST since 2026-09-21
    // the classic screens' font is the same on every theme - the launcher's Open Sans, or the user's own when Options
    // says so; a theme's classic.font is not read (2026-09-29, the owner) - so it is never opened either
    string classicFont =
        Fonts::classicFontPath(config_.inifile.values["themefont"], config_.inifile.values["font"]);
    string medium =
        launcher.fonts.medium.empty() ? Env::getPathToFontsDir() + sep + "OpenSans-Medium.ttf" : launcher.fonts.medium;
    string bold =
        launcher.fonts.bold.empty() ? Env::getPathToFontsDir() + sep + "OpenSans-Bold.ttf" : launcher.fonts.bold;
    // ...unless the language needs glyphs no theme font has: then the one CJK font draws everything
    string cjk = Fonts::cjkFontFor(config_.inifile.values["language"]);
    if (!cjk.empty()) {
        PLOG_INFO << "Language " << config_.inifile.values["language"] << ": every font is " << cjk;
        classicFont = medium = bold = cjk;
    }
    classicFontFile_ = classicFont;
    PLOG_INFO << "Classic font: " << classicFont;
    Gui::tickBusy();
    themeFont = Fonts::openNewSharedCachedFont(classicFont, Fonts::ClassicFontSize, renderer_);
    if (!themeFont.valid() && classicFont != Fonts::defaultClassicFontPath()) {
        // a file that is no font (an empty one crashed every screen drawing with it) - the default instead
        PLOG_WARNING << "Cannot open the font " << classicFont << ", using the default";
        classicFont = classicFontFile_ = Fonts::defaultClassicFontPath();
        themeFont = Fonts::openNewSharedCachedFont(classicFont, Fonts::ClassicFontSize, renderer_);
    }
    fixedFonts().openAllFonts(medium, bold, renderer_);
    if (cjk.empty() && classicFont != Fonts::defaultClassicFontPath()) {
        PLOG_INFO << "UI font: " << classicFont;
        themeFonts.openAllFonts(classicFont, classicFont, renderer_); // a user's font has no bold of its own
    } else {
        themeFonts = fixedFonts(); // the same pair: shared handles, nothing opened twice
    }
    Gui::tickBusy();
}

//*******************************
// ThemeAssets::loadImage
//*******************************
Texture ThemeAssets::loadImage(ableem::Renderer &renderer, const string &file) {
    float pixelScale = 1.0f;
    const string picked = ableem::themeImageFile(file, renderer.outputScale(), pixelScale);
    if (pixelScale == 1.0f)
        return Texture::loadFile(renderer, picked); // the 1x file: the very call it always was
    return Texture::loadFile(renderer, picked, pixelScale);
}

//*******************************
// ThemeAssets::fixedFonts
//*******************************
Fonts &ThemeAssets::fixedFonts() {
    static Fonts fonts; // closed by unload() before the renderer goes
    return fonts;
}

//*******************************
// ThemeAssets::classicFontAtSize
//*******************************
ableem::Font ThemeAssets::classicFontAtSize(int size) {
    if (classicFontFile_.empty())
        return themeFont;
    return Fonts::openNewSharedCachedFont(classicFontFile_, size, renderer_);
}
