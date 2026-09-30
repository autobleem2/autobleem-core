# ab_gui EvolutionUI art - the drawing spec (G5)

What an illustrator needs to draw the launcher's own pieces for a theme (the first user: `ab2.0.0`), and what a theme
author writes in `theme.json` to use them. The mechanism and the order the pieces take effect in are
`docs/ab-gui-plan.md`, "G5 sub-steps" (the step is named with each piece). G4's frames (panel, selection, heading,
keys, field) are `docs/ab-gui-frames-spec.md`; **its section 1 - units, 9-slice, bleed, `@2x`, colour, file format -
holds here unchanged** and is not repeated.

A theme without these pieces draws exactly as today. A theme may set any of them and leave the others out: a frame left
out stays code-drawn, an icon left out comes from the `default` theme, else the launcher's own file.

## 1. Two kinds of piece

- **Frames** (`launcher.frames`, as in G4): 9-slice PNGs drawn into a box of any size. Read from **the theme's own**
  `theme.json` only - never taken over from `default`.
- **Icons** (`launcher.icons`, new in G5 - UIREV-30): fixed images drawn at their size (1x = logical px, the `@2x` twin
  on a screen above 720p), never stretched. **An icon falls back**: the theme's own, else `default`'s, else the
  launcher's built-in file (`evoimg/`, listed in 3.) - so a theme may replace one icon and keep the rest.
  - **Full colour** (no tint). Transparent where nothing is drawn; keep the shape inside the canvas with 1 px clear
    all round (the halo below needs it).
  - **The halo**: under every icon the code draws a dark outline built from the icon's own alpha (black at 60%, 1 px
    all round and 2 px down-right - UIREV-27), so a light icon reads on a light background. A theme whose icons carry
    their own outline or glow switches it off with `"iconHalo": false`.
  - Draw the icon **optically centred** in its canvas: the code places the canvas, not the shape.

**Where the pieces are** (the Games state of the carousel, 1280 x 720):

```
 +--------------------------------------------------------------------------------------+
 | [plate: pad batteries]                                     [toast: scan / banner]    |
 |                                                            [toast: message]         |
 |        covers ...        [coverGlow round the selected cover]     title             |
 |                                                                    publisher, year   |
 |                                                  [players] 1 Player [disc] 1 [badges]|
 |                              [ play 200x68 ]                                         |
 |                   [tile] [tile] [tile] [tile]      (the game menu row, 118 each)     |
 |  logo (background)         [hintBar: two lines of hints, chips, d-pad icons]         |
 +--------------------------------------------------------------------------------------+
```

## 2. The frames

"Today" says what the code draws now, so the frame can be designed as its replacement. "Over it" is what the code
still draws on top. Sizes are the recommended ones; any size works if `slice` and the file agree.

### 2.1 `toast` - the notification bubbles and the set banner (G5f)

**What and where.** The small panels at the top right: the scan's progress (title, a detail line, a bar), the
extensions' downloads, the **set banner** ("Showing: All games (24)"), a message ("Resume point saved"). Right edge at
x 1264, the first at y 16, the next stacked 8 px under it. Each slides in from the right edge (250 ms) and out again.
Width: the scan's 440; a banner or message as wide as its text plus 24 (from about 80 to 440). Height: **48** (a title),
**70** (with a detail line), **82** (with the bar), **92** (detail and bar).

**Today.** The panel sheet (black at 78%, a 1 px edge in `edge` at 63%) - or the theme's `panel` frame since G4b. A
theme without `toast` keeps getting `panel` here.

**Over it.** The title (bold 20, `text`) at 12, 12; the detail (bold 15, `secondary`) under it; the bar, 4 px high,
12 px in from each side (2.4).

| | |
|---|---|
| Files | `frames/toast.png` **64 x 64**, `frames/toast@2x.png` **128 x 128** |
| Box part | 48 x 48 (8 px bleed round it) |
| `slice` | **20** all round (8 bleed + 12 corner) - top and bottom art together at most **24 px** (a banner is 48 high) |
| `bleed` | **4** recommended, **8** maximum (two stacked bubbles are 8 px apart; the right edge is 16 px from the screen's) |
| Centre | stretched; at least **70% opaque** - the text reads over the covers and the background |
| States | one |
| Colour | either; tintable with `edge` works well |
| Smallest box | 80 x 48 |

### 2.2 `hintBar` - the launcher's hint bar (G5e)

**What and where.** The panel behind the two lines of button hints at the bottom of the carousel ("Play", "Game
menu", "Quick menu" / "Games shown", "Random", "Guide", "System"). Its box is the theme's own `launcher.hintBar`
rect - `ab2.0.0` today: x 360, y 642, **900 x 68**; the mockup's is about 840 x 76 from x 424 (move the rect with the
art). Each line fills half the bar's height.

**Today.** Nothing: the hints are drawn straight onto the theme's footer image (`launcher.footer`, 1280 x 88), whose
art is the band. With a `hintBar` frame, draw the footer image **without** its band: the frame is drawn in the band's
place (right after the footer image, under the covers, the game menu and the hints - G5e), into the `launcher.hintBar`
rect with its bleed outside it, and the footer image itself is still drawn.

**Over it.** Per hint: its button (a 30 px glyph, a d-pad icon or a chip - 2.3) and the label in `hint` (medium 22 down
to 14 - the largest that fits the language), centred in its line.

| | |
|---|---|
| Files | `frames/hint_bar.png` **80 x 80**, `frames/hint_bar@2x.png` **160 x 160** |
| Box part | 64 x 64 (8 px bleed round it) |
| `slice` | **28** all round (8 bleed + 20 corner) - top and bottom art together at most **40 px** |
| `bleed` | **8** maximum (the bar ends 10 px above the screen's bottom edge) |
| Centre | stretched; at least **60% opaque** |
| States | one |
| Colour | either |
| Smallest box | 600 x 48 (a theme's bar under 48 high shows one line) |

### 2.3 `chip` - a named button (G5d)

**What and where.** The small box a button without a glyph is drawn as - START, SELECT, L2+R2, ESC, a word like RESET:
the launcher's hint bar, every footer, the button guide, the Store, PSC-Bios. **24 px** high, as wide as the name plus
14 (about 28 for "L1", 60 for "SELECT"); neighbours 6 px apart.

**Today.** White at 9% with a 1 px edge in `edge` at 78%.

**Over it.** The name in capitals (bold, about 13 px, `text`), 7 px from the left edge, centred up and down.

| | |
|---|---|
| Files | `frames/chip.png` **32 x 32**, `frames/chip@2x.png` **64 x 64** |
| Box part | 28 x 28 (2 px bleed round it) |
| `slice` | **10** all round (2 bleed + 8 corner) - top and bottom art together at most **16 px** |
| `bleed` | **2** maximum |
| Centre | stretched; the name must read over it |
| States | one |
| Colour | tintable with `edge` recommended |
| Smallest box | 28 x 24 |

### 2.4 `progressTrack`, `progressFill` - progress bars (G5g; the bubbles' in G5f)

**What and where.** Every progress bar: the scan's and a download's bubble (**4 px** high, 416 wide), the busy
spinner's bar (**400 x 6**), the Software Update download (**22 px** high, about 700 wide), the Store's downloads.
The track is drawn into the whole bar, the fill into the part done (from its left edge; it may be 1 px wide).

**Today.** Track: `secondary` at 47%; fill: `text`. The update prompt: a 1 px outline and a fill 2 px inside it.

| | |
|---|---|
| Files | `frames/progress_track.png`, `frames/progress_fill.png` - each **16 x 8**, their `@2x` **32 x 16** |
| Box part | the whole image (no bleed) |
| `slice` | **left 4, top 2, right 4, bottom 2** |
| `bleed` | **0** recommended, **2** maximum |
| Centre | stretched both ways - a 4 px bar and a 22 px bar come from the same image, so keep the art flat up and down |
| States | the two files |
| Colour | tintable recommended: the track with `secondary`, the fill with `text` |
| Smallest box | 8 x 4 (a thinner fill squashes its ends - expected) |

### 2.5 `tab` - the set picker's current tab (G5h)

**What and where.** Select's set picker: three tabs across the top of an 800 wide panel - PlayStation, RetroArch,
Apps - each cell **150 x 102**; the current one is marked.

**Today.** The cell filled with `selectionBand` at 15% and a 5 px bar in `selectionBand` along its bottom. The other
tabs: no box, their icon at 47%.

**Over it.** The tab's icon (56 x 56, 3.) centred 10 px from the top, the label (bold 15) under it.

| | |
|---|---|
| Files | `frames/tab.png` **48 x 48**, `frames/tab@2x.png` **96 x 96** |
| Box part | the whole image (no bleed) |
| `slice` | **16** all round |
| `bleed` | **0** recommended, **4** maximum (the cells touch) |
| Centre | stretched; the icon and the label read over it |
| States | one (the current tab); the others draw no frame |
| Colour | tintable with `selectionBand` recommended |
| Smallest box | 150 x 102 |

### 2.6 `tile`, `tileSelected` - the game menu's tiles (G5i)

**What and where.** The four icons of the game menu (Settings, Game, Memory card, Resume) under Play: **118 x 118**
each, 130 apart; when the row is open the selected one grows to **177 x 177** (1.5x) about its centre. The resume-slot
picker draws the Resume tile four times at **319 x 319** (2.7x), side by side with no gap, the selected slot marked.
The frame is drawn into the grown box as a 9-slice: its corners and rim keep their logical size, only the middle
grows.

**Today.** No frame: the theme's `launcher.menuIcons` images (118 x 118) are the whole tile. In the picker the selected
slot gets a halo in `colors.selection` (or a red tint) and the others are dimmed. With `tile` frames, draw the
`menuIcons` as **the glyph alone** on transparent (the tile is the frame's), keeping the Resume icon's picture window
(`launcher.menuIcons.resumePicture`, default 25, 33, 68 x 52 in the icon's pixels).

**Over it.** The theme's `menuIcons` image in the same box; on Resume, the last save state's picture in its window; in
the picker, "Slot n" (medium 22) at `resumeSlotLabel`. A Resume with no save states is drawn at 47% - frame and icon.

| | |
|---|---|
| Files | `frames/tile.png`, `frames/tile_selected.png` - each **72 x 72**, their `@2x` **144 x 144** |
| Box part | 56 x 56 (8 px bleed round it) |
| `slice` | **24** all round (8 bleed + 16 corner) |
| `bleed` | **4** recommended, **8** maximum (12 px between tiles at rest) |
| Centre | stretched; the glyph and the Resume picture read over it |
| States | `tile` every tile; `tileSelected` the selected one while the row is open, and the selected slot in the picker - the strongest (a glow belongs here). Missing `tileSelected`: `tile` with today's halo |
| Colour | either; both the same way |
| Smallest box | 118 x 118 |

### 2.7 `band` - the resume-slot picker's strip (G5i)

**What and where.** The dark strip across the whole screen behind the resume-slot picker: **1280 x 520** from y 100.

**Today.** Black at 78%.

**Over it.** "Select resume slot to load" (bold 28, centred, y 110), the four tiles (2.6) from y 220.

| | |
|---|---|
| Files | `frames/band.png` **64 x 64**, `frames/band@2x.png` **128 x 128** |
| Box part | the whole image (no bleed) |
| `slice` | **24** all round - the left and right edges fall on the screen's edges |
| `bleed` | **0** |
| Centre | stretched; at least **70% opaque** |
| States | one |
| Colour | either |
| Smallest box | 1280 x 520 |

### 2.8 `coverGlow` - the light round the selected cover (G5k)

**What and where.** The glow behind the carousel's selected cover. Its box is the cover's face: about **222 x 222** at
rest (a PS1 jewel case; a RetroArch big box has its own shape, up to 222 on its long side). **Unlike every other
frame it scales with the cover**: as the row scrolls the cover shrinks towards a side slot and the frame's slices and
bleed shrink with it (all numbers are for the 222 box), and it fades out as the cover leaves the middle. It breathes
(80-100%, 5.6 s). It is drawn **behind** the covers and their reflections.

**Today.** A soft light in `colors.selection` (white without it), brightest behind the cover, reaching **44 px** past
each edge of the face - its real width and height, so a tall or a wide big box is lit round its own shape (CA1) - at
55% strength. The mockup's accent line under the cover belongs in this frame's bottom
bleed (about 8 px under the box).

| | |
|---|---|
| Files | `frames/cover_glow.png` **160 x 160**, `frames/cover_glow@2x.png` **320 x 320** |
| Box part | 64 x 64 in the middle (48 px bleed round it) |
| `slice` | **64** all round (48 bleed + 16) |
| `bleed` | **48** recommended, **60** maximum |
| Centre | `"fill": false` recommended (the cover covers it; a big box or an empty box would show it) |
| States | one; the code sets its alpha (strength and breathing) - draw it at full strength |
| Colour | tintable with `selection` recommended (the theme's accent) - draw it white |
| Smallest box | 110 x 110 (a cover half-way to a side slot; smaller ones get no glow) |

### 2.9 `plate` - the pad battery plate (G5l)

**What and where.** Top left, while a wireless pad reports its battery: a small plate from x 2, y 2, about **100-130**
wide, **41** high for one pad, **64** for two.

**Today.** The panel sheet (black at 78%, a 1 px edge in `secondary`).

**Over it.** Per pad, 14 px in: its tag ("P1", bold 15), the `battery` icon (3.) with the charge filled in by the code,
the percent (bold 15).

| | |
|---|---|
| Files | `frames/plate.png` **48 x 48**, `frames/plate@2x.png` **96 x 96** |
| Box part | 44 x 44 (2 px bleed round it) |
| `slice` | **16** all round (2 bleed + 14 corner) |
| `bleed` | **2** maximum (the plate is 2 px from the screen's edges) |
| Centre | stretched; at least **70% opaque** |
| States | one |
| Colour | either |
| Smallest box | 90 x 41 |

### 2.10 `play` - the Play button (G5j - only if the owner picks the frame over the images)

**What and where.** Play, under the selected cover: box **200 x 68** at x 540, y 428; it pulses to **1.2x** (240 x 82)
about its centre every 2 s (the frame is drawn into the grown box, corners at their size). Hidden while the game menu
is open.

**Today.** Two theme images, `launcher.playButton` (200 x 68) and `launcher.playText` (262 x 60, the word PLAY drawn
into the picture - so it is never translated), with a dark outline made from their alpha. **Two ways for `ab2.0.0`**:
(A) keep drawing the two images - no code needed; (B) this frame, with the `play` icon (3.) and the label in code: "Play"
in the player's language, "Start" on an App.

**Over it (B).** The `play` icon and the label (bold 28, `text`) side by side, centred.

| | |
|---|---|
| Files | `frames/play.png` **96 x 96**, `frames/play@2x.png` **192 x 192** |
| Box part | 80 x 80 (8 px bleed round it) |
| `slice` | **32** all round (8 bleed + 24 corner) - top and bottom art together at most **48 px** |
| `bleed` | **8** recommended, **16** maximum |
| Centre | stretched; the label must read over it |
| States | one (Play is always the action on the selected game) |
| Colour | either |
| Smallest box | 200 x 68 |

### 2.11 `badge` - a plate behind each meta badge (G5b, optional)

**What and where.** Behind each small icon of the game-info row right of the disc count - USB/internal, HD/SD, the lock,
favourite, RetroArch, the light guns: **30 x 30** each, 40 apart (not behind the players icon or the disc). Leave it out
for bare icons (the mockup has none).

| | |
|---|---|
| Files | `frames/badge.png` **40 x 40**, `frames/badge@2x.png` **80 x 80** |
| Box part | 32 x 32 (4 px bleed round it) |
| `slice` | **12** all round (4 bleed + 8 corner) |
| `bleed` | **4** maximum (10 px between badges) |
| Centre | stretched; the icon reads over it |
| States | one |
| Colour | either |
| Smallest box | 30 x 30 |

### 2.12 `footer` - the hint band of every panel screen (G5r8, optional)

**What and where.** The band along the bottom of every panel screen - Options, the editors, Game Manager, Memory
Cards, the System and Quick menus, Extensions, Processors, the set picker, the update prompt, Confirm, the keyboard,
the Store, PSC-Bios, ABFlashKit: the button hints on the left, a counter at the right. **54 px** high and as wide as its
panel (**800** for a compact one, about **1200** for a full one; the launcher's own `hintBar`, 2.2, is another piece).
It is drawn **over the panel's own frame** (`panel`), the hints and the counter over it. Leave it out and the code draws
today's 1 px rule along the band's top edge.

**Today.** The rule only: `edge` at 78%, from 24 px in on both sides.

**Over it.** Button icons (30 px) and their labels (`footer`, 22 down to 15 px), starting 24 px from the left edge and 14
px below the band's top; the counter (`description`) ends 24 px from the right edge.

| | |
|---|---|
| Files | `frames/footer.png` **64 x 62**, `frames/footer@2x.png` **128 x 124** |
| Box part | 56 x 54 (4 px bleed round it) |
| `slice` | **16** all round (4 bleed + 12 corner) - the middle stretches: across for the width, up and down for a band that is not 54 |
| `bleed` | **4** maximum (it reaches 4 px into the rows and 4 px below the panel) |
| Centre | stretched; the hints must read over it |
| States | one (the same on every screen) |
| Colour | either |
| Smallest box | 240 x 54 (a compact panel is never narrower) |

## 3. The icons (`launcher.icons`)

Sizes are the 1x canvas (logical px); the `@2x` file is exactly twice. "Built-in" is the launcher's own file a theme
falls back to (`resources/evoimg/`), which also shows today's design.

| Name | What and where | Built-in (today) | 1x | `@2x` | Step |
|---|---|---|---|---|---|
| `players` | The meta row's first icon, before "1 Player" (BUG-28: the one that needs a new drawing with an outline and a glow in the family of the others) | the theme's `launcher.metaPanel` (`meta_panel.png`) | 30 x 30 | 60 x 60 | G5b, G5r2 |
| `disc` | The meta row, before the disc count | `cd.png` | 30 x 30 | 60 x 60 | G5b |
| `usb` / `internal` | A game on the stick / one of the console's own | `usb.png` / `ps1.png` | 30 x 30 | 60 x 60 | G5b |
| `hd` / `sd` | High-definition on / off | `hd.png` / `sd.png` | 30 x 30 | 60 x 60 | G5b |
| `lock` / `unlock` | The game's own settings locked / free | `lock.png` / `unlock.png` | 30 x 30 | 60 x 60 | G5b |
| `favorite` | A favourite (today's file is 29 x 32 and gets cut - draw 30 x 30) | `favorite.png` | 30 x 30 | 60 x 60 | G5b |
| `retroarch` | Played in RetroArch; a RetroArch game's row | `ra.png` | 30 x 30 | 60 x 60 | G5b |
| `lightgun` / `lightgun2` | A light-gun game for one / two players | `lightgun.png` / `lightgun2.png` | 30 x 30 | 60 x 60 | G5b |
| `dpadUp` `dpadDown` `dpadLeft` `dpadRight` | The d-pad in the hint bar and every footer ("Game menu", "Choose"), in a 30 px line | `dpad_*.png` | 28 x 28 | 56 x 56 | G5a |
| `tabPlayStation` `tabRetroArch` `tabApps` | The set picker's tabs (the others at 47%) | `tab_*.png` (64 x 64, drawn at 56) | 56 x 56 | 112 x 112 | G5c |
| `raCover` / `appCover` | The art of a RetroArch game / an App that has none, laid into the big box (and App start's pane) | `ra-cover.png` / `app-cover.png` | 226 x 226 | 452 x 452 | not a theme key (the carousel's part, the same on every theme - the owner, 2026-09-30) |
| `bigBox` | The printed-cardboard edge of a RetroArch game's or an App's box - a 9-slice with a fixed **7 px** border (keep every detail within 7 px of the edge; the middle stays transparent) | `bigbox.png` | 226 x 226 | 452 x 452 | not a theme key (as above) |
| `extension` | An extension that ships no icon, in the Extensions list (none built in) | - | 56 x 56 | 112 x 112 | G5c |
| `battery` | The pad battery's empty outline and nub; the code fills the charge into x 2..24, y 2..11 (`text`, `hint` when low) | code-drawn | 29 x 13 | 58 x 26 | G5l |
| `play` | Play's glyph (2.10, way B only) | - | 28 x 28 | 56 x 56 | G5j |
| `storeInstalled` | The Store's "Installed" mark on an installed item's row: at the row's right, vertically centred, 24 px inside the list panel's right edge (decision 16); no icon = a 24 x 24 check drawn in the `edge` colour | code-drawn check | 32 x 32 | 64 x 64 | G5t |
| `switchOn` / `switchOff` | A yes/no row's value in Options, the editors, the Store, PSC-Bios - drawn only in a theme that ships them (G5m, decided: no built-in switch); every other theme says ON/OFF. Right edge where the value text would end, centred on the row; a disabled row's veil covers it | - | 60 x 30 | 120 x 60 | G5m |

## 4. What the theme already has (images, no G5 code)

Still drawn from the theme's own images, as `docs/theme-format.md` (launcher) says - listed so the set is complete:
`launcher.background` (1280 x 720), `launcher.footer` (1280 x 88 - without its hint band when `hintBar` is set),
`launcher.settingsPanel` (1282 x 229, the band behind the open game menu), `launcher.arrow` (24 x 24, bobbing over the
row), `launcher.playButton` (200 x 68) and `launcher.playText` (262 x 60) - way A of 2.10, `launcher.menuIcons` (118 x
118 each - glyph only with `tile` frames), `launcher.memcardManager` (grid 256 x 420, pencil), `launcher.hints` (the
footer's cross/circle/triangle, 30 x 30), `classic.buttons` (30 x 30; `check`/`uncheck` 60 x 30), `classic.background`,
`classic.logo`.

## 5. `theme.json`

The frames join G4's in `launcher.frames` (same keys: `image`, `image2x`, `slice`, `bleed`, `fill`, `tint` - see
`docs/ab-gui-frames-spec.md`, 3.). The icons are a new block: a name -> the 1x file, or `{ "image", "image2x" }` when
the `@2x` file is not `<name>@2x.png` next to it. `iconHalo: false` drops the code's dark outline under every icon.

```json
{
  "format": 1,
  "launcher": {
    "frames": {
      "toast":         { "image": "frames/toast.png", "slice": 20, "bleed": 4 },
      "hintBar":       { "image": "frames/hint_bar.png", "slice": 28, "bleed": 8 },
      "chip":          { "image": "frames/chip.png", "slice": 10, "bleed": 2, "tint": "edge" },
      "progressTrack": { "image": "frames/progress_track.png",
                         "slice": { "left": 4, "top": 2, "right": 4, "bottom": 2 }, "tint": "secondary" },
      "progressFill":  { "image": "frames/progress_fill.png",
                         "slice": { "left": 4, "top": 2, "right": 4, "bottom": 2 }, "tint": "text" },
      "tab":           { "image": "frames/tab.png", "slice": 16, "tint": "selectionBand" },
      "tile":          { "image": "frames/tile.png", "slice": 24, "bleed": 4 },
      "tileSelected":  { "image": "frames/tile_selected.png", "slice": 24, "bleed": 4 },
      "band":          { "image": "frames/band.png", "slice": 24 },
      "coverGlow":     { "image": "frames/cover_glow.png", "slice": 64, "bleed": 48, "fill": false,
                         "tint": "selection" },
      "plate":         { "image": "frames/plate.png", "slice": 16, "bleed": 2 },
      "footer":        { "image": "frames/footer.png", "slice": 16, "bleed": 4 }
    },
    "iconHalo": false,
    "icons": {
      "players": "icons/players.png", "disc": "icons/disc.png",
      "usb": "icons/usb.png", "internal": "icons/internal.png", "hd": "icons/hd.png", "sd": "icons/sd.png",
      "lock": "icons/lock.png", "unlock": "icons/unlock.png", "favorite": "icons/favorite.png",
      "retroarch": "icons/retroarch.png", "lightgun": "icons/lightgun.png", "lightgun2": "icons/lightgun2.png",
      "dpadUp": "icons/dpad_up.png", "dpadDown": "icons/dpad_down.png",
      "dpadLeft": "icons/dpad_left.png", "dpadRight": "icons/dpad_right.png",
      "tabPlayStation": "icons/tab_playstation.png", "tabRetroArch": "icons/tab_retroarch.png",
      "tabApps": "icons/tab_apps.png",
      "battery": "icons/battery.png"
    }
  }
}
```

(`play`, `badge` and `footer` frames, and the `play`, `extension`, `raCover`, `appCover`, `bigBox`, `switchOn`/`switchOff`
icons, only when drawn - see their rows. Which pieces take effect: each as its G5 step lands.)

## 6. Delivery checklist

- `frames/`: each frame's 1x and `@2x` PNG, named as in 2.; `icons/`: each icon's 1x and `@2x`, named as in 5.
- The `launcher.frames` and `launcher.icons` blocks with the numbers used (if they differ from the recommended ones),
  and `iconHalo`.
- For each frame: full colour or tintable (with which role); for the icons: whether they carry their own outline.
- The footer image without its hint band (with `hintBar`); the `menuIcons` as glyphs alone (with `tile`).
- The players icon (BUG-28) in the same family as the other meta icons, with its outline and glow.
- Draw at 2x first, make the 1x from it, check the 1x by eye: thin lines on whole pixels, corners crisp, icons centred.
- G4's rules for every frame (frames spec 1.): edges uniform along their length, cut lines 1 px from a colour change,
  a glow inside the bleed, the centres opaque enough for their text (`toast`, `band`, `plate` at least 70%, `hintBar`
  60%).
- The owner's picks first where there is a choice: Play (2.10, A or B), the switches (UIREV-10).
