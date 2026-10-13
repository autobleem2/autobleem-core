// ab_text - lib_ableem's text engine in Rust (rust/ab_text). Internal to lib_ableem: only src/ui/font.cpp and the
// tests include it. Every size and position here is in output pixels; strings are UTF-8 with an explicit length.
// One process-wide engine behind a lock, so measuring from any thread is safe; drawing (the atlas) is the UI
// thread's.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AbtFont AbtFont;

// one glyph to draw: the destination rectangle relative to the text's top-left, the source rectangle in its
// atlas page (pixels)
typedef struct AbtQuad {
    int32_t x, y, w, h;
    int32_t u, v;
    int32_t page;
} AbtQuad;

typedef struct AbtRect {
    int32_t x, y, w, h;
} AbtRect;

enum { ABT_ALIGN_LEFT = 0, ABT_ALIGN_CENTER = 1, ABT_ALIGN_RIGHT = 2 };

// adds a font file to the fallback chain (faces already loaded are kept); returns the number of faces it holds,
// -1 when the file cannot be read. The chain is searched for any character the font in use does not have.
int abt_add_fallback(const char *path);

// the first face of the file at `px` pixels per em (SDL_ttf's point size); NULL when the file cannot be read
AbtFont *abt_font_load(const char *path, float px);
void abt_font_free(AbtFont *font);

// SDL_ttf's metrics: the line height is ascent + descent (no line gap), the baseline `ascent` below the top
int32_t abt_font_line_height(const AbtFont *font);
int32_t abt_font_ascent(const AbtFont *font);

// the width of the widest line and the number of lines; wrapWidth <= 0 = no wrapping ('\n' still breaks)
void abt_measure(const AbtFont *font, const char *text, size_t len, int32_t wrapWidth, int32_t *width, int32_t *lines);

// the text broken into lines no wider than wrapWidth (Unicode line breaking, a word wider than the line broken
// between characters), joined with '\n'. Writes up to cap bytes (no terminator) and returns the full length.
size_t abt_wrap(const AbtFont *font, const char *text, size_t len, int32_t wrapWidth, char *out, size_t cap);

// the byte offsets where a line may break (Unicode line breaking, UAX #14: after a space, between two Chinese
// characters but not before 。or ，...), the end of the text left out. Writes up to cap, returns how many.
size_t abt_line_breaks(const char *text, size_t len, uint32_t *out, size_t cap);

// how many characters no font of the chain has (drawn as the empty box)
size_t abt_missing_glyphs(const AbtFont *font, const char *text, size_t len);

// lays the text out, puts every glyph it needs into the atlas and writes up to cap quads; returns how many
// there are (call again with a bigger buffer when that is more than cap - the layout is cached). Each line is
// aligned within the widest line (or within wrapWidth when wrapping), the way SDL_FontCache aligns.
size_t abt_quads(const AbtFont *font, const char *text, size_t len, int32_t wrapWidth, int32_t align, AbtQuad *out,
                 size_t cap);

// the atlas: square pages of abt_atlas_page_size() pixels, abt_atlas_pages() of them now. A page's changed
// part comes out once as white RGBA pixels (alpha = coverage) into dst (pitch bytes per row, room for the whole
// page); returns 0 when the page has nothing new.
int32_t abt_atlas_page_size(void);
int32_t abt_atlas_pages(void);
int32_t abt_atlas_take_dirty(int32_t page, AbtRect *rect, uint8_t *dst, int32_t pitch);
// the textures were lost (a new renderer, a device reset): every page comes out whole again
void abt_atlas_all_dirty(void);

#ifdef __cplusplus
}
#endif
