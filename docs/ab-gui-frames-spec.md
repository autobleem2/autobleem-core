# ab_gui frames - the drawing spec (G4, the first set)

What an illustrator needs to draw the frames of a theme, and what a theme author writes in `theme.json` to use them.
The mechanism is `docs/ab-gui-plan.md`, "G4 sub-steps". The first set, the owner's (2026-09-30): **panel, row
selection, heading band, key, text field**. Chips, progress bars, tabs, tiles, badges and toasts come later.

A theme without frames draws exactly as today (code-drawn boxes). A theme may set any of the frames below and leave
the others out: each one left out stays code-drawn.

## 1. How a frame is drawn

**Units.** The UI is laid out on a **1280x720 canvas** whatever the screen is; every number here is in those
**logical pixels**. A 1x image has one image pixel per logical pixel; its `@2x` twin has two per logical pixel in
each direction.

**9-slice.** A frame is one PNG cut by four lines into nine pieces and drawn into a box of any size:

```
   +------+-------------------+------+
   |  TL  |       top         |  TR  |   corners: drawn 1:1, never stretched
   +------+-------------------+------+
   |      |                   |      |   top/bottom edges: stretched sideways only
   | left |      centre       | right|   left/right edges: stretched up/down only
   |      |                   |      |   centre: stretched both ways (or left out: "fill": false)
   +------+-------------------+------+
   |  BL  |      bottom       |  BR  |
   +------+-------------------+------+
```

- `slice` = the four cut lines, measured **from the image's outer edge**, in logical px: `left`, `top`, `right`,
  `bottom`.
- **Nothing tiles** (G4 stretches only). So an edge must look the same all along its length: a straight rim, a
  gradient *across* the edge (a glow fading outwards) are fine; a pattern, a notch or a gradient *along* the edge is
  not. The centre is flat, or a gentle gradient (it stays smooth when stretched); texture or noise in it smears.
- Put every cut line **at least 1 px away from a colour change**: the GPU blends one pixel across a cut line.
- A box smaller than the two corners together gets its corners shrunk in proportion (it squashes) - the minimum
  boxes below never do that with the recommended numbers.

**Bleed (glow, shadow).** The box is where the element is; a glow or a shadow may reach outside it. `bleed` says how
far the image reaches outside the box on each side (logical px): the image is the box **plus** the bleed all
round, and it is drawn at the box grown by the bleed. The bleed is inside the slices: `slice` >= `bleed` + the
visible corner. Maximum bleed per frame is in the tables (more would reach into a neighbour or off the panel).

**Hi-res (`@2x`).** Draw every frame also at twice the size: `panel@2x.png` next to `panel.png`, exactly 2x the width
and height, the same design with twice the detail. `theme.json` keeps the 1x numbers. The picture chosen:
the `@2x` one on a screen above 720p (a Pi or a PC at 1080p draws the canvas at 1.5x), the 1x one on 720p (the
PlayStation Classic). A theme may ship only the `@2x` one: it is then drawn everywhere, scaled down on 720p (a little
softer). A 1 px rim at 1x is a 2 px rim at 2x - draw the thin lines on whole pixels in both.

**Colour.** Two ways, per frame:
- **Full colour** (no `tint`): drawn with the image's own colours.
- **Tintable** (`"tint": "<role>"`): the image is multiplied by one of the theme's colour roles from
  `launcher.colors` - `text`, `secondary`, `hint`, `row`, `rowSelected`, `heading`, `value`, `description`,
  `footer`, `selectionBand`, `edge`. Draw it in white and greys (white = the role's colour, grey = a darker shade of
  it, black stays black); its alpha is kept. Then one PNG serves every colour scheme and follows the theme's colours.

**File format.** PNG, 32-bit RGBA, straight (not premultiplied) alpha, sRGB, no colour profile needed. Transparent
where nothing is drawn. Files in the theme's `frames/` folder, lower-case names as below.

**What the code still draws over a frame** (the frame is only the box): all text, the rules (1 px lines) in the panel,
the scroll markers, the button icons, the keyboard's caret and the arrows on Shift/Backspace, the grey veil over a
row that cannot be changed, and the dimming of the screen behind a panel.

## 2. The frames

The "today" line says what the code draws now, so the frame can be designed as its replacement. Sizes are the
recommended ones; any size works if `slice` and the file agree.

### 2.1 `panel` - the panel (sheet)

**What and where.** The dark sheet every menu, list and dialog sits on. **Full panels** (Options, Game Manager, the
game editors, Memory Cards, Hardware Information, the on-screen keyboard, text pages): about **1220 x 650**.
**Compact panels** (Confirm, the Quick and System menus, the set picker, the update prompt, Extensions, short lists):
**800** wide, **172 to ~560** high. The notification bubble (top right, 440 wide, from ~60 high) takes it too (G4b).

**Today.** Black at 78% alpha, a 1 px edge in `edge` at 63%, over the screen dimmed by black at 43%.

**Over it, by the code** (from the box's top-left): the title at x 24, y 18 (bold 28); a 1 px rule in `edge` from
x 24 to w-24 at y 66; the rows from y 74; the footer = the last 54 px, a 1 px rule at its top, the button hints at
x 24, y +14; scroll-marker triangles at x w-24. A selected row's band spans x 1 .. w-1 over the edges.

| | |
|---|---|
| Files | `frames/panel.png` **96 x 96**, `frames/panel@2x.png` **192 x 192** |
| Box part | 72 x 72 in the middle of the image (12 px bleed round it) |
| `slice` | **36** all round (12 bleed + 24 corner). The top may go up to **86** (12 + the 74 px header) to style the header band, the bottom up to **66** (12 + the 54 px footer) to style the footer band - then the image grows to match (e.g. 96 x 176) |
| `bleed` | **12** recommended, **16** maximum (a compact panel is 40 px from the screen's edge) |
| Centre | stretched; dark and at least **70% opaque** - white and grey text must read over any background picture |
| Visible rim | keep it within **6 px** of the box's edge on the left and right (the rows' text starts at 32 px) |
| States | one |
| Colour | either; tintable with `edge` works well (a white rim, a black centre - black stays black) |
| Smallest box | 800 x 172 (a compact panel with one row) - the corners (24 + 24) and the header/footer slices must fit |

### 2.2 `selection` - the selected row

**What and where.** The cursor's row in every list and menu, the full width of the row: the classic lists (row
height = the list font's line, about **28 px**; up to ~1170 wide, narrower beside a detail pane), the compact menus
(**44** and **60 px** rows, 798 wide), the System menu (**32 px**).

**Today.** The row filled with `selectionBand` at 15% alpha, and a solid 5 px bar in `selectionBand` at its left edge.

**Over it, by the code.** The row's text in `rowSelected` (default white) from 32 px in, its value right-aligned
24 px from the right; a second line (description) in a 60 px row. Since G4c a selection frame is drawn **under** the
row's text.

| | |
|---|---|
| Files | `frames/selection.png` **48 x 40**, `frames/selection@2x.png` **96 x 80** |
| Box part | 40 x 32 (4 px bleed round it) |
| `slice` | **left 12, top 10, right 12, bottom 10** (4 bleed + 8 / 6 of art) - the top and bottom art together at most **20 px**, since the smallest row is 26 px |
| `bleed` | **4** recommended, **6** maximum (the rows above and below touch it) |
| Centre | stretched; translucent (15-40%) - the row's text must read over it |
| States | one (selected). A selected row that cannot be changed gets the code's grey veil over it |
| Colour | tintable with `selectionBand` recommended, so it follows the theme's accent |
| Smallest box | 400 x 26 |

### 2.3 `heading` - the heading band

**What and where.** The thin band behind a group heading between rows: Options' groups, the Quick/System menus'
groups, a facts page's sections (Hardware Information, PSC-Bios), the Extensions list, the button guide. Full row
width; **24 px** high in the menus, the list font's line (~**28 px**) in the classic lists.

**Today.** A flat band in `edge` at 27% alpha.

**Over it, by the code.** The heading's text in `heading`, from 32 px in (bold 15, or the list font).

| | |
|---|---|
| Files | `frames/heading.png` **40 x 24**, `frames/heading@2x.png` **80 x 48** |
| Box part | the whole image (no bleed) |
| `slice` | **left 12, top 6, right 12, bottom 6** - top and bottom together at most **18 px** |
| `bleed` | **0** recommended, **2** maximum |
| Centre | stretched; quieter than the selection, so the two are never confused |
| States | one |
| Colour | tintable with `edge` recommended |
| Smallest box | 400 x 24 |

### 2.4 `key`, `keyFunction`, `keyLit`, `keySelected` - the on-screen keyboard's keys

**What and where.** The keyboard (typing a name, a Wi-Fi password in PSC-Bios, a search in the Store): 10 columns of
keys in 5 rows, **up to 96 x 64**, with an **8 px gap**; on a smaller panel they shrink (height from about **48**). The function
row's keys span several columns (Space, Done: up to ~500 wide).

**Today.** Normal: white at 7% with a 1 px edge in `secondary` at 43%; a function key white at 3%; lit (Shift on)
white at 20%; selected: `text` at 24% with a 1 px outline in `text`.

**Over it, by the code.** The label centred in `text` (a letter in medium 22, a word in bold 20), the Shift and
Backspace arrows.

| | |
|---|---|
| Files | `frames/key.png`, `frames/key_function.png`, `frames/key_lit.png`, `frames/key_selected.png` - each **48 x 48**, their `@2x` **96 x 96** |
| Box part | 40 x 40 (4 px bleed round it) |
| `slice` | **16** all round (4 bleed + 12 corner) - corners together at most **40 px** high, since a key may be 48 |
| `bleed` | **4** maximum (two neighbours' glows meet in the 8 px gap) |
| Centre | stretched; the label must read over it in `text` |
| States | `key` a letter/digit/symbol; `keyFunction` Shift, the page key, Space, Backspace, Done (a shade quieter than `key`); `keyLit` Shift while it is on; `keySelected` the key under the cursor - the strongest of the four (a glow belongs here). A missing `keyFunction` or `keyLit` falls back to `key`; a missing `keySelected` to `key` with today's outline over it |
| Colour | either; all four the same way |
| Smallest box | 40 x 44 |

### 2.5 `field` - the text field

**What and where.** The keyboard's text field, above the keys: **48 px** high, about **1100-1150** wide.

**Today.** White at 5% with a 1 px edge in `secondary` at 63%.

**Over it, by the code.** The typed text (medium 22) from 16 px in, vertically centred; the caret, 2 px in `text`.

| | |
|---|---|
| Files | `frames/field.png` **56 x 56**, `frames/field@2x.png` **112 x 112** |
| Box part | 48 x 48 (4 px bleed round it) |
| `slice` | **16** all round (4 bleed + 12 corner) |
| `bleed` | **4** recommended, **8** maximum |
| Centre | stretched; the text in `text` must read over it |
| States | one (the field always has the focus while the keyboard shows) |
| Colour | either |
| Smallest box | 600 x 48 |

## 3. `theme.json`: `launcher.frames`

One entry per frame, by its name; every key but `image` is optional.

| Key | Value | Default |
|---|---|---|
| `image` | the 1x PNG, relative to the theme folder | - (required) |
| `image2x` | the `@2x` PNG | `<image's name>@2x.png` next to it, when that file exists |
| `slice` | a number (all four) or `{ "left": l, "top": t, "right": r, "bottom": b }`, logical px from the image's outer edge | 0 (the whole image stretched) |
| `bleed` | the same form: how far the image reaches outside the box | 0 |
| `fill` | `false` leaves the centre out (a rim only) | `true` |
| `tint` | a colour role of `launcher.colors` the image is multiplied by | none (the image's own colours) |

A frame whose image is missing, or smaller than its slices, is ignored (the log says so) and that element stays
code-drawn. Frames are read from **the theme's own** `theme.json` only - never taken over from the `default` theme.

The first set with the recommended numbers:

```json
{
  "format": 1,
  "launcher": {
    "frames": {
      "panel":       { "image": "frames/panel.png", "slice": 36, "bleed": 12 },
      "selection":   { "image": "frames/selection.png",
                       "slice": { "left": 12, "top": 10, "right": 12, "bottom": 10 }, "bleed": 4,
                       "tint": "selectionBand" },
      "heading":     { "image": "frames/heading.png",
                       "slice": { "left": 12, "top": 6, "right": 12, "bottom": 6 }, "tint": "edge" },
      "key":         { "image": "frames/key.png", "slice": 16, "bleed": 4 },
      "keyFunction": { "image": "frames/key_function.png", "slice": 16, "bleed": 4 },
      "keyLit":      { "image": "frames/key_lit.png", "slice": 16, "bleed": 4 },
      "keySelected": { "image": "frames/key_selected.png", "slice": 16, "bleed": 4 },
      "field":       { "image": "frames/field.png", "slice": 16, "bleed": 4 }
    }
  }
}
```

(The `@2x` files are found by name; the rest of the theme - colours, images, fonts - is as `docs/theme-format.md` in
the launcher says. Which frames take effect: the panel since G4a, the others as G4b-e land.)

## 4. Delivery checklist

- A `frames/` folder: for each frame its 1x and `@2x` PNG, named as above.
- The `launcher.frames` block with the numbers used (if they differ from the recommended ones).
- For each frame: full colour or tintable (and with which role).
- Draw at 2x first, make the 1x from it, then check the 1x by eye: the thin rims on whole pixels, the corners crisp.
- Check the rules of 1.: edges uniform along their length, cut lines 1 px away from a colour change, the glow
  inside the bleed, the centre opaque enough for the text (the panel at least 70%).
