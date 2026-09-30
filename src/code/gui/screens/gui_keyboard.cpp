//
// GuiKeyboard - see the header. The pages, the keys, the editing, the drawing and the loop are ab_gui's
// abgui::Keyboard (docs/ab-gui-plan.md, G3n); this class is it as a classic screen (G3z).
//
#include "gui_keyboard.h"

#include "../gui.h"

#include <algorithm>

using namespace std;

//*******************************
// GuiKeyboard::pageName
//*******************************
// the same "what R1 leads to" as pageKeyLabel(), in words - the footer's R1 hint (K2, UIREV-21) used to read
// "Symbols" no matter which page was showing; this names the page that follows the current one.
string GuiKeyboard::pageName(int page) {
    switch (max(0, min(Pages - 1, page))) {
    case 0:
        return _("Symbols");
    case 1:
        return _("Accented letters");
    case 2:
        return _("More accents");
    default:
        return _("Letters");
    }
}
