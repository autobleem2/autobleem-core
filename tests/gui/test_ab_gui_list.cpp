//
// abgui::List (G3m part 2 of docs/ab-gui-plan.md): the row geometry against a copy of the old TextRenderer formulas and
// the switch rows' ON/OFF (pure), the Context's compact panel state, and through a headless GuiBase (those cases skip
// themselves without a renderer, like test_ab_gui_screen): a draw's order - the backdrop, the compact panel set for a
// short list before the sheet and dropped after the footer, the title, the rows on the page with the cursor's marked,
// the footer - the rows' layout (labels at the text's left, values right-aligned, headings, switches), the
// DebugDriver's items/selected, and the events through handle(): the moves and their sounds, L1/R1 first/last, L2/R2 a
// page, Cross and Circle, the keys as keys, and a held d-pad at the shared HoldRepeat pace until another event comes.
//
#include "doctest/doctest.h"

#include <ab_gui/context.h>
#include <ab_gui/frame.h>
#include <ab_gui/icon.h>
#include <ab_gui/list.h>
#include <ab_gui/panel.h>

#include <ableem/ui/debug_driver.h>
#include <ableem/ui/gui_base.h>

#include <cstdlib>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

using namespace std;
using abgui::Context;
using abgui::List;
using abgui::Panel;
using abgui::Style;
using abgui::UiSound;
using ableem::Button;
using ableem::Color;
using ableem::Event;
using ableem::GuiBase;
using ableem::Key;
using ableem::Rect;

namespace {

//*******************************
// the old TextRenderer row formulas, frozen (text_renderer.cpp before G3m part 2; PanelStyle::RowInset = 24)
//*******************************
int oldTextX(const Rect &opscreen, int xoffset) {
    return opscreen.x + 24 + 8 + xoffset;
}
int oldRowY(int line, int yoffset, int fontHeight) {
    int y = (fontHeight * line) + yoffset;
    if (line < 0) {
        line = -line;
        y = line;
    }
    return y;
}
int oldValueRight(const Rect &opscreen, int rightEdge) {
    return rightEdge > 0 ? rightEdge : opscreen.x + opscreen.w - 24 - 8;
}
Rect oldBox(const Rect &opscreen, int line, int yoffset, int xoffset, int fontHeight, int rightEdge) {
    Rect r;
    r.x = opscreen.x + 1 + xoffset;
    r.y = yoffset + fontHeight * (line);
    r.w = (rightEdge > 0 ? rightEdge + 12 : opscreen.x + opscreen.w - 1) - r.x;
    r.h = fontHeight;
    return r;
}
int oldSwitch(const string &_text, string &text) {
    text = _text;
    int button = -1;
    if (text.find("|@Check|") != std::string::npos)
        button = 1;
    if (text.find("|@Uncheck|") != std::string::npos)
        button = 0;
    if (button != -1)
        text = text.substr(0, text.find("|"));
    return button;
}

bool sameRect(const Rect &a, const Rect &b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
bool sameColor(const Color &a, const Color &b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

struct MaybeGui {
    unique_ptr<GuiBase> gui;

    MaybeGui() {
#ifdef _WIN32
        _putenv_s("AB_HEADLESS", "1");
#else
        setenv("AB_HEADLESS", "1", 1);
#endif
        try {
            gui = unique_ptr<GuiBase>(new GuiBase("ab_gui_test_list", 1280, 720));
        } catch (const exception &e) {
            const string why = e.what();
            MESSAGE("test_ab_gui_list: skipping - no usable renderer here (" << why << ")");
        }
        if (gui)
            gui->input().flushEvents();
    }

    bool available() const { return gui != nullptr; }
};

// what a list drew and played: the Context's text, the backdrop, the compact panel switch, the sounds
struct Side {
    struct Text {
        string text;
        int x, y;
        Color color;
    };
    Context ctx;
    vector<UiSound> sounds;
    vector<Text> texts;
    vector<string> calls; // the order of the drawing hooks (the list's own below add theirs)
    vector<Rect> switched;
    Rect full{100, 50, 900, 600};
    Style style;

    explicit Side(GuiBase &gui) : ctx(gui.renderer(), gui.input(), gui.platform()) {
        ctx.soundPlayer = [this](UiSound s) { sounds.push_back(s); };
        style.rowSelected = Color(255, 255, 255, 255);
        style.row = Color(100, 100, 100, 255);
        style.value = Color(90, 90, 90, 255);
        style.heading = Color(80, 80, 80, 255);
        ctx.styleProvider = [this]() { return style; };
        ctx.panelProvider = [this]() { return full; };
        ctx.backdropDrawer = [this]() { calls.push_back("backdrop"); };
        ctx.textDrawer = [this](const ableem::Font &, const string &text, int x, int y, const Color &color) {
            texts.push_back({text, x, y, color});
        };
        ctx.textMeasurer = [](const ableem::Font &, const string &text) { return 10 * static_cast<int>(text.size()); };
        ctx.translator = [](const string &text) { return "~" + text; };
        ctx.panelSwitch = [this](const Rect *rect) {
            calls.push_back(rect ? "compact" : "full");
            if (rect)
                switched.push_back(*rect);
        };
    }
};

// a list whose rows, title and footer say when they are drawn
struct Recorder : List {
    Side &side;
    int count;
    vector<Rect> panelAtRow; // the Context's current panel while each row drew
    Recorder(GuiBase &gui, Side &s, int rows) : List(gui, s.ctx), side(s), count(rows) {}
    int size() override { return count; }
    string titleText() override {
        side.calls.push_back("title");
        return "Title";
    }
    string statusText() override {
        side.calls.push_back("status");
        return "";
    }
    void drawRow(int index, int line, bool isSelected) override {
        side.calls.push_back("row " + to_string(index) + "@" + to_string(line) + (isSelected ? "*" : ""));
        panelAtRow.push_back(ctx.currentPanelRect());
    }
};

// a list over plain rows that draws nothing but counts its frames and steps
struct Quiet : List {
    vector<int> stepsAt; // the clock at each step
    function<void()> onRedraw;
    unsigned int *clock = nullptr;
    Quiet(GuiBase &gui, Context &ctx, int n) : List(gui, ctx) {
        for (int i = 0; i < n; i++)
            rows.push_back({"row " + to_string(i), "", false, false});
        maxVisible = 4;
        lastVisible = 3;
        firstRender = false;
    }
    void draw() override {}
    void step(int by) override {
        if (clock)
            stepsAt.push_back(static_cast<int>(*clock));
        List::step(by);
    }
    void redraw() override {
        if (onRedraw)
            onRedraw();
    }
};

Event button(Button b, bool down = true) {
    Event e;
    e.type = down ? Event::Type::ButtonDown : Event::Type::ButtonUp;
    e.button = b;
    return e;
}

Event dpad(bool down, Button b) {
    Event e;
    e.type = down ? Event::Type::DpadDown : Event::Type::DpadUp;
    e.button = b;
    return e;
}

Event key(Key k, bool down = true) {
    Event e;
    e.type = down ? Event::Type::KeyDown : Event::Type::KeyUp;
    e.key = k;
    return e;
}

// the event through the Input as the loop reads it (the d-pad state follows), then to the list
void feed(GuiBase &gui, List &list, const Event &event) {
    gui.input().inject(event);
    Event polled;
    REQUIRE(gui.input().poll(polled));
    list.handle(polled);
}

// a d-pad tap: the press and its release queued together, so the held press takes its one step and returns (the
// release is pending), then the release (nothing held any more: nothing)
void tap(GuiBase &gui, List &list, Button direction) {
    gui.input().inject(dpad(true, direction));
    gui.input().inject(dpad(false, direction));
    Event polled;
    REQUIRE(gui.input().poll(polled));
    list.handle(polled);
    REQUIRE(gui.input().poll(polled));
    list.handle(polled);
}

} // namespace

//*******************************
// the row geometry (pure)
//*******************************
TEST_CASE("List::rowTop / textLeft / valueRight / band: the old TextRenderer formulas, number for number") {
    const Style style;
    const Rect panels[] = {Rect(100, 50, 900, 600), Rect(240, 180, 800, 360), Rect(0, 0, 1280, 720)};
    for (const Rect &panel : panels) {
        for (int xoffset : {0, 15, 300}) {
            CHECK(List::textLeft(panel, style, xoffset) == oldTextX(panel, xoffset));
            for (int rightEdge : {0, 700, 1000}) {
                CHECK(List::valueRight(panel, style, rightEdge) == oldValueRight(panel, rightEdge));
                for (int line : {0, 3, 7}) {
                    for (int fontHeight : {0, 28, 36}) {
                        const int yoffset = panel.y + 74;
                        CHECK(sameRect(List::band(panel, yoffset + fontHeight * line, fontHeight, xoffset, rightEdge),
                                       oldBox(panel, line, yoffset, xoffset, fontHeight, rightEdge)));
                    }
                }
            }
        }
    }
    for (int line : {-400, -1, 0, 1, 9})
        for (int fontHeight : {0, 30})
            CHECK(List::rowTop(line, 124, fontHeight) == oldRowY(line, 124, fontHeight));
}

TEST_CASE("List::switchState: |@Check| is ON, |@Uncheck| OFF (it wins when both), the label is the text before") {
    const string cases[] = {"Sound|@Check|",  "Sound |@Uncheck|", "Both|@Check||@Uncheck|", "Plain", "",
                            "Icon |@X| here", "|@Check|"};
    for (const string &text : cases) {
        string label, oldLabel;
        CHECK(List::switchState(text, &label) == oldSwitch(text, oldLabel));
        CHECK(label == oldLabel);
        CHECK(List::switchState(text) == oldSwitch(text, oldLabel));
    }
    string label;
    CHECK(List::switchState("Sound|@Check|", &label) == 1);
    CHECK(label == "Sound");
    CHECK(List::switchState("Mode|@Uncheck|", &label) == 0);
    CHECK(label == "Mode");
}

TEST_CASE("List::isCompact: up to CompactRows rows with nothing beside them") {
    for (int size = 0; size <= List::CompactRows; size++) {
        CHECK(List::isCompact(size, 0));
        CHECK_FALSE(List::isCompact(size, 600));
    }
    CHECK_FALSE(List::isCompact(List::CompactRows + 1, 0));
    CHECK_FALSE(List::isCompact(40, 0));
}

//*******************************
// the Context's compact panel
//*******************************
TEST_CASE("Context::setCompactPanel: the current panel follows, the program's switch is told, clear goes back") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    CHECK_FALSE(side.ctx.hasCompactPanel());
    CHECK(sameRect(side.ctx.currentPanelRect(), side.full));
    const Rect compact(240, 200, 800, 300);
    side.ctx.setCompactPanel(compact);
    CHECK(side.ctx.hasCompactPanel());
    CHECK(sameRect(side.ctx.currentPanelRect(), compact));
    REQUIRE(side.switched.size() == 1);
    CHECK(sameRect(side.switched[0], compact));
    side.ctx.clearCompactPanel();
    CHECK_FALSE(side.ctx.hasCompactPanel());
    CHECK(sameRect(side.ctx.currentPanelRect(), side.full));
    CHECK(side.calls == vector<string>{"compact", "full"});

    Context bare(g.gui->renderer()); // no switch: only the Context keeps it
    bare.setCompactPanel(compact);
    CHECK(sameRect(bare.currentPanelRect(), compact));
    bare.clearCompactPanel();
    CHECK_FALSE(bare.hasCompactPanel());
}

//*******************************
// the drawing
//*******************************
TEST_CASE("List::draw: a short list - the backdrop, its compact panel around the sheet..footer, the page's rows") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Recorder list(*g.gui, side, 3);
    list.selected = 1;
    list.maxVisible = 5;
    list.draw();
    CHECK(side.calls ==
          vector<string>{"backdrop", "status", "compact", "title", "row 0@0", "row 1@1*", "row 2@2", "status", "full"});
    // the status line is read first: the compact panel widens for its one-row footer.
    // The compact panel is Panel::compact's for the list's rows, font and footer line, and the rows were drawn in it
    REQUIRE(side.switched.size() == 1);
    const Rect expected = Panel::compact(side.ctx, 3, list.font, list.statusText()).rect();
    CHECK(sameRect(side.switched[0], expected));
    REQUIRE(list.panelAtRow.size() == 3);
    for (const Rect &r : list.panelAtRow)
        CHECK(sameRect(r, expected));
    CHECK_FALSE(side.ctx.hasCompactPanel());
    // the rows start under the header of that panel
    CHECK(list.yoffset == expected.y + side.style.headerHeight);
    CHECK_FALSE(list.firstRender);
}

TEST_CASE("List::draw: a long list, or a short one with a pane beside it, draws in the full panel") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Recorder list(*g.gui, side, 20);
    list.maxVisible = 4;
    list.selected = 12; // the first draw computes the page around the cursor
    list.firstRow = 1;
    list.draw();
    CHECK(side.calls == vector<string>{"backdrop", "title", "row 10@1", "row 11@2", "row 12@3*", "row 13@4", "status"});
    CHECK(list.firstVisible == 10);
    CHECK(list.lastVisible == 13);
    CHECK(list.yoffset == side.full.y + side.style.headerHeight);
    for (const Rect &r : list.panelAtRow)
        CHECK(sameRect(r, side.full));

    side.calls.clear();
    Recorder pane(*g.gui, side, 3);
    pane.selectionRightEdge = 700;
    pane.labelsOnly = true; // nothing to pick: no row is the cursor's
    pane.draw();
    CHECK(side.calls == vector<string>{"backdrop", "title", "row 0@0", "row 1@1", "row 2@2", "status"});
}

TEST_CASE("List::draw: with a selection frame it is drawn before the rows (under their text), without one after") {
    MaybeGui g;
    if (!g.available())
        return;
    ableem::Renderer &renderer = g.gui->renderer();

    // what the list asked the Context's frame provider for, next to the rows: "frame selection" is the query of
    // selectionFramed() first, then the frame's drawing (a frame that is there is drawn, one that is not falls back to
    // the code-drawn band)
    auto order = [](const vector<string> &calls) {
        vector<string> out;
        for (const string &c : calls)
            if (c.compare(0, 3, "row") == 0 || c == "frame selection")
                out.push_back(c);
        return out;
    };

    // no frame (every shipped theme): the rows, then the band over them - the classic order
    {
        Side side(*g.gui);
        side.ctx.frameProvider = [&](const string &name) {
            side.calls.push_back("frame " + name);
            return abgui::Frame();
        };
        Recorder list(*g.gui, side, 3);
        list.selected = 1;
        list.maxVisible = 5;
        list.draw();
        CHECK(order(side.calls) ==
              vector<string>{"frame selection", "row 0@0", "row 1@1*", "row 2@2", "frame selection"});
    }

    // the same list on a context with no provider at all draws exactly the old call order
    {
        Side side(*g.gui);
        Recorder list(*g.gui, side, 3);
        list.selected = 1;
        list.maxVisible = 5;
        list.draw();
        CHECK(side.calls ==
              vector<string>{"backdrop", "status", "compact", "title", "row 0@0", "row 1@1*", "row 2@2", "status", "full"});
    }

    // a `selection` frame: drawn before the first row, so the text reads over it
    {
        abgui::FrameSet set;
        abgui::FrameSpec spec;
        spec.file = string(AB_TEST_DATA_DIR) + "/frame-test-theme/frames/selection.png";
        spec.slice = abgui::Insets(12, 10, 12, 10);
        spec.bleed = abgui::Insets::all(4);
        map<string, abgui::FrameSpec> specs;
        specs["selection"] = spec;
        set.assign(specs);
        Side side(*g.gui);
        side.ctx.frameProvider = [&](const string &name) {
            side.calls.push_back("frame " + name);
            return set.frame(renderer, name);
        };
        Recorder list(*g.gui, side, 3);
        list.selected = 1;
        list.maxVisible = 5;
        list.draw();
        CHECK(order(side.calls) ==
              vector<string>{"frame selection", "frame selection", "row 0@0", "row 1@1*", "row 2@2"});
        set.release();
    }
}

TEST_CASE(
    "List::drawRows: labels at the text's left, values right-aligned, switches ON/OFF, headings in their colour") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    List list(*g.gui, side.ctx);
    list.rows = {{"Sound", "Loud", false, false},
                 {"Audio", "", true, false},
                 {"Music|@Check|", "", false, false},
                 {"Mode|@Uncheck|", "", false, true}};
    list.maxVisible = 8;
    list.lastVisible = 7;
    list.selected = 2;
    list.yoffset = 124;
    list.drawRows();
    const int left = List::textLeft(side.full, side.style);
    const int right = List::valueRight(side.full, side.style);
    REQUIRE(side.texts.size() == 7);
    CHECK(side.texts[0].text == "Sound");
    CHECK(side.texts[0].x == left);
    CHECK(sameColor(side.texts[0].color, side.style.row));
    CHECK(side.texts[1].text == "Loud");
    CHECK(side.texts[1].x == right - 40);
    CHECK(sameColor(side.texts[1].color, side.style.value));
    CHECK(side.texts[2].text == "Audio");
    CHECK(sameColor(side.texts[2].color, side.style.heading));
    CHECK(side.texts[3].text == "Music");
    CHECK(sameColor(side.texts[3].color, side.style.rowSelected));
    CHECK(side.texts[4].text == "~ON");
    CHECK(side.texts[4].x == right - 30);
    CHECK(sameColor(side.texts[4].color, side.style.rowSelected));
    CHECK(side.texts[5].text == "Mode");
    CHECK(side.texts[6].text == "~OFF");
    CHECK(sameColor(side.texts[6].color, side.style.value));
    for (const Side::Text &t : side.texts)
        CHECK(t.y == 124); // the font's line height is 0 here (no font provider)
}

TEST_CASE("List::drawRows: a theme with switchOn/switchOff draws the image, not ON/OFF; a disabled row keeps its veil (G5m)") {
    MaybeGui g;
    if (!g.available())
        return;
    const string dir = string(AB_TEST_DATA_DIR) + "/frame-test-theme/icons/";
    abgui::IconSet set;
    map<string, abgui::IconSpec> specs;
    specs["switchOn"] = {dir + "switch_on.png", dir + "switch_on@2x.png"};
    specs["switchOff"] = {dir + "switch_off.png", dir + "switch_off@2x.png"};
    set.assign(specs);

    Side side(*g.gui);
    vector<string> asked;
    side.ctx.iconProvider = [&](const string &name) {
        asked.push_back(name);
        return set.icon(g.gui->renderer(), name);
    };
    List list(*g.gui, side.ctx);
    list.rows = {{"Sound", "Loud", false, false}, {"Music|@Check|", "", false, false}, {"Mode|@Uncheck|", "", false, true}};
    list.maxVisible = 8;
    list.lastVisible = 7;
    list.yoffset = 124;
    list.drawRows();
    // both icons are asked for on every switch row (the images are used only when the theme has both)
    CHECK(asked == vector<string>{"switchOn", "switchOff", "switchOn", "switchOff"});
    REQUIRE(side.texts.size() == 4); // the four texts below: no ~ON / ~OFF
    vector<string> said;
    for (const Side::Text &t : side.texts)
        said.push_back(t.text);
    // the switch rows' labels are drawn, their values are images
    CHECK(said == vector<string>{"Sound", "Loud", "Music", "Mode"});
}

TEST_CASE("List::drawSwitch: no icon of that name - nothing drawn, false (default and ab2 have none)") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    CHECK_FALSE(List::drawSwitch(side.ctx, true, 868, 124, 28)); // no provider at all
    side.ctx.iconProvider = [](const string &) { return ableem::Texture(); };
    CHECK_FALSE(List::drawSwitch(side.ctx, false, 868, 124, 28));
}

TEST_CASE("List::drawSwitch: a theme with only one of switchOn/switchOff draws neither - ON/OFF text in both states") {
    MaybeGui g;
    if (!g.available())
        return;
    const string dir = string(AB_TEST_DATA_DIR) + "/frame-test-theme/icons/";
    abgui::IconSet set;
    map<string, abgui::IconSpec> specs;
    specs["switchOn"] = {dir + "switch_on.png", dir + "switch_on@2x.png"};
    set.assign(specs); // no switchOff
    Side side(*g.gui);
    side.ctx.iconProvider = [&](const string &name) { return set.icon(g.gui->renderer(), name); };
    CHECK_FALSE(List::drawSwitch(side.ctx, true, 868, 124, 28));
    CHECK_FALSE(List::drawSwitch(side.ctx, false, 868, 124, 28));

    specs.clear();
    specs["switchOff"] = {dir + "switch_off.png", dir + "switch_off@2x.png"};
    set.assign(specs); // no switchOn
    CHECK_FALSE(List::drawSwitch(side.ctx, true, 868, 124, 28));
    CHECK_FALSE(List::drawSwitch(side.ctx, false, 868, 124, 28));

    List list(*g.gui, side.ctx);
    list.rows = {{"Music|@Check|", "", false, false}, {"Mode|@Uncheck|", "", false, false}};
    list.maxVisible = 8;
    list.lastVisible = 7;
    list.yoffset = 124;
    list.drawRows();
    vector<string> said;
    for (const Side::Text &t : side.texts)
        said.push_back(t.text);
    CHECK(said == vector<string>{"Music", "~ON", "Mode", "~OFF"});
}

TEST_CASE("List::driverItems / driverSelected: every row as shown, a heading marked '#', '|' as '/'") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    List list(*g.gui, side.ctx);
    list.rows = {{"Games", "", true, false}, {"Scan|now", "", false, false}, {"Theme", "Dark", false, false}};
    list.selected = 2;
    CHECK(list.driverItems() == vector<string>{"#Games", "Scan/now", "Theme"});
    CHECK(list.driverSelected() == 2);
    list.labelsOnly = true;
    CHECK(list.driverSelected() == -1);
    list.labelsOnly = false;
    list.rows.clear();
    CHECK(list.driverItems().empty());
    CHECK(list.driverSelected() == -1);
}

TEST_CASE("List: every move publishes the cursor at once, and a pick publishes the row it takes before it closes") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Quiet list(*g.gui, side.ctx, 20);
    list.visible = true;
    ableem::DebugDriver::setActiveForTest(true);
    ableem::DebugDriver::pushScreen(typeid(list).name());
    // no draw() anywhere in this test: only the moves publish
    feed(*g.gui, list, button(Button::R2));
    CHECK(ableem::DebugDriver::selected() == 4);
    CHECK(ableem::DebugDriver::items().size() == 20);
    feed(*g.gui, list, button(Button::R1));
    CHECK(ableem::DebugDriver::selected() == 19);
    feed(*g.gui, list, button(Button::L1));
    CHECK(ableem::DebugDriver::selected() == 0);
    tap(*g.gui, list, Button::DpadDown);
    CHECK(ableem::DebugDriver::selected() == 1);
    tap(*g.gui, list, Button::DpadUp);
    CHECK(ableem::DebugDriver::selected() == 0);
    feed(*g.gui, list, button(Button::R2));
    CHECK(ableem::DebugDriver::selected() == 4);
    feed(*g.gui, list, button(Button::L2));
    CHECK(ableem::DebugDriver::selected() == 0);
    list.selected = 7; // moved by the caller, not by a press: the pick still tells the driver
    feed(*g.gui, list, button(Button::Cross));
    CHECK_FALSE(list.visible);
    CHECK(ableem::DebugDriver::selected() == 7);
    ableem::DebugDriver::popScreen();
    ableem::DebugDriver::setActiveForTest(false);
}

//*******************************
// the events
//*******************************
TEST_CASE("List: L1/R1 the first/last row, L2/R2 a page, with the classic list's sounds") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Quiet list(*g.gui, side.ctx, 20);
    feed(*g.gui, list, button(Button::R1));
    CHECK(list.selected == 19);
    CHECK(list.firstVisible == 16);
    feed(*g.gui, list, button(Button::L1));
    CHECK(list.selected == 0);
    feed(*g.gui, list, button(Button::R2));
    CHECK(list.selected == 4);
    CHECK(list.firstVisible == 4);
    feed(*g.gui, list, button(Button::L2));
    CHECK(list.selected == 0);
    CHECK(side.sounds == vector<UiSound>{UiSound::HomeDown, UiSound::HomeDown, UiSound::HomeUp, UiSound::HomeDown});
    // the releases do nothing
    side.sounds.clear();
    feed(*g.gui, list, button(Button::R1, false));
    CHECK(list.selected == 0);
    CHECK(side.sounds.empty());
}

TEST_CASE("List: Cross closes with the Cursor sound (not an empty list), Circle closes cancelled with Cancel") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Quiet list(*g.gui, side.ctx, 3);
    list.visible = true;
    list.cancelled = true;
    feed(*g.gui, list, button(Button::Cross));
    CHECK_FALSE(list.visible);
    CHECK_FALSE(list.cancelled);
    list.visible = true;
    feed(*g.gui, list, button(Button::Circle));
    CHECK_FALSE(list.visible);
    CHECK(list.cancelled);
    CHECK(side.sounds == vector<UiSound>{UiSound::Cursor, UiSound::Cancel});

    Quiet empty(*g.gui, side.ctx, 0);
    empty.visible = true;
    feed(*g.gui, empty, button(Button::Cross));
    CHECK(empty.visible);
}

TEST_CASE("List: a d-pad tap steps a row by the live state, wrapping, headings skipped, the Cursor sound") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Quiet list(*g.gui, side.ctx, 4);
    list.rows[1].heading = true;
    tap(*g.gui, list, Button::DpadDown);
    CHECK(list.selected == 2); // over the heading
    tap(*g.gui, list, Button::DpadDown);
    CHECK(list.selected == 3);
    tap(*g.gui, list, Button::DpadDown);
    CHECK(list.selected == 0); // wrapped
    tap(*g.gui, list, Button::DpadUp);
    CHECK(list.selected == 3);
    CHECK(side.sounds == vector<UiSound>(4, UiSound::Cursor));
    list.labelsOnly = true; // nothing to pick: the sound, no move
    tap(*g.gui, list, Button::DpadUp);
    CHECK(list.selected == 3);
}

TEST_CASE("List: the keys as keys (the keyboard-as-pad off) - arrows, Page Up/Down, Home/End, Enter, Esc") {
    MaybeGui g;
    if (!g.available())
        return;
    g.gui->input().setKeyboardAsPad(false);
    Side side(*g.gui);
    Quiet list(*g.gui, side.ctx, 20);
    feed(*g.gui, list, key(Key::Down));
    CHECK(list.selected == 1);
    feed(*g.gui, list, key(Key::Up));
    CHECK(list.selected == 0);
    feed(*g.gui, list, key(Key::End));
    CHECK(list.selected == 19);
    feed(*g.gui, list, key(Key::Home));
    CHECK(list.selected == 0);
    feed(*g.gui, list, key(Key::PageDown));
    CHECK(list.selected == 4);
    feed(*g.gui, list, key(Key::PageUp));
    CHECK(list.selected == 0);
    feed(*g.gui, list, key(Key::Down, false)); // a release: nothing
    CHECK(list.selected == 0);
    list.visible = true;
    feed(*g.gui, list, key(Key::Return));
    CHECK_FALSE(list.visible);
    list.visible = true;
    feed(*g.gui, list, key(Key::Escape));
    CHECK_FALSE(list.visible);
    CHECK(list.cancelled);
    g.gui->input().setKeyboardAsPad(true);
}

TEST_CASE("List: with the keyboard-as-pad on (the default) the arrows are the d-pad, Enter Cross, Backspace Circle") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    Quiet list(*g.gui, side.ctx, 5);
    g.gui->input().inject(key(Key::Down));
    g.gui->input().inject(key(Key::Down, false));
    Event polled;
    REQUIRE(g.gui->input().poll(polled));
    CHECK(polled.type == Event::Type::DpadDown);
    list.handle(polled); // one step: the release is pending
    REQUIRE(g.gui->input().poll(polled));
    list.handle(polled);
    CHECK(list.selected == 1);
    list.visible = true;
    feed(*g.gui, list, key(Key::Return));
    CHECK_FALSE(list.visible);
    CHECK_FALSE(list.cancelled);
    list.visible = true;
    feed(*g.gui, list, key(Key::Backspace));
    CHECK_FALSE(list.visible);
    CHECK(list.cancelled);
}

TEST_CASE("List::holdRows: a held d-pad steps at once, then at HoldRepeat's pace, until another event comes") {
    MaybeGui g;
    if (!g.available())
        return;
    Side side(*g.gui);
    unsigned int now = 0;
    side.ctx.clock = [&now]() { return now += 2; }; // every look at the clock 2 ms on (the loop's delay(2))
    Quiet list(*g.gui, side.ctx, 30);
    list.clock = &now;
    int frames = 0;
    list.onRedraw = [&]() {
        if (++frames == 4)
            g.gui->input().inject(dpad(false, Button::DpadDown)); // the release: pending, the hold ends
    };
    g.gui->input().inject(dpad(true, Button::DpadDown));
    Event polled;
    REQUIRE(g.gui->input().poll(polled));
    list.handle(polled);
    // the press at 2; the first repeat 350 ms after it, then every 80 ms
    CHECK(list.stepsAt == vector<int>{2, 352, 432, 512});
    CHECK(frames == 4);
    CHECK(list.selected == 4);
    CHECK(side.sounds == vector<UiSound>(4, UiSound::Cursor));
    REQUIRE(g.gui->input().poll(polled));
    list.handle(polled); // the release: nothing held
    CHECK(list.selected == 4);
}

// the end-of-list rule (the owner, 2026-10-05): a press at the end wraps, a held d-pad stops there
namespace {
// holds `dir` (DpadDown/DpadUp) on a list at `from` until the clock reaches `until` ms, then releases; returns where
// the cursor ended and how many sounds were played
struct HeldRun {
    int selected = -1;
    size_t sounds = 0;
};

HeldRun holdFor(MaybeGui &g, int rows, int from, Button dir, unsigned int until) {
    Side side(*g.gui);
    unsigned int now = 0;
    bool released = false;
    side.ctx.clock = [&]() {
        now += 2;
        if (now >= until && !released) {
            released = true;
            g.gui->input().inject(dpad(false, dir)); // the release: pending, the hold ends
        }
        return now;
    };
    Quiet list(*g.gui, side.ctx, rows);
    list.selected = from;
    g.gui->input().inject(dpad(true, dir));
    Event polled;
    REQUIRE(g.gui->input().poll(polled));
    list.handle(polled);
    REQUIRE(g.gui->input().poll(polled)); // the release
    list.handle(polled);
    return {list.selected, side.sounds.size()};
}
} // namespace

TEST_CASE("List::holdRows: a held Down stops at the last row, a held Up at the first - the repeats never wrap") {
    MaybeGui g;
    if (!g.available())
        return;
    // 6 rows from row 3: the press 4, the first repeat 5, and the repeats after that find the end (1 s held)
    HeldRun down = holdFor(g, 6, 3, Button::DpadDown, 1000);
    CHECK(down.selected == 5);
    CHECK(down.sounds == 2); // the press and the one repeat that moved; the others at the end are silent
    HeldRun up = holdFor(g, 6, 2, Button::DpadUp, 1000);
    CHECK(up.selected == 0);
    CHECK(up.sounds == 2);
}

TEST_CASE("List::holdRows: a single press at the last row wraps to the first, and at the first to the last") {
    MaybeGui g;
    if (!g.available())
        return;
    // released at once (before the first repeat is due): only the press
    HeldRun down = holdFor(g, 6, 5, Button::DpadDown, 10);
    CHECK(down.selected == 0);
    HeldRun up = holdFor(g, 6, 0, Button::DpadUp, 10);
    CHECK(up.selected == 5);
}
