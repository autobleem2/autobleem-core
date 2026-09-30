// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui frames (docs/ab-gui-plan.md, G4): a 9-slice image a style draws a box with - a panel, a selection, a key -
// instead of its code-drawn shape. The image is cut by four lines (`slice`, measured from its outer edge) into four
// corners drawn 1:1, four edges stretched along their length and a centre stretched both ways (left out with `fill`
// false); it may reach outside the box by `bleed` (a glow, a shadow). Every number is logical (the 1280x720 canvas);
// an @2x image carries twice the pixels for the same numbers and is drawn when the output scale is above 1. `tint`
// names a colour of the Style the image is multiplied by (Style::colorByName), so a white frame follows the theme.
// The artist's side is docs/ab-gui-frames-spec.md.
//
// A program describes its frames as FrameSpecs by name and keeps them in a FrameSet, which loads each image the first
// time it is asked for and drops the textures when the display goes (release()); the Context hands them out through
// its frameProvider, and Style::drawFrame draws one. No frame by that name = the primitive draws its old shape.
//
#pragma once

#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>
#include <ableem/ui/types.h>

#include <map>
#include <string>
#include <vector>

namespace abgui {

// four logical lengths, one per side
struct Insets {
    int left = 0, top = 0, right = 0, bottom = 0;

    Insets() = default;
    Insets(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}
    static Insets all(int n) { return Insets(n, n, n, n); }
};

// a frame as a program describes it: the files are absolute paths ("" = none of that size)
struct FrameSpec {
    std::string file;   // the 1x image
    std::string file2x; // the @2x image
    Insets slice;
    Insets bleed;
    bool fill = true;
    std::string tint; // a Style colour's name (Style::colorByName); "" the image's own colours
    // the tint as the program resolved it, for a name that is not a Style colour (`selection`, a launcher.colors
    // colour Style has no role for - G5k): used when Style::colorByName does not know `tint`
    bool tintResolved = false;
    ableem::Color tintColor = ableem::Color(255, 255, 255, 255);
};

// a frame ready to draw; invalid (no texture) when there is none or its image could not be used
struct Frame {
    ableem::Texture texture;
    float imageScale = 1.0f; // image pixels per logical pixel: 1 for a 1x image, 2 for an @2x one
    Insets slice;
    Insets bleed;
    bool fill = true;
    std::string tint;
    bool tintResolved = false;
    ableem::Color tintColor = ableem::Color(255, 255, 255, 255);

    bool valid() const { return texture.valid(); }
};

// one piece of the 9-slice: from `src` (image pixels) to `dst` (logical)
struct FramePiece {
    ableem::Rect src, dst;
};

// The pieces that draw an image of `image` pixels (imageScale pixels per logical one) cut by `slice` into `box` grown
// by `bleed`: up to nine, the empty ones left out, the centre only with `fill`. A box too small for the two corners
// of a side shrinks them in proportion; an image smaller than its slices gives no pieces (nothing to draw).
std::vector<FramePiece> framePieces(const ableem::Size &image, float imageScale, const Insets &slice,
                                    const Insets &bleed, bool fill, const ableem::Rect &box);

// whether the image is big enough for its slices (the check a FrameSet makes on load)
bool frameFits(const ableem::Size &image, float imageScale, const Insets &slice);

// draws `frame` into `box` (grown by its bleed) with `tint` multiplying its colours (white: its own)
void drawFrame(ableem::Renderer &renderer, const Frame &frame, const ableem::Rect &box,
               const ableem::Color &tint = ableem::Color(255, 255, 255, 255));
// the same at `alpha` (255: as the image is; a glow fading with a scroll - G5a). The texture is shared by every draw of
// the frame, so every draw sets its alpha (the one above 255)
void drawFrame(ableem::Renderer &renderer, const Frame &frame, const ableem::Rect &box, const ableem::Color &tint,
               unsigned char alpha);

// `frame` with its slices and bleed scaled by `factor` (G5k: the cover glow follows the cover, size / 222): the same
// image cut at the same pixels, drawn into a box whose corners and glow are `factor` times as big. Logical lengths are
// rounded; a factor of 1 (or less than or equal to 0) gives `frame` as it is.
Frame scaledFrame(const Frame &frame, float factor);

// which of an image's two files to load at `outputScale` - what FrameSet and IconSet (icon.h) both pick by: the @2x one
// above scale 1 when there is one, else the 1x one, else the @2x one (the GPU scales it down); `scale` gets its image
// pixels per logical pixel. "" when neither is set
std::string pickImageFile(const std::string &file, const std::string &file2x, float outputScale, float &scale);

//********************
// FrameSet
//********************
class FrameSet {
public:
    // the frames by name (the old ones and their textures dropped)
    void assign(const std::map<std::string, FrameSpec> &specs);
    // drops every loaded texture and keeps the specs - before the renderer goes (a display release); the next
    // frame() loads again
    void release();
    bool empty() const { return entries_.empty(); }
    bool has(const std::string &name) const { return entries_.count(name) != 0; }

    // the frame `name`, its image loaded on the first call (the @2x one when pickFile says so); an invalid Frame when
    // there is no such frame, or its image does not load or is smaller than its slices (logged once, then remembered)
    Frame frame(ableem::Renderer &renderer, const std::string &name);

    // which file to draw at `outputScale` (pickImageFile): the @2x one above scale 1 when there is one, else the 1x
    // one, else the @2x one (the GPU scales it down); `scale` gets its image pixels per logical pixel. "" when neither
    // is set
    static std::string pickFile(const FrameSpec &spec, float outputScale, float &scale);

private:
    struct Entry {
        FrameSpec spec;
        Frame loaded;
        bool tried = false;
    };
    std::map<std::string, Entry> entries_;
};

} // namespace abgui
