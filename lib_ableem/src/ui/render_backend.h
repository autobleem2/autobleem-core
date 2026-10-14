/* The renderer backend's C table: what a loadable renderer (ab_render_gles3.so / .dll, rust/ab_gl) exports for
 * lib_ableem's Renderer (ABLEEM_RENDER_GL, render_backend.cpp loads it).
 *
 * A backend exports ONE function, ab_render_backend(), which returns its table. The table starts with its size and
 * version and only ever grows at the end: a caller checks `size` before it calls an entry past the ones it knows, and
 * a function whose meaning changes gets a new entry (name_v2) instead of a new meaning for the old one.
 *
 * Coordinates are output pixels, y down, from the top-left of what is drawn into: the screen's viewport, or a target
 * texture. Texture pixels are RGBA bytes (SDL_PIXELFORMAT_RGBA32), rows top first. The caller makes the window and the
 * GL context (window_needs says how) and swaps the buffers; the backend only draws, through the functions getProc
 * gives it. Everything is called on the thread the context is current on. */
#ifndef ABLEEM_RENDER_BACKEND_H
#define ABLEEM_RENDER_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABR_API_VERSION 1
#define ABR_EXPORT_NAME "ab_render_backend"

typedef struct AbrContext AbrContext; /* a backend made on a context (create) */
typedef struct AbrTexture AbrTexture; /* a texture of it */

typedef struct AbrRect {
    int32_t x, y, w, h;
} AbrRect;

typedef struct AbrFRect {
    float x, y, w, h;
} AbrFRect;

typedef struct AbrPoint {
    float x, y;
} AbrPoint;

/* a colour (or a tint: SDL's colour and alpha mod), 0..255 */
typedef struct AbrColor {
    uint8_t r, g, b, a;
} AbrColor;

/* one side of a trapezoid: where it stands, its top and bottom */
typedef struct AbrEdge {
    float x, top, bottom;
} AbrEdge;

/* SDL_Vertex's layout: position, colour, texture coordinate (0..1) */
typedef struct AbrVertex {
    float x, y;
    AbrColor color;
    float u, v;
} AbrVertex;

/* ableem::BlendMode's order */
enum AbrBlend {
    ABR_BLEND_NONE = 0,
    ABR_BLEND_BLEND = 1,
    ABR_BLEND_ADD = 2,
    ABR_BLEND_MOD = 3,
    ABR_BLEND_PREMULTIPLIED = 4,
    ABR_BLEND_MASK = 5
};

/* texture_create's flags */
enum AbrTextureFlags {
    ABR_TEX_TARGET = 1,       /* can be drawn into (set_target); made transparent black */
    ABR_TEX_NEAREST = 2,      /* nearest filtering (SDL's scale quality "0") */
    ABR_TEX_AUTO_MIPMAPS = 4  /* mipmaps made on the first draw that shrinks it by more than 1.5: covers, scaled art */
};

/* copy's flip */
enum AbrFlip { ABR_FLIP_NONE = 0, ABR_FLIP_HORIZONTAL = 1, ABR_FLIP_VERTICAL = 2 };

/* a GL context the window needs, SDL's SDL_GL_CONTEXT_PROFILE_* values */
enum AbrGlProfile { ABR_GL_CORE = 1, ABR_GL_COMPATIBILITY = 2, ABR_GL_ES = 4 };

typedef struct AbrGlVersion {
    int32_t profile, major, minor;
} AbrGlVersion;

/* what the window needs before it is made (window_needs) */
typedef struct AbrWindowNeeds {
    uint32_t size;   /* sizeof(AbrWindowNeeds), set by the caller */
    int32_t opengl;  /* 1: an SDL_WINDOW_OPENGL window with one of `versions` current on it */
    int32_t count;   /* how many of `versions` to try, in order */
    AbrGlVersion versions[4];
} AbrWindowNeeds;

typedef void *(*AbrGetProc)(const char *name);

typedef struct AbrApi {
    uint32_t size;    /* sizeof(AbrApi) of the backend: entries past it are not there */
    uint32_t version; /* ABR_API_VERSION it was built with */
    const char *name; /* "gles3" */

    void (*window_needs)(AbrWindowNeeds *out);
    /* the backend on the current context; NULL with the reason in err */
    AbrContext *(*create)(AbrGetProc getProc, char *err, int32_t errSize);
    void (*destroy)(AbrContext *c);
    /* one line for the log: GL_VERSION, GL_RENDERER, the extensions used; returns its length */
    int32_t (*describe)(AbrContext *c, char *out, int32_t size);

    /* a frame on the screen of w x h pixels starts (nothing cleared); end_frame after the swap */
    void (*begin_frame)(AbrContext *c, int32_t w, int32_t h);
    void (*end_frame)(AbrContext *c);
    /* the screen's viewport / the clip rect; NULL = none */
    void (*set_viewport)(AbrContext *c, const AbrRect *r);
    void (*set_clip)(AbrContext *c, const AbrRect *r);

    /* rgba may be NULL (undefined pixels, a target's transparent black); pitch in bytes */
    AbrTexture *(*texture_create)(AbrContext *c, int32_t w, int32_t h, const void *rgba, int32_t pitch, uint32_t flags);
    void (*texture_update)(AbrContext *c, AbrTexture *t, const AbrRect *r, const void *rgba, int32_t pitch);
    void (*texture_destroy)(AbrContext *c, AbrTexture *t);
    /* draws into t (NULL: the screen) from now on; 0 when t is no target */
    int32_t (*set_target)(AbrContext *c, AbrTexture *t);

    /* the whole of what is drawn into, past viewport and clip */
    void (*clear)(AbrContext *c, AbrColor color);
    void (*fill_rects)(AbrContext *c, const AbrFRect *rects, int32_t count, AbrColor color, int32_t blend);
    /* `count` lines, from points[2i] to points[2i+1], both ends drawn */
    void (*draw_lines)(AbrContext *c, const AbrPoint *points, int32_t count, AbrColor color, int32_t blend);
    /* src in texture pixels (NULL: all of it), turned `angle` degrees clockwise about center (relative to dst; NULL:
     * its middle), flip AbrFlip */
    void (*copy)(AbrContext *c, AbrTexture *t, const AbrFRect *src, const AbrFRect *dst, AbrColor tint,
                 int32_t blend, double angle, const AbrPoint *center, int32_t flip);
    /* src (texture pixels) onto the trapezoid between two vertical edges, perspective-correct, the tint going from
     * top to bottom, the source upside down with flipVertically; left.x > right.x mirrors it */
    void (*trapezoid)(AbrContext *c, AbrTexture *t, const AbrFRect *src, AbrEdge left, AbrEdge right, AbrColor top,
                      AbrColor bottom, int32_t flipVertically, int32_t blend);
    /* triangles (indices NULL: every three vertices), t NULL = untextured */
    void (*geometry)(AbrContext *c, AbrTexture *t, const AbrVertex *vertices, int32_t count, const int32_t *indices,
                     int32_t indexCount, int32_t blend);
    /* r (NULL: all) of what is drawn into, RGBA rows top first; 0 on failure */
    int32_t (*read_pixels)(AbrContext *c, const AbrRect *r, void *rgba, int32_t pitch);

    uint32_t (*draw_calls)(AbrContext *c);     /* this frame's */
    uint32_t (*textures_alive)(AbrContext *c); /* made and not destroyed */
} AbrApi;

typedef const AbrApi *(*AbrEntry)(void);

#ifdef __cplusplus
}
#endif

#endif
