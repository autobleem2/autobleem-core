#pragma once
//
// HoldRepeat and DpadHold live in ab_gui since G3f (<ab_gui/hold_repeat.h>, namespace abgui); these are the names
// every screen and extension already uses, unchanged - the pace, the API and the layout are the same.
// Header-only on purpose - an extension uses it without a new export from the launcher.
//
#include <ab_gui/hold_repeat.h>

using HoldRepeat = abgui::HoldRepeat;
using DpadHold = abgui::DpadHold;
