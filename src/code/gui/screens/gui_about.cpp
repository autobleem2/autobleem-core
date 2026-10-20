//
// Created by screemer on 2019-01-24.
//

#include "gui_about.h"
#include "gui_surprise.h"
#include <algorithm>
#include <string>
#include "../gui.h"
#include "../../core/services/environment.h"
#include "about_trace.h"

using about_trace::Stage;

const std::string GuiAbout::HeadingMark = "";

void GuiAbout::init() {
    Stage whole("init total");
    std::shared_ptr<Gui> gui(Gui::getInstance());
    fx.renderer = &renderer;
    // the credits' small font: the launcher's medium face (about.ttf, an SST copy, went with the Sony fonts)
    {
        Stage stage("init credits font");
        font = Fonts::openNewSharedCachedFont(Fonts::defaultClassicFontPath(), 15, renderer);
    }
    {
        Stage stage("init logo");
        logo = ableem::Texture::loadFile(renderer, Env::getWorkingPath() + sep + "ablogo.png");
    }

    if (credits.empty()) {
        credits = autobleemCredits();
        foot = autobleemFoot();
    }
}

//*******************************
// GuiAbout::draw
//*******************************
// what the stack's frame holds (docs/ab-gui-plan.md, G3c): the credits
void GuiAbout::draw() {
    if (renderer.fourByThreeOutput())
        drawCreditsNarrow();
    else
        drawCredits();
}

//*******************************
// GuiAbout::autobleemCredits
//*******************************
vector<string> GuiAbout::autobleemCredits() {
    auto heading = [](const string &text) { return HeadingMark + text; }; // render() draws it as a heading
    return {
        heading(_("Code C++ and shell scripts")),
        "screemer, Axanar, mGGk, nex, genderbent",
        heading(_("Graphics")),
        "KaonashiFTW, GeekAndy, rubixcube6, NewbornfromHell",
        heading(_("Testing")),
        "MagnusRC, xboxiso, Azazel, Solidius, SupaSAIAN, Kingherb, saptis",
        heading(_("Database maintenance")),
        "Screemer,Kingherb",
        heading(_("Localization support")),
        "nex(German), Azazel(Polish), gadsby(Turkish), GeekAndy(Dutch), Pardubak(Slovak), SupaSAIAN(Spanish), "
        "Mate(Czech)",
        "Sasha(Italian), Jakejj(BR_Portuguese), jolny(Swedish), StepJefli(Danish), alucard73 / MagnusRC(French), "
        "Quenti(Occitan), ",
        heading(_("RetroArch and emulation cores")),
        "genderbent, KMFDManic",
        heading(_("Ported from AutoBleem-NG")),
        "cornelk (AutoBleem-NG), Axanar - lightgun games, RDB metadata, libretro-thumbnails covers, multi-disc merge",
        heading(_("Game data")),
        "libretro-database (Sony - PlayStation.rdb), libretro-thumbnails",
    };
}

//*******************************
// GuiAbout::autobleemFoot
//*******************************
// the lines under the credits: support, and - GPLv3 5(d), the "Appropriate Legal Notices" an interactive
// program shows - the copyright and the licence
vector<string> GuiAbout::autobleemFoot() {
    return {
        _("Support via Discord:") + " https://discord.gg/AHUS3RM",
        "Copyright (C) 2018-2026 screemer and the AutoBleem contributors",
        _("Free software under the GNU GPL v3 or later - no warranty. Source and licence:") +
            " github.com/autobleem/AutoBleem2",
    };
}

//*******************************
// GuiAbout::drawCredits
//*******************************
void GuiAbout::drawCredits() {
    std::shared_ptr<Gui> gui(Gui::getInstance());

    gui->renderBackground();

    // the screen darkened to near black, no edge: the star field shows through what is left
    gui->panelStyle().box(renderer, ableem::Rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT), abgui::Tone::Black, 235,
                          abgui::Tone::None);

    fx.render(gui->platform().ticks());

    // UIREV-38: the screen draws in the theme's fonts and colour roles (no fixed fonts: every language's longest
    // heading and foot line fits - measured, see the design) and the credits sit on the theme's `panel` frame
    PanelStyle style = gui->panelStyle();
    Fonts &fonts = gui->assets().themeFonts;
    const ableem::Font &versionFont = fonts.atSize(FONT_MED, 16);
    const ableem::Font &lineFont = fonts.atSize(FONT_MED, 17);
    const ableem::Font &headingFont = fonts.atSize(FONT_BOLD, 18);
    const ableem::Font &textFont = fonts.atSize(FONT_MED, 15);
    auto centred = [&](const ableem::Font &f, const string &text, int y, const ableem::Color &color) {
        gui->text().renderText_WithColor(f, text, SCREEN_WIDTH / 2 - gui->text().textWidth(f, text) / 2, y, color,
                                         XALIGN_LEFT);
    };

    // the logo, centred: the theme's launcher.logo 400 wide (its height by its own aspect); a theme without one
    // keeps the old ablogo.png as it was
    int logoBottom;
    if (gui->launcherLogo().valid() && gui->launcherLogoRect().w > 0) {
        const ableem::Rect &place = gui->launcherLogoRect();
        const int logoWidth = 400;
        ableem::Rect rect;
        rect.w = logoWidth;
        rect.h = max(1, logoWidth * place.h / place.w);
        rect.x = (SCREEN_WIDTH - rect.w) / 2;
        rect.y = 30;
        renderer.copy(gui->launcherLogo(), nullptr, &rect);
        logoBottom = rect.y + rect.h;
    } else {
        ableem::Rect rect(SCREEN_WIDTH / 2 - 100, 5, 200, 141);
        renderer.copy(logo, nullptr, &rect);
        logoBottom = rect.y + rect.h;
    }
    const int versionTop = logoBottom + 14;
    centred(versionFont, Env::productVersion(), versionTop, style.secondary);
    const int creditTop = versionTop + 24;
    centred(lineFont, _("This version is brought to you by screemer. The AutoBleem team is back, baby!"), creditTop,
            style.text);

    // the credits as sections - a heading and its names wrapped under it - in two columns on the panel; the
    // sections stay in order and the split is where the taller column is shortest
    struct Section {
        string heading;
        vector<string> lines;
    };
    vector<Section> sections;
    for (const string &s : credits) {
        if (s.compare(0, HeadingMark.size(), HeadingMark) == 0)
            sections.push_back({s.substr(HeadingMark.size()), {}});
        else if (!sections.empty())
            sections.back().lines.push_back(s);
        else
            sections.push_back({"", {s}});
    }
    const int panelX = 64, panelWidth = SCREEN_WIDTH - 2 * panelX;
    const int columnGap = 48, columnWidth = 520;
    const int panelTop = creditTop + 36;
    auto sectionHeight = [&](const Section &sec) {
        int h = sec.heading.empty() ? 0 : 26;
        for (const string &line : sec.lines)
            h += max(20, gui->text().wrappedHeight(textFont, line, columnWidth));
        return h + 14;
    };
    vector<int> heights;
    for (const Section &sec : sections)
        heights.push_back(sectionHeight(sec));
    int total = 0;
    for (int h : heights)
        total += h;
    // the first section of the right column (== size: no right column)
    size_t split = sections.size();
    int columnHeight = total;
    int before = 0;
    for (size_t k = 1; k < sections.size(); k++) {
        before += heights[k - 1];
        const int taller = max(before, total - before);
        if (taller < columnHeight) {
            columnHeight = taller;
            split = k;
        }
    }
    const int panelHeight = columnHeight + 34;
    style.drawFrame(gui->uiContext(), "panel", ableem::Rect(panelX, panelTop, panelWidth, panelHeight));

    const int leftX = panelX + 32;
    int x = leftX, y = panelTop + 24;
    for (size_t i = 0; i < sections.size(); i++) {
        const Section &sec = sections[i];
        if (i == split) {
            x = leftX + columnWidth + columnGap;
            y = panelTop + 24;
        }
        if (!sec.heading.empty()) {
            gui->text().renderText_WithColor(headingFont, sec.heading, x, y, style.heading, XALIGN_LEFT);
            y += 26;
        }
        for (const string &line : sec.lines)
            y += max(20, gui->text().renderWrappedText(textFont, line, x, y, columnWidth, style.hint));
        y += 14;
    }

    // the foot: support, copyright, licence - centred under the panel
    y = panelTop + panelHeight + 22;
    for (const string &line : foot) {
        centred(textFont, line, y, style.secondary);
        y += 20;
    }

    // the footer's hints without its rule: the starfield is the panel here
    style.footer(*gui, gui->classicFooter(), "|@O| " + _("Back") + " |@Start| " + _("Surprise"), false);
}

//*******************************
// GuiAbout::buildPages
//*******************************
// the credits' sections and the foot as lines of one column `width` wide, cut into pages: the first has `firstRoom` px
// (under the logo), the others `room`. A section's heading never ends a page, and a line is never split.
void GuiAbout::buildPages(int width, int firstRoom, int room) {
    std::shared_ptr<Gui> gui(Gui::getInstance());
    Fonts &fonts = gui->assets().themeFonts;
    const ableem::Font &textFont = fonts.atSize(FONT_MED, TextSize);
    const ableem::Font &headingFont = fonts.boldAtSize(HeadingSize);
    const int lineH = textFont.lineHeight() + 2, headingH = headingFont.lineHeight() + 2;
    // a block is a heading with its lines: it stays on one page
    struct Block {
        std::vector<PageLine> lines;
        int height = 0;
    };
    std::vector<Block> blocks;
    auto addBody = [&](Block &b, const string &text) {
        const vector<string> wrapped = gui->text().wrapLines(textFont, text, width);
        for (size_t i = 0; i < wrapped.size(); i++) {
            PageLine line;
            line.text = wrapped[i];
            if (i + 1 == wrapped.size())
                line.gapAfter = 10;
            b.lines.push_back(line);
            b.height += lineH + line.gapAfter;
        }
    };
    for (const string &c : credits) {
        if (c.compare(0, HeadingMark.size(), HeadingMark) == 0) {
            Block b;
            PageLine h;
            h.text = c.substr(HeadingMark.size());
            h.heading = true;
            b.lines.push_back(h);
            b.height = headingH;
            blocks.push_back(b);
        } else {
            if (blocks.empty())
                blocks.push_back(Block());
            addBody(blocks.back(), c);
        }
    }
    Block footBlock;
    for (const string &f : foot)
        addBody(footBlock, f);
    blocks.push_back(footBlock);

    pages.clear();
    pages.emplace_back();
    int used = 0, cap = firstRoom;
    for (const Block &b : blocks) {
        if (used > 0 && used + b.height > cap) {
            pages.emplace_back();
            used = 0;
            cap = room;
        }
        for (const PageLine &l : b.lines)
            pages.back().push_back(l);
        used += b.height;
    }
    pageWidth = width;
    pageFirstRoom = firstRoom;
    pageRoom = room;
}

//*******************************
// GuiAbout::drawCreditsNarrow
//*******************************
// the credits on a 4:3 canvas (800x600, shown 0.8x): the logo and the line under it on the first page, then the
// sections in one column of text 22 px (17.6 on the screen) - a page at a time, turning by itself every PageMs or with
// the d-pad, "1/3" in the footer. The 1280x720 two-column design would be 12 px here.
void GuiAbout::drawCreditsNarrow() {
    std::shared_ptr<Gui> gui(Gui::getInstance());
    const int W = renderer.width();

    gui->renderBackground();
    gui->panelStyle().box(renderer, ableem::Rect(0, 0, renderer.width(), renderer.height()), abgui::Tone::Black, 235,
                          abgui::Tone::None);
    fx.render(gui->platform().ticks());

    PanelStyle style = gui->panelStyle();
    Fonts &fonts = gui->assets().themeFonts;
    const ableem::Font &textFont = fonts.atSize(FONT_MED, TextSize);
    const ableem::Font &headingFont = fonts.boldAtSize(HeadingSize);
    const int lineH = textFont.lineHeight() + 2, headingH = headingFont.lineHeight() + 2;
    const ableem::Rect footer = gui->classicFooter();
    auto centred = [&](const ableem::Font &f, const string &text, int y, const ableem::Color &color) {
        gui->text().renderText_WithColor(f, text, W / 2 - gui->text().textWidth(f, text) / 2, y, color, XALIGN_LEFT);
    };

    // the header of the first page
    ableem::Rect logoRect;
    const bool haveLogo = gui->launcherLogo().valid() && gui->launcherLogoRect().w > 0;
    if (haveLogo) {
        const ableem::Rect &place = gui->launcherLogoRect();
        logoRect.w = 260;
        logoRect.h = max(1, logoRect.w * place.h / place.w);
        logoRect.x = (W - logoRect.w) / 2;
        logoRect.y = 10;
    } else {
        logoRect = ableem::Rect(W / 2 - 50, 4, 100, 70);
    }
    const int headerBottom = logoRect.y + logoRect.h;
    const string credit = _("This version is brought to you by screemer. The AutoBleem team is back, baby!");
    const vector<string> creditLines = gui->text().wrapLines(textFont, credit, W - 80);
    const int headerH = headerBottom + 6 + lineH + static_cast<int>(creditLines.size()) * lineH + 8;

    const int panelX = 20, panelWidth = W - 2 * panelX, inset = 22, pad = 14;
    const int bodyWidth = panelWidth - 2 * inset;
    const int panelBottom = footer.y - 6;
    const int firstRoom = panelBottom - headerH - 2 * pad;
    const int room = panelBottom - 8 - 2 * pad;
    if (pages.empty() || pageWidth != bodyWidth || pageFirstRoom != firstRoom || pageRoom != room)
        buildPages(bodyWidth, firstRoom, room);
    const unsigned int now = gui->platform().ticks();
    if (page >= static_cast<int>(pages.size()))
        page = 0;
    if (pageSince == 0)
        pageSince = now;
    if (pages.size() > 1 && now - pageSince >= PageMs) {
        page = (page + 1) % static_cast<int>(pages.size());
        pageSince = now;
    }

    int top = 8;
    if (page == 0) {
        renderer.copy(haveLogo ? gui->launcherLogo() : logo, nullptr, &logoRect);
        centred(textFont, Env::productVersion(), headerBottom + 6, style.secondary);
        int y = headerBottom + 6 + lineH;
        for (const string &line : creditLines) {
            centred(textFont, line, y, style.text);
            y += lineH;
        }
        top = headerH;
    }
    // the panel hugs the page's lines
    int bodyH = 0;
    for (const PageLine &l : pages[static_cast<size_t>(page)])
        bodyH += (l.heading ? headingH : lineH) + l.gapAfter;
    const int panelHeight = min(panelBottom - top, bodyH + 2 * pad);
    style.drawFrame(gui->uiContext(), "panel", ableem::Rect(panelX, top, panelWidth, panelHeight));
    int y = top + pad;
    for (const PageLine &l : pages[static_cast<size_t>(page)]) {
        if (l.heading)
            gui->text().renderText_WithColor(headingFont, l.text, panelX + inset, y, style.heading, XALIGN_LEFT);
        else
            gui->text().renderText_WithColor(textFont, l.text, panelX + inset, y, style.hint, XALIGN_LEFT);
        y += (l.heading ? headingH : lineH) + l.gapAfter;
    }

    string hints = "|@O| " + _("Back") + " |@Start| " + _("Surprise");
    if (pages.size() > 1)
        hints += " |@Left+Right| " + _("Page") + " " + to_string(page + 1) + "/" + to_string(pages.size());
    style.footer(*gui, footer, hints, false);
}

//*******************************
// GuiAbout::loop
//*******************************
void GuiAbout::loop() {
    std::shared_ptr<Gui> gui(Gui::getInstance());
    menuVisible = true;
    while (menuVisible) {
        const unsigned int ticks = gui->platform().ticks();
        render();
        Event e;
        while (gui->input().poll(e)) {
            // this is for pc Only
            if (e.type == Event::Type::Quit) {
                menuVisible = false;
            }
            const bool press = e.type == Event::Type::ButtonDown || e.type == Event::Type::DpadDown;
            // 4:3 credits: the pages turn with the d-pad
            if (renderer.fourByThreeOutput() && press && pages.size() > 1) {
                const bool next = e.button == Button::DpadRight || e.button == Button::DpadDown;
                const bool prev = e.button == Button::DpadLeft || e.button == Button::DpadUp;
                if (next || prev) {
                    const int count = static_cast<int>(pages.size());
                    page = (page + (next ? 1 : count - 1)) % count;
                    pageSince = ticks;
                    app.audio().cursor.play();
                    continue;
                }
            }
            if (e.type == Event::Type::ButtonDown && e.button == Button::Start) {
                // the surprise game is a screen of its own, dropping in from the top over the credits
                GuiSurprise surprise(*gui, surpriseAssets);
                surprise.show();
                pageSince = gui->platform().ticks(); // the credits' page turns count from now
            } else if (e.type == Event::Type::ButtonDown && e.button == Button::Circle) {
                app.audio().cancel.play();
                menuVisible = false;
            }
        }
    }
}
