# ab_gui - the AutoBleem User Interface Library (design, G1)

Status: **approved by the owner** (2026-09-30). Licence: **GPL-3.0-or-later** (the owner, hub `docs/decisions.md`).

## 1. What it is

A GUI library for **consoles and TV-like devices**, driven **mainly by a gamepad**, with the **keyboard as the
fallback** (and for typing). It is built from what the launcher already has - `PanelStyle`, the list menus, the
dialogs, the on-screen keyboard, the busy rule, the screen stack - pulled out of AutoBleem so that **other programs
can be built on it** later (the console tools, the Store, future Apps, other devices). It knows nothing of games,
themes as AutoBleem stores them, `config.ini` or the language files: the program hands it those.

Targets: everything with SDL2 that lib_ableem runs on - the PlayStation Classic (`autobleem_sdl` 2.0.18, Wayland,
720p, weak GPU), Raspberry Pi, the PC stick, Windows; later Linux ARM handhelds and the Steam Deck.

Not in scope (now): mouse and touch, a general animation framework, the carousel's 3D drawing.

## 2. Where it sits

```
lib_ableem   ableem_engine (no SDL)  +  ableem (SDL: Platform, Renderer, Texture, Font, Input, GuiBase, DebugDriver)
ab_gui       NEW - Style, painter primitives, actions + focus, widgets, screens      (no AutoBleem code)
ab_classic   AutoBleem glue: Gui, AppBase, Theme -> Style, ThemeAssets, About/splash, the extension runtime
ab_ui / ab_evoui / tools / extensions   the programs
```

- Lives in **autobleem-core**, next to lib_ableem: `ab_gui/` (sources), headers as `<ab_gui/...>`, its own CMake
  target `ab_gui` (links `ableem`), namespace `abgui`.
- C++14 (the console's gcc-6), clang-format/tidy rules as today.
- **Hard rule (the owner, 2026-09-30): no SDL outside lib_ableem.** ab_gui and everything built on it never include
  an SDL header, call an SDL function or use an SDL type; the `ableem` interface exposes no SDL type either. What
  is missing goes into lib_ableem first - so a GLES2 renderer can later replace the SDL one under the same interface.
- **No singletons.** One `abgui::Context` holds what every widget needs: the `Renderer` and `Input`, the fonts, the
  button glyphs, the UI sounds, the translator (a `std::function<std::string(const std::string&)>`), the `Style`.
  AutoBleem's `Gui` builds one and keeps it; another program builds its own.

## 3. Style (today's PanelStyle)

- **Colour roles** exactly as UIREV-29 made them: `text`, `secondary`, `hint`, `selection` and the roles `row`,
  `rowSelected`, `heading`, `value`, `description`, `footer`, `selectionBand`, `edge` (a role may name another).
- **Metrics**: header/footer/row heights, insets, margins, the selection bar - today's constants, as fields with
  today's values as defaults (so a style can change them).
- **Font roles**: title, row, row-small, hint, key - the program fills them (AutoBleem: its launcher pair and the
  theme's classic font); a CJK fallback font per role.
- **Frames** (G4): every primitive in 4. may draw a **9-slice image** from the style instead of its code-drawn
  shape - panel, selection, heading band, chip, key, field, progress track/fill, tab, tile, badge, toast. A frame is
  an image + its four insets + an optional glow margin; a style without it looks exactly as today. This is how the
  new look's cut corners, cyan rims and glows come in without code per shape.
- Loaded with `Style::fromJson(block)` from a plain JSON block; AutoBleem's `theme.json` fills it (its
  `launcher.colors` + a new `launcher.frames` in G4), so a theme keeps one file.

## 4. Painter primitives

Stateless drawing with a `Context` and a `Style`, all in logical units:
`dim`, `sheet`, `header`, `rule`, `vrule`, `selection`, `label` (heading band), `disabled`, `scrollMarker`,
`chip`/`buttons` (a glyph + label), `footer` (the hint line from the `|@X| Label` protocol, sorted, shortened to fit
- today's `footer_shorten`), `key` and `field` (the keyboard), `progress`, `spinner`, `box`, `tab`, text helpers
(fit, shorten, right-align). **Step 1 of the refactor already agreed** (the 7 missing ones: key, field, progress,
spinner, box, tab, vrule) lands here directly.

## 5. Input: actions, not buttons

- Screens react to **actions**: `Confirm`, `Back`, `Option` (Triangle), `Extra` (Square), `Menu` (Start),
  `View` (Select), `PrevTab`/`NextTab` (L1/R1), `PageUp`/`PageDown` (L2/R2), `Up/Down/Left/Right`, `First`/`Last`.
- An **ActionMap** turns pad buttons and keys into actions: the pad as today, the keyboard as the fallback
  (Enter/Esc/arrows/Tab/PgUp/PgDn/Home/End/F-keys), and a switch for the **Confirm/Back swap** (the Japanese and
  Nintendo layout). The existing `doCross_Pressed()`-style hooks stay as thin adapters until every screen moves.
- **Hold-repeat** is the one shared pace (`HoldRepeat`), the **busy rule** stays in `Input` (nothing leaks through
  a spinner), unchanged.

## 6. Focus and navigation (d-pad, no pointer)

- A **FocusGroup** knows its focusable items and moves the focus with the d-pad: along a list, across a grid or a
  row of tiles, between tabs and between a dialog's buttons - the nearest item in the pressed direction. Headings and
  disabled items are skipped by the group, not by each screen.
- First the **vertical list** (all today's lists), then row, grid, tabs, button row.

## 7. Widgets and screens

- **Widgets** (state + input + drawing through the primitives): `List` (today's `GuiMenuBase`: headings, paging,
  first/last, scroll markers, right-aligned values, the on/off switch), `ActionMenu` (Quick/System menu), `Confirm`,
  `Keyboard` (the on-screen keyboard with its pages and the USB keyboard alongside), `FactsPage`, `TextPage`,
  `Tabs`, `Toast`/notification, `Busy` (spinner over the dimmed screen), `ProgressDialog`.
- **Screens**: a `Screen` base and the **screen stack** (today's `ableem::GuiScreen` + AutoBleem's shim), a screen's
  loop reading actions.
- **Testing built in**: every screen reports itself to the DebugDriver (`screen`), every List/ActionMenu/Tabs its rows
  and cursor (`items`/`selected`), `Busy` its state (`busy`/`wait_ready`) - so every program on ab_gui can be driven by
  a script from day one, with no per-screen code.

## 7a. Transitions (the owner, 2026-09-30)

- Every screen declares an **in** and an **out** transition: `None`, `Fade` (through black - today's splash),
  `CrossFade` (dissolve old -> new), `Slide`/`Drop` (the new one comes in from top/bottom/side over the old one,
  ease-out, the old one dimming under it - dialogs and panels), `Pop` (95% -> 100% with a fade - confirms). The out
  transition defaults to the in one reversed. Durations and easing come from the style; an Options row turns
  animations off (slow devices, preference).
- **Screens draw, the stack presents.** A screen has `draw()` only; clearing and presenting belong to the screen
  stack (today every screen clears and presents itself - that is the one change G3 makes for this).
- **Both frames live in GPU textures**: the old screen's last frame is kept (the launcher already does it for the
  menus over the carousel, `captureNextFrame`); the new screen runs `init()` and draws its first frame into an
  off-screen target - while it loads, the old one stays on screen, and the transition starts only once the new one
  is ready. For 150-300 ms the stack composes the two (alpha, offset, scale), then hands input to the new screen. A
  screen that animates itself is drawn into its target every frame of the transition.
- On the console: no read-back from the GPU (slow there), two 720p textures (~7 MB), a two-texture compose per frame.
- A press during a transition finishes it at once and goes to the new screen - nothing lost, nothing to the old one.
- The DebugDriver counts a transition as busy, so `wait_ready` waits it out and no test grabs a half-way frame.
- The splash becomes a plain screen: one image, one label, in Fade, out Fade, a hold time - declared, no own loop.

## 7b. Element animations (the owner, 2026-09-30 - evoui needs them)

- A **Tween** animates one property of an element already on screen - position, size/scale, alpha, angle, colour -
  from/to over a duration with a delay and an easing (`easeOutCubic` as today, linear, in/out, a small overshoot);
  **loop** and **yoyo** for ambient motion (a glow pulse); a callback at the end. A **Timeline** runs tweens in
  sequence or in parallel. One frame clock (the platform's ticks) drives all of them.
- Frame need: while a tween runs the screen redraws every frame; when none runs (or only ambient ones) the old rule
  holds - redraw only on a change - so an idle screen costs nothing (the console's CPU).
- The DebugDriver: `wait_ready` waits until the non-ambient tweens are done (the carousel's scroll, a panel sliding
  in); ambient loops are marked as such and do not hold it (as Input's frame need already tells "ambient" apart).
- evoui's own motion - the carousel's scroll and cover moves, the meta panel's slide, the notification bubble, the
  fade-in overlay, the menu icons - moves onto Tweens in G5, so it keeps its look but loses its hand-written timers.

## 8. Button glyphs by pad

A **GlyphSet** per controller family - PlayStation, Xbox, Nintendo, generic, keyboard - picked from the pad in use
(`Input` knows its name/GUID; the keyboard's when the last input came from it). Footers and hints ask for the glyph
of an action, so the same screen shows the right icons on any pad. The program (or theme) supplies the images;
AutoBleem ships PlayStation's as today.

## 9. Resolution

- The **logical canvas stays 1280x720 units**; positions may be fractional. Drawing maps to output pixels at draw
  time (today's `Renderer::outputScale`), so text and shapes are already sharp at 1080p.
- **High-resolution images**: a style/theme may ship a 1080p (or `@2x`) version of any image or frame; it is drawn
  into the same logical rect - 1:1 on a 1080p screen, scaled down by the GPU on the console's 720p. No extra
  render pass, no off-screen buffer (the console's GPU read-back is slow).

## 10. What stays in ab_classic (AutoBleem)

`Gui` (becomes the holder of AutoBleem's `abgui::Context`), `AppBase`, `Theme`/`ThemeAssets` (build the `Style`
from `theme.json`), About and the surprise game, the splash, Hardware Information (a FactsPage with AutoBleem's
data), the extension runtime, the game-aware screens in ab_ui and everything in ab_evoui.

## 11. Steps

| Step | What | Visible change | Check |
|---|---|---|---|
| G1 | this document | - | the owner's OK |
| G2 | `ab_gui` target; `PanelStyle` -> `abgui::Style` + primitives, the 7 new primitives; every caller outside evoui draws through them (keyboard, busy/progress, About, splash, text page, detail pane, the Store's boxes/tabs/progress/toast, PSC-Bios' hold bar) | none | screenshot diff before/after, every screen, 2 themes, on the VM |
| G3 | widgets move: `List` from `GuiMenuBase`, `ActionMenu`, `Confirm`, `Keyboard`, `FactsPage`, `TextPage`, `Busy`; `Context` replaces the `Gui`/`AppBase` look-ups; actions + ActionMap under the old hooks | none | the same diff; one **ABI bump** at the end, the Store and PSC-Bios rebuilt once |
| G4 | frames (9-slice + hi-res images) in `Style`, `launcher.frames` in `theme.json`, `docs/theme-format.md` | only with a theme that sets frames (`ab2.0.0`) | default/ab2 diff unchanged; ab2.0.0 against its mockups |
| G5 | evoui's own pieces on the primitives (badges, Play, menu tiles, the hint bar, the banner/bubble, the cover glow) | only with such a theme | as G4 |
| later | FocusGroup beyond lists, GlyphSets, Confirm/Back swap | new options | per feature |

Every step is checked on the pcusb-test VM through the DebugDriver (screenshots before/after), then on a device
by the owner. UIREV-10/26/27/30/31 and BUG-30/31 are done inside these steps, not before.

### G3 sub-steps

G3 lands as small steps, each one core commit (+ the launcher's gitlink and its own callers), each built, run
through the core suites and checked by the **masked screenshot diff of the 58 screens against the baseline - 0
differ** before the next starts. The senior design comes first (a-g), the widget moves after it (h-n) are
mechanical and brief-sized, the ABI step (z) closes G3.

**The ABI rule for a-n.** An extension built for ABI 6 (the Store, PSC-Bios) keeps loading and working until z:
- A class an extension shares a layout with - holds by value, constructs, derives from, or reads through inline
  code - keeps its members, its virtuals and its inline bodies: `ableem::GuiScreen`, the classic `GuiScreen` shim,
  `Gui` (members may only be appended after `uiContext_`), `PanelStyle`, `TextRenderer`, `ThemeAssets`,
  `LauncherTheme`, `Input`/`Renderer`, and the widget classes the extensions use: `GuiConfirm`, `GuiKeyboard`,
  `GuiActionMenu`, `GuiFactsPage` (derived from), `GuiTextPage`. Their out-of-line bodies may change (forward to
  ab_gui); every symbol they declare keeps its signature and meaning.
- `abgui::Context` is used by no extension: it may grow, members appended at the end.
- The header-only templates (`GuiMenuBase`, `GuiStringMenu`, `GuiTwoColumnStringMenu`, `HoldRepeat`) are compiled
  into the extension, so their code may change - but whatever host symbol their new code calls must be exported:
  from the first step where header code calls `abgui::` directly (c), the launcher's `--whole-archive` list gets
  `$<TARGET_FILE:ab_gui>` too (today only ab_classic/ab_core/ableem/ableem_engine are there).
- A new virtual in a class an extension derives from, or a changed layout of one it holds, waits for z.

So the pattern for a move: the new `abgui::` class side by side, the in-tree callers switched to it, the old class
an adapter over it with its header untouched (or, where it cannot be, left as it is until z deletes it).

| Step | What moves | Files | ABI | Risk |
|---|---|---|---|---|
| **G3a** (done) | `Context` completes its non-drawing half: the `Input` and `Platform` (a three-argument constructor - both are GuiBase's and outlive the display's release), `ticks()`/`delay()` (a settable `clock` for tests, else the platform's), `play(UiSound)` over a `soundPlayer` (Cursor, Cancel, HomeUp, HomeDown, Resume). `Gui` builds it with its input/platform and wires the sounds to AppAudio. No caller yet. | `ab_gui/.../context.h`, `ab_gui/src/context.cpp`, `gui/gui.cpp`, `gui/gui.h` (comment), `tests/gui/test_ab_gui_context.cpp`, `tests/CMakeLists.txt` | none (Context appended) | none: nothing draws differently |
| **G3b** (senior, done) | `Context`: the backdrop and the classic panel's geometry, so a widget draws a full or compact panel without `Gui`. `Context` gains a `backdropDrawer` (`drawBackdrop()`, else a black clear) and a `panelProvider` (`panelRect()`: the full panel, else the canvas inset by the margin); `abgui::Panel` (a rect + a `Style`) has the geometry - `Panel::full(ctx)`, `Panel::compact(ctx, rows, font)` (and the pure `compactRect`), `content()`, `footer()`, `rowsThatFit(lineHeight)`/`rowsThatFit(ctx, font)`, the scroll markers' places - and the drawing - `sheet(ctx)` (dim + sheet), `header`, `footer(ctx, line)`, `scrollMarkers`. `Gui` wires the drawer to the theme's background and the provider to the theme's menu panel down to the status line's foot; `renderBackground`, `classicPanel/classicContent/classicFooter/classicRowsThatFit`, `setCompactPanel`, `renderTextBar/renderHeader/renderStatus/renderScrollMarkers` keep their signatures and forward. The compact panel stays `Gui`'s state (`compact_`/`compactPanel_`, which `TextRenderer`'s rows read as their panel) until the widgets draw their own rows (m2). No switch images: since 2026-09-29 a boolean's value is the text ON/OFF (`renderRowValue`), which moves with the List (m2). Test: `test_ab_gui_panel` - every number against a copy of the old `Gui` formulas, the shipped themes' full panel and the compact panels on 1280x720 and 1920x1080 canvases. | `ab_gui/.../panel.h`, `ab_gui/src/panel.cpp`, `context.h/.cpp`, `gui/gui.cpp`, `CMakeLists.txt`, `tests/gui/test_ab_gui_panel.cpp`, `tests/CMakeLists.txt` | none (Gui's methods kept, no Gui member added, Context appended) | low: every classic panel draws through the moved code - Options, editors, Game Manager, Memory Cards, Hardware Information, text page, keyboard, compact lists |
| **G3c** (senior, done) | **Screens draw, the stack presents.** `abgui::ScreenStack` (owned by `Gui`, appended after `uiContext_`; the Context holds a pointer, appended: `setStack`/`hasStack()`/`stack()`): `frame(draw)` = `clear()` in the current draw colour, the screen's drawing, `present()`; `frame(colour, draw)` sets the draw colour first (what a `setDrawColor` + `clear` did) - the one place a frame is presented, where 7a's transitions will later send a frame into an off-screen target instead. The frames go to a `ScreenStack::Display` (the renderer's clear/present, or a test's recorder). A frame started inside another's drawing (a busy tick from a load inside a draw) is a frame of its own - cleared, drawn, presented at once, as it always was; the outer presents at its end (`depth()` tells them apart); a drawing that throws is not presented. Every core screen's `render()` became `stack().frame([this]{ draw(); })` with a new non-virtual (private, so ABI-neutral) `draw()` holding what it drew between clear and present: `GuiMenuBase` (its `clearCompactPanel()` now at the end of `draw()`, just before the present), `GuiConfirm`, `GuiActionMenu`, `GuiFactsPage`, `GuiKeyboard`, `GuiTextPage`, `GuiAbout` (one frame choosing `draw()` or `renderSurprise()`), `GuiSplash` (cleared to black/0), and `Gui`'s own `drawBusyFrame`, `drawText`, `showSplashPicture`, `display` (the resume's black frame). `render()` stays the virtual `loop()` calls. The launcher's `--whole-archive` gained ab_gui. Test: `test_ab_gui_screen_stack` - the order, one present a frame, nesting, a throw (on a recording display), and on a headless GuiBase the frame count, the DebugDriver's frame copy and a `captureNextFrame()` backdrop. | `ab_gui/.../screen_stack.h`, `ab_gui/src/screen_stack.cpp`, `context.h`, `CMakeLists.txt`, `gui.h/.cpp`, `menus/gui_menu_base.h`, `screens/gui_{confirm,action_menu,facts_page,keyboard,text_page,about,splash}.{h,cpp}`, `tests/gui/test_ab_gui_screen_stack.cpp`, `tests/CMakeLists.txt`; launcher `CMakeLists.txt` | none: `render()` keeps its slot, the new `draw()`s are non-virtual (no layout or vtable change), Gui's member is appended; an ABI-6 extension's own inline `GuiMenuBase::render` still presents itself, which stays correct | medium: a screen whose `render()` did not start with `clear()` now does - Confirm, Keyboard, TextPage, About, `drawText` - but each of them began with `renderBackground()`, which clears to black itself, so the pixels and the capture's on-GPU path are what they were; the extra full clear is the only cost - check the compact dialogs over each screen (Confirm, the Quick/System menu) in the diff |
| **G3d** | The launcher's own screens through the stack (the same pattern): ab_ui's Options, game editor, RetroArch editor, Game Manager, select memcard; evoui's app start, button guide, Extensions, memory card manager, Processors, set picker, System menu, update (two). | launcher `gui/menus/gui_{options_menu,game_editor_menu,game_editor_ra_menu,game_manager_menu}.cpp`, `gui/screens/gui_select_memcard.cpp`, `evoui/screens/evoui_{app_start,btn_guide,extensions,mc_manager,processors,set_picker,system_menu,update}.cpp` | none (launcher only) | low; Sonnet |
| **G3e** (senior, done) | `GuiLauncher::render` through the stack: the carousel, its fade-in overlay, the notification lines/bubble, the `captureNextFrame()` taken before a menu or an extension opens, `AB_SHOT`. `render()` keeps what comes before the frame (`endBusy()`, the state selector's frame) and runs `stack().frame(transparent black, draw)`; a new non-virtual `draw()` holds the rest, the fade-in overlay last, as before. The capture, `AB_SHOT` and the DebugDriver's frame copy live in the renderer's own `clear()`/`present()`, which the stack's display calls unchanged, so `_actions.cpp` (`captureNextFrame(); render(); lastCapture()`) and the loop's frame-need rules did not change; no ScreenStack addition was needed. | launcher `evoui/screens/evoui_launcher_screen.cpp`, `evoui_launcher.h` | none | medium: the frame every other screen's backdrop is taken from - check the carousel in every state and every dialog over it |
| **G3f** (done) | **Actions.** `abgui::Action` (plan 5: Confirm, Back, Option, Extra, Menu, View, PrevTab/NextTab, PageUp/PageDown, Up/Down/Left/Right, First/Last) and `abgui::ActionMap`: an `ableem::Event` (+ the d-pad state) -> an action and pressed/released, the pad as today, the keyboard fallback (Enter/Esc/arrows/Tab/PgUp/PgDn/Home/End), the Confirm/Back swap flag (off). `HoldRepeat` moves to ab_gui (`abgui::HoldRepeat`; `gui/hold_repeat.h` keeps a `using`). Pure, no caller yet. **Built:** `abgui::Action` (+ `None`), `ActionMap` (default map = today's pad buttons and, for every key, what `ableem::KeyboardMap::toPad` sends it to; `fromButton/fromKey/fromEvent` -> `ActionEvent{action, pressed, released}`; `setSwapConfirmBack` exchanges Confirm/Back on the pad's buttons only, the keyboard keeps Enter/Esc; `bind`/`bindSpace`; First/Last have no default button or key - L1/R1 are PrevTab/NextTab, and the screen decides), `abgui::HoldRepeat` and `DpadHold` moved unchanged into `hold_repeat.h`, `gui/hold_repeat.h` aliases both. Test: `test_ab_gui_actions`. | `ab_gui/.../actions.h`, `ab_gui/src/actions.cpp`, `ab_gui/.../hold_repeat.h`, `gui/hold_repeat.h`, `tests/gui/test_ab_gui_actions.cpp`, `tests/classic/test_hold_repeat.cpp` (include) | none (HoldRepeat is header-only; same layout) | none |
| **G3g** (senior, done) | **`abgui::Screen`**: the base of ab_gui's screens, `: public ableem::GuiScreen`, holding a `Context&`; `draw()` pure, `render()` final = `ctx.stack().frame(draw)`; `loop()` reads the events through the `ActionMap` into `virtual void onAction(Action, bool pressed)`, whose default calls the old `doX_Pressed/Released`/`doJoyX`/key hooks exactly as `ableem::GuiScreen::loop` does today (the adapter under the old hooks); text input and the typing keys still reach their hooks. Nothing derives from it yet. Test: synthetic events through a headless GuiBase - the hooks called are the same, in order, as the old loop's for every button, d-pad direction and key (skips without a renderer). **Built:** `Screen(GuiBase&, Context&)` (public `ctx`); `render()` final (a Context without a stack gets one on its renderer); `loop()` = the old loop with each event to `handle()`: a mapped press/release -> `virtual onAction(const ActionEvent&)`, anything else (an unbound key, text, a device event) -> `virtual onUnmapped(const Event&)`. The action alone cannot pick the old hook - Cross and Enter are both Confirm but `doCross_Pressed`/`doEnter`, and the d-pad hooks read the live d-pad state, not the event - so `ActionEvent` carries its source `event` and the default `onAction` (`legacyAction`) is: a pad button -> the hook of the button its action belongs to (`classicButton`: Confirm Cross ... PageDown R2, First L1, Last R1 - so the swap, when a later step turns it on, reaches the legacy screens too), a d-pad direction -> `dispatchDpad()` (live state, old priority), a key -> its own key hook. The old loop's switches moved, unchanged, into non-virtual `ableem::GuiScreen::dispatchEvent/dispatchDpad/dispatchButton/dispatchKey`, which both loops call (no layout or vtable change). The program's one map is the Context's `actions` (appended). Test: `test_ab_gui_screen` - both loops over the same injected events, hook for hook, plus the expected hooks (every button, direction, two directions held, every key with the keyboard-as-pad off and on, key repeats, text, a hook closing the screen mid-batch, a hold-repeat inside a hook: same steps, pace, end on the release), the swap, a rebind, an `onAction` override, `render()` through the stack. **Left for z / later:** the classic shim on `Screen` (a base change is a layout change); the screens with their own `loop()` (About, ActionMenu, Confirm, FactsPage, KeepDisplay, Keyboard, SelectMemcard, Splash, TextPage, the game editors; the launcher's evoui screens) still read raw events until each moves (h-n, z); the swap with the keyboard-as-pad on - `Input` turns Enter into a Cross event before the map sees it, so turning the swap on needs `Input` to mark a key-made pad event (an `Event` layout change - z) or the map to see the key. | `ab_gui/.../screen.h`, `ab_gui/src/screen.cpp`, `ab_gui/.../actions.h`, `actions.cpp`, `context.h`, `lib_ableem/.../gui_screen.h`, `lib_ableem/src/ui/gui_screen.cpp`, `CMakeLists.txt`, `tests/gui/test_ab_gui_screen.cpp`, `tests/CMakeLists.txt` | none (a new class; `ableem::GuiScreen` gains only non-virtual functions; Context appended; the classic shim moves onto it in z) | low: no screen uses it yet; the old loop now calls its switches through the moved functions - same code, same order |
| **G3h** (done) | **TextPage** (the smallest, the pattern for the rest): `abgui::TextPage` (the lines, `splitItem`, the scroll, Circle back) on `abgui::Screen` and the G3b panel; `GuiTextPage`'s .cpp forwards to it; its in-tree callers switch. The DebugDriver sees the same screen name for the old class. **Built:** `abgui::TextPage : Screen` with `title`/`lines`/`centred`/`color`, `draw()` on `Panel::full(ctx)` and the Context (the same numbers and calls as the old `draw()`), its own `loop()` (frame need Idle, a frame when due, then every event to `handle()`), `onAction` (Back closes, PageUp/PageDown page by the action; the d-pad by its live state; keys as keys) and `onUnmapped` (the keys); pure `abgui::wrapText` (`TextRenderer::wrapLines` forwards), `splitItem`, `canScroll`, `scrolled`. Context gained `lineDrawer`/`drawLine` (appended; Gui wires it to `renderText`) for the blank and centred lines, which draw in the font's own colour. `GuiTextPage::render()/loop()/splitItem` forward (header untouched; `show()` stays the old class, so the DebugDriver's name is unchanged); no launcher caller exists. Test: `test_ab_gui_text_page`. | `ab_gui/.../text_page.h`, `ab_gui/src/text_page.cpp`, `context.h/.cpp`, `gui/gui.cpp`, `gui/text_renderer.cpp`, `gui/screens/gui_text_page.cpp`, `tests/gui/test_ab_gui_text_page.cpp`, `tests/CMakeLists.txt` | none (`GuiTextPage` header untouched) | low; Sonnet |
| **G3i** (done) | **FactsPage**: `abgui::FactsPage` (sections, heading bands, label/value rows, paging, the refresh interval) with `title()/collect()/onButton()/extraHints()` as today; `GuiFactsPage` keeps its header (PSC-Bios derives from it) and forwards; Hardware Information moves onto the new one only if that keeps its header/ABI rules. **Built:** `abgui::FactsPage : Screen` (`FactsSection`/`FactsRow` in, `Line` out; the four hooks protected virtuals as before), `draw()` on `Panel::full(ctx)` and the Context (the same numbers and call order as the old `draw()`: the heading band by `Style::label`, heading/label/value in the style's heading/row/rowSelected, a value right-aligned at the value column by `textWidth`), its own `loop()` (refresh when due, a frame when due, then each event to `handle()`), `onAction`/`onUnmapped` (the d-pad by its live state, up first; PrevTab/NextTab the first/last row, PageUp/PageDown a page, then `onButton()`, then Back closes); pure `linesOf`, `maxFirstVisible`, `scrolled`, `refreshDue`, `counter`, `valueColumn` and the free `elideText` (`TextRenderer::elide` forwards). `GuiFactsPage::render()/loop()` build a `Forward : abgui::FactsPage` (its hooks call the classic page's protected virtuals through functions) from the header's `lines`/`firstVisible`/`rowsThatFit`/`lastRefresh`/`font`/`refreshInterval`, run it and copy the state back; `init()`/`refresh()` stay on the old class. **Hardware Information keeps deriving from `GuiFactsPage`** (moving it would change its base and vtable - ABI 7, G3z - and PSC-Bios derives the same way); nothing changes there. Test: `test_ab_gui_facts_page`. | `ab_gui/.../facts_page.h`, `ab_gui/src/facts_page.cpp`, `gui/screens/gui_facts_page.cpp`, `gui/text_renderer.cpp`, `tests/gui/test_ab_gui_facts_page.cpp`, `tests/CMakeLists.txt` | none (`GuiFactsPage` header untouched) | low; Sonnet |
| **G3j** (done) | **Confirm**: `abgui::Confirm` (label, the two answers, `result`); `GuiConfirm` forwards; the launcher's callers stay on `GuiConfirm` (it is the same dialog now, and the Store constructs it). **Built:** `abgui::Confirm : Screen` with `label`/`title`/`confirmLabel`/`cancelLabel`/`result`, `draw()` on a `Panel` over the pure `Confirm::panelRect`/`textWidth` (the same numbers and call order as the old `draw()`), its own `loop()` (`waitForEvent(250)`, a frame when nothing came, events to `handle()`), `onAction` (Confirm yes/Cursor, Back no/Cancel) and `onUnmapped` (Enter, Escape). Context gained `shadowSwitch`/`setTextShadow` (appended; Gui wires it to the text renderer) for the dialog's halo. `GuiConfirm::render()/loop()` forward through a fresh `abgui::Confirm` seeded from the header's fields; `GuiKeepDisplay` keeps its own loop and renders through `GuiConfirm::render()`. Test: `test_ab_gui_confirm`. | `ab_gui/.../confirm.h`, `ab_gui/src/confirm.cpp`, `context.h/.cpp`, `gui/gui.cpp`, `gui/screens/gui_confirm.cpp`, `tests/gui/test_ab_gui_confirm.cpp`, `tests/CMakeLists.txt` | none (`GuiConfirm` header untouched - the Store constructs it) | low; Sonnet |
| **G3k** (done) | **ActionMenu**: `abgui::ActionMenu` (rows of a name over a description, headings, disabled rows with a reason, wrap, `background`, `result`) - the Quick and System menus' panel; `GuiActionMenu` forwards. **Built:** `abgui::ActionMenu : Screen` with `Item{title, description, heading, disabled}`, `title`/`subtitle`/`crossLabel`/`circleLabel`/`selected`/`wrap`/`background`/`result`, `draw()` on a `Panel` (the same numbers and call order as the old `draw()`), pure `selectable`/`rowHeight`/`roomForRows`/`visibleCount`/`scrolledTo`/`moved`, its own `loop()` (frame when due, `DpadHold`, events to `handle()`), `onAction` (Confirm picks, Back leaves, the d-pad by its live state) and `onUnmapped`. `GuiActionMenu::init()/render()/loop()` forward through a fresh `abgui::ActionMenu` seeded from the header's fields and copy `selected`/`result`/the scroll back; `show()` stays on the old class. **The launcher's Quick/System menus stay on `GuiSystemMenu`**: its own class (not derived from `GuiActionMenu`) with 20/14/15 px fonts, a description strip and status notes that `FontRole` does not offer; the DebugDriver's `items`/`selected` are published there and unchanged. Switching it needs font roles by size - a later step. Test: `test_ab_gui_action_menu`. | `ab_gui/.../action_menu.h`, `ab_gui/src/action_menu.cpp`, `gui/screens/gui_action_menu.cpp`, `tests/gui/test_ab_gui_action_menu.cpp`, `tests/CMakeLists.txt` | none (`GuiActionMenu` header untouched) | low; Sonnet |
| **G3l** | **Busy**: `abgui::Busy` (the backdrop, the dimmed spinner, the message, the bar, the 40 ms pace, the flush at the end - the busy rule stays in `Input`), reached as `ctx.stack().busy()`; `Gui::beginBusy/busyTick/setBusyProgress/endBusy/tickBusy/drawText` forward to it (their members stay unused until z); the DebugDriver's `busy` from it. | `ab_gui/.../busy.h`, `ab_gui/src/busy.cpp`, `screen_stack.*`, `gui/gui.cpp` | none (Gui's methods kept) | medium: every long job (Options closing, a theme load, a game delete, PSC-Bios' jobs) - `tests/classic/test_busy_input.cpp` must stay green |
| **G3m** (m1 done, m2 open) | **List** from `GuiMenuBase`: m1 the model - selection, paging, headings skipped, first/last, wrap, `landOnSelectable` - as `abgui::ListModel` (pure; `test_menu_base_navigation.cpp` then tests the real class instead of its local copy); **m1 built:** `abgui::ListModel` is header-only (inline templates over the skip predicate, so an extension needs no host symbol; no `list_model.cpp`) working on a `View` of references to `GuiMenuBase`'s own members, whose layout is unchanged; the class's `adjustPageBy`/`landOnSelectable`/`computePagePosition`/`doKeyDown`/`Up`/`PageDown`/`PageUp`/`Home`/`End` forward to it; `test_menu_base_navigation` tests the real model and compares it move for move with a frozen copy of the old code over all list shapes; m2 the drawing and input - `abgui::List` (rows, right-aligned values, the switch, scroll markers, the compact panel for a short list) and `GuiMenuBase` becomes a thin template over it (header-only: free to change). | m1: `ab_gui/.../list_model.h`, `ab_gui/src/list_model.cpp`, `gui/menus/gui_menu_base.h`, the test; m2: `ab_gui/.../list.h`, `ab_gui/src/list.cpp`, `gui/menus/gui_menu_base.h`, `gui_string_menu.h`, `gui_two_column_string_menu.h` | none (templates are the extension's own copy) | m1 low; m2 medium - every list: Options, the editors, Game Manager, Memory Cards, playlists, the game dir menu, PSC-Bios' menus; Sonnet with a senior review |
| **G3n** | **Keyboard**: `abgui::Keyboard` (the pages, Shift/caps, the function row, the USB keyboard alongside, Esc, UTF-8 by characters, keyboard-as-pad off while it shows); `GuiKeyboard` forwards. | `ab_gui/.../keyboard.h`, `ab_gui/src/keyboard.cpp`, `gui/screens/gui_keyboard.cpp`, `tests/core/test_keyboard.cpp` (if the page tables move) | none (`GuiKeyboard` header untouched - the Store and PSC-Bios construct it) | medium: typing on the pad and the USB keyboard; Sonnet |
| **G3z** (senior, one merge) | **The ABI step.** The classic `GuiScreen` shim moves onto `abgui::Screen` (every screen reads actions; `render()` is the stack's, a screen has `draw()` only - the ones still overriding `render()` are converted; the shim's constructor passes `Gui::uiContext()`; a screen with its own `loop()` hands its events to `handle()` so they go through the map too - from G3g's list, those not moved in h-n); the old `Gui*` widget classes become thin subclasses of the ab_gui ones (their layouts change); `Gui`'s busy and compact-panel members that nothing uses any more go; `AB_SDK_ABI` 7 (`gui/extension.h` history line); the Store (`ext_store`) and PSC-Bios/ABFlashKit (`autobleem-console-tools`) adjusted where they used what went, rebuilt once, their gitlinks bumped; both CLAUDE.md ABI notes. | core `gui/gui_screen.h`, the `gui/screens/*`, `gui/menus/*`, `gui.h/.cpp`, `extension.h`; launcher gitlink + screens overriding `render()`; ext_store, autobleem-console-tools | **7** - an extension built for 6 is refused | high: everything at once - the full 58-screen diff, then the Store and PSC-Bios driven on the VM |

The DebugDriver keeps seeing what it sees today in every step (the screen names on its stack, `items`/`selected`,
`busy`): the scripts in `docs/testing.md` must run unchanged; a widget reports itself from G3h on, the per-screen
publishing code goes in z.

## 12. The owner's decisions (2026-09-30)

1. Namespace `abgui`, headers `<ab_gui/...>` - **yes**.
2. The Confirm/Back swap (Nintendo/Japanese layout) - **yes, as an Options row for users** too.
3. GlyphSets - **not drawn by us**: button prompts that only show which button to press are descriptive use of the
   pads' marks (as RetroArch and Steam do). Proposed default set: Kenney's "Input Prompts" (CC0 - Xbox,
   PlayStation, Switch, Steam Deck, keyboard, one style); a theme or a program may replace any glyph.
