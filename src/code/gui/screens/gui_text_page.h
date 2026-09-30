//
// GuiTextPage: a titled page of static text in the shared panel look - what a tool's instructions or an
// info box are. Longer than the panel, it scrolls: Up/Down a line, L2/R2 a page. Circle (or Escape) goes
// back; the caller sets `title` and `lines` and calls show().
//
#pragma once

#include "../gui_screen.h"

#include <string>
#include <vector>

//********************
// GuiTextPage
//********************
class GuiTextPage : public GuiScreen {
public:
    using GuiScreen::GuiScreen;

    void render() override;
    void loop() override;

    std::string title;              // the header
    std::vector<std::string> lines; // one row each, left aligned, wrapped to the panel; "" is a blank row
    bool centred = false;           // centre every line instead

    // how a line is laid out: its leading spaces indent all of it, and a numbered item's marker ("1. ") hangs
    // in front of the text, whose wrapped rows start under the text's first letter
    struct Item {
        size_t indent = 0;  // leading spaces
        std::string marker; // "12. ", "" when the line is not a numbered item
        std::string text;   // the rest
    };
    static Item splitItem(const std::string &line);

private:
    void draw(); // what render() puts between the stack's clear and present

    int firstLine = 0;     // the first line shown
    int lastLineShown = 0; // one past the last line render() fitted in
    int rowsThatFit = 1;   // rows of the font in the content rect - a page
};
