//
// GuiKeyboard - see the header.
//
#include "gui_keyboard.h"

#include "../gui.h"

#include <algorithm>

using namespace std;

namespace {

// a page: four rows of ten keys, lower case and shifted (the symbols page is the same both ways)
struct PageRows {
    const char *lower[GuiKeyboard::CharRows][GuiKeyboard::Columns];
    const char *upper[GuiKeyboard::CharRows][GuiKeyboard::Columns];
};

const PageRows PagesTable[GuiKeyboard::Pages] = {
    // letters
    {{{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
      {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
      {"a", "s", "d", "f", "g", "h", "j", "k", "l", "'"},
      {"z", "x", "c", "v", "b", "n", "m", ",", ".", "-"}},
     {{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
      {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
      {"A", "S", "D", "F", "G", "H", "J", "K", "L", "\""},
      {"Z", "X", "C", "V", "B", "N", "M", ";", ":", "_"}}},
    // symbols: what URLs, paths and passwords need
    {{{"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
      {"/", "\\", ":", ";", "?", "=", "+", "-", "_", "~"},
      {"\"", "'", "`", "|", "<", ">", "[", "]", "{", "}"},
      {".", ",", "€", "£", "¥", "¢", "§", "°", "¿", "¡"}},
     {{"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
      {"/", "\\", ":", ";", "?", "=", "+", "-", "_", "~"},
      {"\"", "'", "`", "|", "<", ">", "[", "]", "{", "}"},
      {".", ",", "€", "£", "¥", "¢", "§", "°", "¿", "¡"}}},
    // accents: the Western and Central European letters
    {{{"à", "á", "â", "ä", "ã", "å", "æ", "ç", "è", "é"},
      {"ê", "ë", "ì", "í", "î", "ï", "ñ", "ò", "ó", "ô"},
      {"ö", "õ", "ø", "ù", "ú", "û", "ü", "ý", "ÿ", "ß"},
      {"ą", "ć", "ę", "ł", "ń", "ś", "ź", "ż", "œ", "ð"}},
     {{"À", "Á", "Â", "Ä", "Ã", "Å", "Æ", "Ç", "È", "É"},
      {"Ê", "Ë", "Ì", "Í", "Î", "Ï", "Ñ", "Ò", "Ó", "Ô"},
      {"Ö", "Õ", "Ø", "Ù", "Ú", "Û", "Ü", "Ý", "Ÿ", "ß"},
      {"Ą", "Ć", "Ę", "Ł", "Ń", "Ś", "Ź", "Ż", "Œ", "Ð"}}},
    // more: Czech/Slovak, Turkish, Romanian, Hungarian, Nordic
    {{{"č", "ď", "ě", "ň", "ř", "š", "ť", "ů", "ž", "ľ"},
      {"ĺ", "ŕ", "ş", "ğ", "ı", "ș", "ț", "ă", "ő", "ű"},
      {"þ", "ā", "ē", "ī", "ō", "ū", "ė", "į", "ų", "ģ"},
      {"ķ", "ļ", "ņ", "«", "»", "„", "“", "”", "…", "·"}},
     {{"Č", "Ď", "Ě", "Ň", "Ř", "Š", "Ť", "Ů", "Ž", "Ľ"},
      {"Ĺ", "Ŕ", "Ş", "Ğ", "I", "Ș", "Ț", "Ă", "Ő", "Ű"},
      {"Þ", "Ā", "Ē", "Ī", "Ō", "Ū", "Ė", "Į", "Ų", "Ģ"},
      {"Ķ", "Ļ", "Ņ", "«", "»", "„", "“", "”", "…", "·"}}},
};

const int FunctionRow = GuiKeyboard::CharRows;

} // namespace

//*******************************
// GuiKeyboard::keyAt / pageKeyLabel
//*******************************
GuiKeyboard::KeyCap GuiKeyboard::keyAt(int page, int row, int column, bool shifted) {
    KeyCap cap;
    if (row < FunctionRow) {
        const PageRows &p = PagesTable[max(0, min(Pages - 1, page))];
        cap.text = shifted ? p.upper[row][column] : p.lower[row][column];
        cap.firstColumn = column;
        return cap;
    }
    // Shift 0-1, Page 2-3, Space 4-6, Backspace 7, Done 8-9
    static const struct {
        KeyKind kind;
        int first, span;
    } function[] = {{KeyKind::Shift, 0, 2},
                    {KeyKind::Page, 2, 2},
                    {KeyKind::Space, 4, 3},
                    {KeyKind::Backspace, 7, 1},
                    {KeyKind::Done, 8, 2}};
    for (const auto &f : function)
        if (column >= f.first && column < f.first + f.span) {
            cap.kind = f.kind;
            cap.firstColumn = f.first;
            cap.span = f.span;
            cap.text = f.kind == KeyKind::Space ? " " : "";
        }
    return cap;
}

string GuiKeyboard::pageKeyLabel(int page) {
    static const char *const next[Pages] = {"?123", "àé", "čş", "abc"};
    return next[max(0, min(Pages - 1, page))];
}

//*******************************
// GuiKeyboard::previousChar / nextChar
//*******************************
size_t GuiKeyboard::previousChar(const string &text, size_t at) {
    if (at == 0)
        return 0;
    size_t i = min(at, text.size()) - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
        i--; // a continuation byte: back to the character's first
    return i;
}

size_t GuiKeyboard::nextChar(const string &text, size_t at) {
    if (at >= text.size())
        return text.size();
    size_t i = at + 1;
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
        i++;
    return i;
}

//*******************************
// GuiKeyboard::init
//*******************************
void GuiKeyboard::init() {
    gui = Gui::getInstance();
    cursorIndex = result.size(); // the cursor starts at the end of the text
}

//*******************************
// GuiKeyboard::editing
//*******************************
void GuiKeyboard::type(const string &text) {
    if (text.empty())
        return;
    result.insert(cursorIndex, text);
    cursorIndex += text.size();
    if (shift == Shift::Once)
        shift = Shift::Off;
}

void GuiKeyboard::backspace() {
    if (cursorIndex == 0)
        return;
    const size_t from = previousChar(result, cursorIndex);
    result.erase(from, cursorIndex - from);
    cursorIndex = from;
}

void GuiKeyboard::deleteForward() {
    if (cursorIndex >= result.size())
        return;
    result.erase(cursorIndex, nextChar(result, cursorIndex) - cursorIndex);
}

void GuiKeyboard::nextShift() {
    shift = shift == Shift::Off ? Shift::Once : shift == Shift::Once ? Shift::Lock : Shift::Off;
}

void GuiKeyboard::confirm() {
    cancelled = false;
    menuVisible = false;
}

void GuiKeyboard::cancel() {
    cancelled = true;
    menuVisible = false;
}

void GuiKeyboard::press() {
    const KeyCap cap = keyAt(page, row, column, shift != Shift::Off);
    switch (cap.kind) {
    case KeyKind::Char:
    case KeyKind::Space:
        type(cap.text);
        break;
    case KeyKind::Shift:
        nextShift();
        break;
    case KeyKind::Page:
        page = (page + 1) % Pages;
        break;
    case KeyKind::Backspace:
        backspace();
        break;
    case KeyKind::Done:
        confirm();
        break;
    }
}

//*******************************
// GuiKeyboard::moveSelection
//*******************************
// round the grid both ways; on the function row a step is a key, and going up or down keeps the column
void GuiKeyboard::moveSelection(int dx, int dy) {
    if (dy != 0) {
        row = (row + dy + CharRows + 1) % (CharRows + 1);
        if (row == FunctionRow)
            column = keyAt(page, row, column, false).firstColumn;
    }
    if (dx != 0) {
        if (row == FunctionRow) {
            const KeyCap cap = keyAt(page, row, column, false);
            column = dx > 0 ? cap.firstColumn + cap.span : cap.firstColumn - 1;
            column = (column + Columns) % Columns;
            column = keyAt(page, row, column, false).firstColumn;
        } else {
            column = (column + dx + Columns) % Columns;
        }
    }
}

//*******************************
// GuiKeyboard::render
//*******************************
void GuiKeyboard::drawKey(const ableem::Rect &key, const KeyCap &cap, bool selected) {
    PanelStyle style = gui->panelStyle();
    renderer.setBlendMode(ableem::BlendMode::Blend);
    const bool lit = cap.kind == KeyKind::Shift && shift != Shift::Off;
    if (selected) {
        renderer.setDrawColor(ableem::Color(style.text.r, style.text.g, style.text.b, 60));
        renderer.fillRect(key);
        renderer.setDrawColor(style.text);
        renderer.drawRect(key);
    } else {
        // the function keys a shade darker than the letters, as on a phone's keyboard
        const unsigned char fill = cap.kind == KeyKind::Char ? 18 : 8;
        renderer.setDrawColor(ableem::Color(255, 255, 255, lit ? 50 : fill));
        renderer.fillRect(key);
        renderer.setDrawColor(ableem::Color(style.secondary.r, style.secondary.g, style.secondary.b, 110));
        renderer.drawRect(key);
    }
    // the letters in the text colour (readable at a glance), the function keys' words in the secondary one
    const ableem::Color ink = selected || lit || cap.kind == KeyKind::Char ? style.text : style.secondary;
    const int cx = key.x + key.w / 2, cy = key.y + key.h / 2;
    Fonts &fonts = gui->assets().themeFonts;
    // drawn as it is: "|" and "@" are keys here, not the text renderer's markers
    auto label = [&](const string &text, FontEnum f) {
        const ableem::Font &font = fonts[f];
        font.drawColor(renderer, cx - font.width(text) / 2, cy - font.lineHeight() / 2, ink, text);
    };
    renderer.setDrawColor(ink);
    switch (cap.kind) {
    case KeyKind::Char:
        label(cap.text, FONT_22_MED);
        break;
    case KeyKind::Space:
        label(_("Space"), FONT_20_BOLD);
        break;
    case KeyKind::Done:
        label(_("Confirm"), FONT_20_BOLD);
        break;
    case KeyKind::Page:
        label(pageKeyLabel(page), FONT_20_BOLD);
        break;
    case KeyKind::Shift: {
        // an arrow up; a bar under it for caps lock
        for (int i = 0; i < 9; i++)
            renderer.fillRect(ableem::Rect(cx - i, cy - 12 + i, 2 * i + 1, 1));
        renderer.fillRect(ableem::Rect(cx - 4, cy - 3, 9, 9));
        if (shift == Shift::Lock)
            renderer.fillRect(ableem::Rect(cx - 8, cy + 9, 17, 3));
        break;
    }
    case KeyKind::Backspace: {
        // an arrow left
        for (int i = 0; i < 9; i++)
            renderer.fillRect(ableem::Rect(cx - 12 + i, cy - i, 1, 2 * i + 1));
        renderer.fillRect(ableem::Rect(cx - 3, cy - 3, 15, 7));
        break;
    }
    }
}

void GuiKeyboard::render() {
    gui->renderBackground();
    gui->renderTextBar();
    int yoffset = gui->renderHeader(label);
    PanelStyle style = gui->panelStyle();
    const ableem::Rect content = gui->classicContent();
    Fonts &fonts = gui->assets().themeFonts;

    // the text field: a band across the panel with the text in the launcher's medium font and a caret
    string shown = result;
    if (displayAsterisksInstead) {
        shown.clear();
        for (size_t i = 0; i < result.size(); i = nextChar(result, i))
            shown += '*';
    }
    size_t caretIn = cursorIndex;
    if (displayAsterisksInstead) {
        caretIn = 0;
        for (size_t i = 0; i < cursorIndex; i = nextChar(result, i))
            caretIn++;
    }
    const ableem::Font &fieldFont = fonts[FONT_22_MED];
    const int fieldH = 48;
    ableem::Rect field(content.x + PanelStyle::RowInset, yoffset + 6, content.w - 2 * PanelStyle::RowInset, fieldH);
    renderer.setBlendMode(ableem::BlendMode::Blend);
    renderer.setDrawColor(ableem::Color(255, 255, 255, 14));
    renderer.fillRect(field);
    renderer.setDrawColor(ableem::Color(style.secondary.r, style.secondary.g, style.secondary.b, 160));
    renderer.drawRect(field);
    // a long text scrolls so the caret stays in the field
    const int fieldInner = field.w - 32;
    // the text drawn as it is (a "|" or "@" typed is text, not a marker), scrolled to keep the caret in view
    const int before = caretIn > 0 ? fieldFont.width(shown.substr(0, caretIn)) : 0;
    const int scroll = max(0, before - fieldInner);
    const int textX = field.x + 16 - scroll;
    const int textY = field.y + (fieldH - fieldFont.lineHeight()) / 2;
    fieldFont.drawColor(renderer, textX, textY, style.text, shown);
    if ((gui->platform().ticks() / 500) % 2 == 0) {
        renderer.setDrawColor(style.text);
        renderer.fillRect(ableem::Rect(textX + before, textY + 2, 2, fieldFont.lineHeight() - 4));
    }

    // the keys: four rows of the page and the function row, centred in what is left
    const int gridTop = field.y + fieldH + 20;
    const int gridBottom = content.y + content.h - 8;
    const int gap = 8, rows = CharRows + 1;
    const int keyW = min(96, (content.w - 2 * PanelStyle::RowInset - gap * (Columns - 1)) / Columns);
    const int keyH = min(64, (gridBottom - gridTop - gap * (rows - 1)) / rows);
    const int gridW = keyW * Columns + gap * (Columns - 1);
    const int gridH = keyH * rows + gap * (rows - 1);
    const int gridX = content.x + (content.w - gridW) / 2;
    const int gridY = gridTop + max(0, (gridBottom - gridTop - gridH) / 2);
    const bool shifted = shift != Shift::Off;
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < Columns;) {
            const KeyCap cap = keyAt(page, y, x, shifted);
            const ableem::Rect key(gridX + x * (keyW + gap), gridY + y * (keyH + gap),
                                   keyW * cap.span + gap * (cap.span - 1), keyH);
            const bool selected = row == y && keyAt(page, row, column, shifted).firstColumn == cap.firstColumn;
            drawKey(key, cap, selected);
            x += cap.span;
        }

    gui->renderStatus("|@X| " + _("Select") + "  |@T| " + _("Backspace") + "  |@S| " + _("Space") + "  |@L1| " +
                      _("Shift") + "  |@R1| " + _("Symbols") + "  |@L2|/|@R2| " + _("Move cursor") + "  |@Start| " +
                      _("Confirm") + "  |@O| " + _("Cancel") + " |");
    renderer.present();
}

//*******************************
// GuiKeyboard::loop
//*******************************
void GuiKeyboard::loop() {
    // letters typed on a keyboard are letters here, and Esc cancels - both put back when the keyboard goes
    ableem::Input &input = gui->input();
    const bool keyboardAsPad = input.keyboardAsPad();
    const bool rawKeyboard = input.rawKeyboard();
    input.setKeyboardAsPad(false);
    input.setRawKeyboard(true);

    menuVisible = true;
    while (menuVisible) {
        render();
        Event e;
        while (menuVisible && input.poll(e)) {
            switch (e.type) {
            case Event::Type::Quit:
                cancel();
                break;
            case Event::Type::TextInput:
                app.audio().cursor.play();
                type(e.text);
                break;
            case Event::Type::KeyDown:
                if (e.key == Key::Left)
                    cursorIndex = previousChar(result, cursorIndex);
                else if (e.key == Key::Right)
                    cursorIndex = nextChar(result, cursorIndex);
                else if (e.key == Key::Home)
                    cursorIndex = 0;
                else if (e.key == Key::End)
                    cursorIndex = result.size();
                else if (e.key == Key::Backspace)
                    backspace();
                else if (e.key == Key::Delete)
                    deleteForward();
                else if (e.key == Key::Return)
                    confirm();
                else if (e.key == Key::Escape)
                    cancel();
                break;
            case Event::Type::ButtonDown:
                app.audio().cursor.play();
                if (e.button == Button::Cross)
                    press();
                else if (e.button == Button::Triangle)
                    backspace();
                else if (e.button == Button::Square)
                    type(" ");
                else if (e.button == Button::L1)
                    nextShift();
                else if (e.button == Button::R1)
                    page = (page + 1) % Pages;
                else if (e.button == Button::L2)
                    cursorIndex = previousChar(result, cursorIndex);
                else if (e.button == Button::R2)
                    cursorIndex = nextChar(result, cursorIndex);
                else if (e.button == Button::Start)
                    confirm();
                else if (e.button == Button::Circle) {
                    app.audio().cancel.play();
                    cancel();
                }
                break;
            case Event::Type::DpadDown:
                app.audio().cursor.play();
                if (input.dpadUp())
                    moveSelection(0, -1);
                else if (input.dpadDown())
                    moveSelection(0, 1);
                else if (input.dpadLeft())
                    moveSelection(-1, 0);
                else if (input.dpadRight())
                    moveSelection(1, 0);
                break;
            default:
                break;
            }
        }
    }
    input.setKeyboardAsPad(keyboardAsPad);
    input.setRawKeyboard(rawKeyboard);
}
