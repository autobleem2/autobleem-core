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
- C++14 (the console's gcc-6), no SDL include outside lib_ableem (as today), clang-format/tidy rules as today.
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

## 12. The owner's decisions (2026-09-30)

1. Namespace `abgui`, headers `<ab_gui/...>` - **yes**.
2. The Confirm/Back swap (Nintendo/Japanese layout) - **yes, as an Options row for users** too.
3. GlyphSets - **not drawn by us**: button prompts that only show which button to press are descriptive use of the
   pads' marks (as RetroArch and Steam do). Proposed default set: Kenney's "Input Prompts" (CC0 - Xbox,
   PlayStation, Switch, Steam Deck, keyboard, one style); a theme or a program may replace any glyph.
