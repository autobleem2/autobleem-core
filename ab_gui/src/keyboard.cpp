// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui::Keyboard: the on-screen keyboard. See the header.
//
#include <ab_gui/keyboard.h>

#include <ab_gui/panel.h>

#include <algorithm>

using namespace std;
using ableem::Event;
using ableem::Key;

namespace abgui {

namespace {

// a page: four rows of ten keys, lower case and shifted (the symbols page is the same both ways)
struct PageRows {
    const char *lower[Keyboard::CharRows][Keyboard::Columns];
    const char *upper[Keyboard::CharRows][Keyboard::Columns];
};

const PageRows PagesTable[Keyboard::Pages] = {
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

const int FunctionRow = Keyboard::CharRows;

} // namespace

const int Keyboard::Columns;
const int Keyboard::CharRows;
const int Keyboard::Pages;

//*******************************
// Keyboard::keyAt / pageKeyLabel / pageName
//*******************************
Keyboard::KeyCap Keyboard::keyAt(int page, int row, int column, bool shifted) {
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

string Keyboard::pageKeyLabel(int page) {
    static const char *const next[Pages] = {"?123", "àé", "čş", "abc"};
    return next[max(0, min(Pages - 1, page))];
}

// the same "what R1 leads to" as pageKeyLabel(), in words: the page that follows the current one
string Keyboard::pageName(int page) {
    switch (max(0, min(Pages - 1, page))) {
    case 0:
        return "Symbols";
    case 1:
        return "Accented letters";
    case 2:
        return "More accents";
    default:
        return "Letters";
    }
}

//*******************************
// Keyboard::previousChar / nextChar
//*******************************
size_t Keyboard::previousChar(const string &text, size_t at) {
    if (at == 0)
        return 0;
    size_t i = min(at, text.size()) - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
        i--; // a continuation byte: back to the character's first
    return i;
}

size_t Keyboard::nextChar(const string &text, size_t at) {
    if (at >= text.size())
        return text.size();
    size_t i = at + 1;
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
        i++;
    return i;
}

//*******************************
// Keyboard::inserted / backspaced / deletedForward
//*******************************
Keyboard::Edit Keyboard::inserted(const Edit &edit, const string &text) {
    Edit out = edit;
    if (text.empty())
        return out;
    out.text.insert(out.cursor, text);
    out.cursor += text.size();
    return out;
}

Keyboard::Edit Keyboard::backspaced(const Edit &edit) {
    Edit out = edit;
    if (out.cursor == 0)
        return out;
    const size_t from = previousChar(out.text, out.cursor);
    out.text.erase(from, out.cursor - from);
    out.cursor = from;
    return out;
}

Keyboard::Edit Keyboard::deletedForward(const Edit &edit) {
    Edit out = edit;
    if (out.cursor >= out.text.size())
        return out;
    out.text.erase(out.cursor, nextChar(out.text, out.cursor) - out.cursor);
    return out;
}

//*******************************
// Keyboard::shown / caretIn
//*******************************
string Keyboard::shown(const string &text, bool asterisks) {
    if (!asterisks)
        return text;
    string out;
    for (size_t i = 0; i < text.size(); i = nextChar(text, i))
        out += '*';
    return out;
}

size_t Keyboard::caretIn(const string &text, size_t cursor, bool asterisks) {
    if (!asterisks)
        return cursor;
    size_t in = 0;
    for (size_t i = 0; i < cursor; i = nextChar(text, i))
        in++;
    return in;
}

//*******************************
// Keyboard::moved / shiftAfter / nextPage
//*******************************
Keyboard::Selection Keyboard::moved(int page, const Selection &at, int dx, int dy) {
    Selection s = at;
    if (dy != 0) {
        s.row = (s.row + dy + CharRows + 1) % (CharRows + 1);
        if (s.row == FunctionRow)
            s.column = keyAt(page, s.row, s.column, false).firstColumn;
    }
    if (dx != 0) {
        if (s.row == FunctionRow) {
            const KeyCap cap = keyAt(page, s.row, s.column, false);
            s.column = dx > 0 ? cap.firstColumn + cap.span : cap.firstColumn - 1;
            s.column = (s.column + Columns) % Columns;
            s.column = keyAt(page, s.row, s.column, false).firstColumn;
        } else {
            s.column = (s.column + dx + Columns) % Columns;
        }
    }
    return s;
}

Keyboard::Shift Keyboard::shiftAfter(Shift shift) {
    return shift == Shift::Off ? Shift::Once : shift == Shift::Once ? Shift::Lock : Shift::Off;
}

int Keyboard::nextPage(int page) {
    return (page + 1) % Pages;
}

//*******************************
// Keyboard::init
//*******************************
void Keyboard::init() {
    cursorIndex = result.size(); // the cursor starts at the end of the text
}

//*******************************
// editing
//*******************************
void Keyboard::type(const string &text) {
    if (text.empty())
        return;
    const Edit edit = inserted(Edit{result, cursorIndex}, text);
    result = edit.text;
    cursorIndex = edit.cursor;
    if (shift == Shift::Once)
        shift = Shift::Off;
}

void Keyboard::backspace() {
    const Edit edit = backspaced(Edit{result, cursorIndex});
    result = edit.text;
    cursorIndex = edit.cursor;
}

void Keyboard::deleteForward() {
    const Edit edit = deletedForward(Edit{result, cursorIndex});
    result = edit.text;
    cursorIndex = edit.cursor;
}

void Keyboard::nextShift() {
    shift = shiftAfter(shift);
}

void Keyboard::confirm() {
    cancelled = false;
    menuVisible = false;
}

void Keyboard::cancel() {
    cancelled = true;
    menuVisible = false;
}

void Keyboard::press() {
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
        page = nextPage(page);
        break;
    case KeyKind::Backspace:
        backspace();
        break;
    case KeyKind::Done:
        confirm();
        break;
    }
}

void Keyboard::moveSelection(int dx, int dy) {
    Selection at;
    at.row = row;
    at.column = column;
    at = moved(page, at, dx, dy);
    row = at.row;
    column = at.column;
}

//*******************************
// Keyboard::draw
//*******************************
void Keyboard::drawKey(const Style &style, const ableem::Rect &key, const KeyCap &cap, bool selected) {
    ableem::Renderer &renderer = ctx.renderer();
    const bool lit = cap.kind == KeyKind::Shift && shift != Shift::Off;
    const KeyState state = selected ? KeyState::Selected : lit ? KeyState::Lit : KeyState::Normal;
    style.key(ctx, key, state, cap.kind != KeyKind::Char);
    // every key's label in the theme's text colour (K1, UIREV-21): the function row's words used to draw in
    // the dimmer secondary colour and read as barely-there next to the letters - only the key's own tile
    // (above) stays a shade darker, as on a phone's keyboard
    const ableem::Color ink = style.text;
    const int cx = key.x + key.w / 2, cy = key.y + key.h / 2;
    // drawn as it is: "|" and "@" are keys here, not the text renderer's markers
    auto label = [&](const string &text, FontRole role) {
        const ableem::Font &font = ctx.font(role);
        font.drawColor(renderer, cx - font.width(text) / 2, cy - font.lineHeight() / 2, ink, text);
    };
    renderer.setDrawColor(ink);
    switch (cap.kind) {
    case KeyKind::Char:
        label(cap.text, FontRole::Row);
        break;
    case KeyKind::Space:
        label(ctx.translate("Space"), FontRole::RowSmall);
        break;
    case KeyKind::Done:
        label(ctx.translate("Confirm"), FontRole::RowSmall);
        break;
    case KeyKind::Page:
        label(pageKeyLabel(page), FontRole::RowSmall);
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

void Keyboard::draw() {
    ctx.drawBackdrop();
    const Panel panel = Panel::full(ctx);
    const Style &style = panel.style();
    ableem::Renderer &renderer = ctx.renderer();
    panel.sheet(ctx);
    const int yoffset = panel.header(ctx, label);
    const ableem::Rect content = panel.content();
    const int rowInset = Style::DefaultRowInset;

    // the text field: a band across the panel with the text in the launcher's medium font and a caret
    const string text = shown(result, displayAsterisksInstead);
    const size_t caret = caretIn(result, cursorIndex, displayAsterisksInstead);
    const ableem::Font &fieldFont = ctx.font(FontRole::Row);
    const int fieldH = 48;
    ableem::Rect field(content.x + rowInset, yoffset + 6, content.w - 2 * rowInset, fieldH);
    style.field(ctx, field);
    // a long text scrolls so the caret stays in the field
    const int fieldInner = field.w - 32;
    // the text drawn as it is (a "|" or "@" typed is text, not a marker), scrolled to keep the caret in view
    const int before = caret > 0 ? fieldFont.width(text.substr(0, caret)) : 0;
    const int scroll = max(0, before - fieldInner);
    const int textX = field.x + 16 - scroll;
    const int textY = field.y + (fieldH - fieldFont.lineHeight()) / 2;
    fieldFont.drawColor(renderer, textX, textY, style.text, text);
    if ((ctx.ticks() / 500) % 2 == 0) {
        style.caret(renderer, textX + before, textY + 2, fieldFont.lineHeight() - 4);
    }

    // the keys: four rows of the page and the function row, centred in what is left
    const int gridTop = field.y + fieldH + 20;
    const int gridBottom = content.y + content.h - 8;
    const int gap = 8, rows = CharRows + 1;
    const int keyW = min(96, (content.w - 2 * rowInset - gap * (Columns - 1)) / Columns);
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
            drawKey(style, key, cap, selected);
            x += cap.span;
        }

    panel.footer(ctx, "|@X| " + ctx.translate("Select") + "  |@T| " + ctx.translate("Backspace") + "  |@S| " +
                          ctx.translate("Space") + "  |@L1| " + ctx.translate("Shift") + "  |@R1| " +
                          ctx.translate(pageName(page)) + "  |@L2/R2| " + ctx.translate("Move cursor") +
                          "  |@Start| " + ctx.translate("Confirm") + "  |@O| " + ctx.translate("Cancel") + " |");
}

//*******************************
// Keyboard::loop
//*******************************
void Keyboard::loop() {
    // letters typed on a keyboard are letters here, and Esc cancels - both put back when the keyboard goes
    ableem::Input &input = gui.input();
    const bool keyboardAsPad = input.keyboardAsPad();
    const bool rawKeyboard = input.rawKeyboard();
    input.setKeyboardAsPad(false);
    input.setRawKeyboard(true);

    menuVisible = true;
    input.setFrameNeed(ableem::Input::FrameNeed::Idle); // nothing moves between presses
    while (menuVisible) {
        if (input.frameDue())
            render();
        Event e;
        while (menuVisible && input.poll(e)) {
            if (e.type == Event::Type::Quit) {
                cancel();
                continue;
            }
            handle(e);
        }
    }
    input.setKeyboardAsPad(keyboardAsPad);
    input.setRawKeyboard(rawKeyboard);
}

//*******************************
// Keyboard::onAction / onUnmapped
//*******************************
// the pad by its action (the d-pad by its live state), the keys as keys; a button plays the Cursor sound whatever it is
void Keyboard::onAction(const ActionEvent &action) {
    const Event &e = action.event;
    switch (e.type) {
    case Event::Type::ButtonDown:
        buttonDown(action.action);
        break;
    case Event::Type::DpadDown:
        dpadDown();
        break;
    case Event::Type::KeyDown:
        keyDown(e.key);
        break;
    default:
        break;
    }
}

void Keyboard::onUnmapped(const Event &event) {
    switch (event.type) {
    case Event::Type::TextInput:
        ctx.play(UiSound::Cursor);
        type(event.text);
        break;
    case Event::Type::ButtonDown:
        buttonDown(Action::None);
        break;
    case Event::Type::DpadDown:
        dpadDown();
        break;
    case Event::Type::KeyDown:
        keyDown(event.key);
        break;
    default:
        break;
    }
}

void Keyboard::keyDown(Key key) {
    if (key == Key::Left)
        cursorIndex = previousChar(result, cursorIndex);
    else if (key == Key::Right)
        cursorIndex = nextChar(result, cursorIndex);
    else if (key == Key::Home)
        cursorIndex = 0;
    else if (key == Key::End)
        cursorIndex = result.size();
    else if (key == Key::Backspace)
        backspace();
    else if (key == Key::Delete)
        deleteForward();
    else if (key == Key::Return)
        confirm();
    else if (key == Key::Escape)
        cancel();
}

void Keyboard::buttonDown(Action action) {
    ctx.play(UiSound::Cursor);
    switch (action) {
    case Action::Confirm:
        press();
        break;
    case Action::Option:
        backspace();
        break;
    case Action::Extra:
        type(" ");
        break;
    case Action::PrevTab:
    case Action::First:
        nextShift();
        break;
    case Action::NextTab:
    case Action::Last:
        page = nextPage(page);
        break;
    case Action::PageUp:
        cursorIndex = previousChar(result, cursorIndex);
        break;
    case Action::PageDown:
        cursorIndex = nextChar(result, cursorIndex);
        break;
    case Action::Menu:
        confirm();
        break;
    case Action::Back:
        ctx.play(UiSound::Cancel);
        cancel();
        break;
    default:
        break;
    }
}

void Keyboard::dpadDown() {
    ctx.play(UiSound::Cursor);
    ableem::Input &input = gui.input();
    if (input.dpadUp())
        moveSelection(0, -1);
    else if (input.dpadDown())
        moveSelection(0, 1);
    else if (input.dpadLeft())
        moveSelection(-1, 0);
    else if (input.dpadRight())
        moveSelection(1, 0);
}

} // namespace abgui
