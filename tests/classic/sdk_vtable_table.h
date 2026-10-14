//
// The SDK vtable table: for each polymorphic class an extension subclasses or calls, a hash of its declaration (bases)
// and its virtual / override / final lines (test_sdk_vtables.cpp says what is read). AB_SDK_ABI 11.
// A mismatch means the class's vtable or its bases changed: bump AB_SDK_ABI, add a history line in gui/extension.h,
// then copy the printed hashes here. Never update the table alone.
//
#pragma once

// X(header relative to the core's root, class, hash)
#define AB_SDK_VTABLE_TABLE(X)                                                                                         \
    X("src/code/gui/extension.h", "ExtensionHost", "62408400b04ac59")                                                  \
    X("src/code/gui/extension.h", "Extension", "520edac33d2a84cb")                                                     \
    X("src/code/gui/gui_screen.h", "ClassicScreen", "9f0aac166d0171ed")                                                \
    X("src/code/gui/gui_screen.h", "GuiScreen", "d104612355ec88a4")                                                    \
    X("src/code/gui/screens/gui_action_menu.h", "GuiActionMenu", "90f67e273a3cf9b9")                                   \
    X("src/code/gui/screens/gui_confirm.h", "GuiConfirm", "d0ed81322c025222")                                          \
    X("src/code/gui/screens/gui_text_page.h", "GuiTextPage", "3ff98ff2f4702086")                                       \
    X("src/code/gui/screens/gui_keyboard.h", "GuiKeyboard", "4ae6dc7100238aa")                                         \
    X("src/code/gui/screens/gui_facts_page.h", "GuiFactsPage", "fb24257d847ebf72")                                     \
    X("src/code/gui/menus/gui_menu_base.h", "GuiMenuBase", "dd5a9b64b798a272")                                         \
    X("ab_gui/include/ab_gui/screen.h", "Screen", "85dc4905d96c1865")                                                  \
    X("ab_gui/include/ab_gui/action_menu.h", "ActionMenu", "49b733339875eb70")                                         \
    X("ab_gui/include/ab_gui/confirm.h", "Confirm", "f267ae2b4e499d73")                                                \
    X("ab_gui/include/ab_gui/text_page.h", "TextPage", "873ddba45868d975")                                             \
    X("ab_gui/include/ab_gui/keyboard.h", "Keyboard", "bd8f6f4ba87705f4")                                              \
    X("ab_gui/include/ab_gui/facts_page.h", "FactsPage", "a20960d5ac890139")                                           \
    X("ab_gui/include/ab_gui/list.h", "List", "a28a1ab671f6ffef")
