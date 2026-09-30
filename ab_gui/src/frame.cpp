// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui frames: the 9-slice pieces, their drawing and the FrameSet. See the header.
//
#include <ab_gui/frame.h>

#include <ableem/engine/log.h>

#include <cmath>

using namespace std;
using ableem::Rect;
using ableem::Size;

namespace abgui {

namespace {
int toImage(int logical, float scale) {
    return static_cast<int>(lround(logical * scale));
}

// a side's two corners in `length` logical px: as sliced, or shrunk in proportion when they do not fit
void corners(int first, int second, int length, int &outFirst, int &outSecond) {
    outFirst = first;
    outSecond = second;
    if (first + second > length) {
        outFirst = first + second > 0 ? length * first / (first + second) : 0;
        outSecond = length - outFirst;
    }
}
} // namespace

//*******************************
// frameFits / framePieces
//*******************************
bool frameFits(const Size &image, float imageScale, const Insets &slice) {
    if (slice.left < 0 || slice.top < 0 || slice.right < 0 || slice.bottom < 0 || imageScale <= 0.0f)
        return false;
    // at least one pixel between the cut lines both ways: the edges and the centre are stretched from it
    return toImage(slice.left, imageScale) + toImage(slice.right, imageScale) < image.w &&
           toImage(slice.top, imageScale) + toImage(slice.bottom, imageScale) < image.h;
}

vector<FramePiece> framePieces(const Size &image, float imageScale, const Insets &slice, const Insets &bleed, bool fill,
                               const Rect &box) {
    vector<FramePiece> pieces;
    const Rect outer(box.x - bleed.left, box.y - bleed.top, box.w + bleed.left + bleed.right,
                     box.h + bleed.top + bleed.bottom);
    if (outer.w <= 0 || outer.h <= 0 || !frameFits(image, imageScale, slice))
        return pieces;

    // the image's columns and rows, in its own pixels
    const int sl = toImage(slice.left, imageScale), sr = toImage(slice.right, imageScale);
    const int st = toImage(slice.top, imageScale), sb = toImage(slice.bottom, imageScale);
    const int srcX[3] = {0, sl, image.w - sr};
    const int srcW[3] = {sl, image.w - sl - sr, sr};
    const int srcY[3] = {0, st, image.h - sb};
    const int srcH[3] = {st, image.h - st - sb, sb};

    // the box's columns and rows, logical
    int dl, dr, dt, db;
    corners(slice.left, slice.right, outer.w, dl, dr);
    corners(slice.top, slice.bottom, outer.h, dt, db);
    const int dstX[3] = {outer.x, outer.x + dl, outer.x + outer.w - dr};
    const int dstW[3] = {dl, outer.w - dl - dr, dr};
    const int dstY[3] = {outer.y, outer.y + dt, outer.y + outer.h - db};
    const int dstH[3] = {dt, outer.h - dt - db, db};

    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) {
            if (row == 1 && col == 1 && !fill)
                continue;
            if (srcW[col] <= 0 || srcH[row] <= 0 || dstW[col] <= 0 || dstH[row] <= 0)
                continue;
            FramePiece p;
            p.src = Rect(srcX[col], srcY[row], srcW[col], srcH[row]);
            p.dst = Rect(dstX[col], dstY[row], dstW[col], dstH[row]);
            pieces.push_back(p);
        }
    return pieces;
}

//*******************************
// drawFrame
//*******************************
void drawFrame(ableem::Renderer &renderer, const Frame &frame, const Rect &box, const ableem::Color &tint) {
    drawFrame(renderer, frame, box, tint, 255);
}

void drawFrame(ableem::Renderer &renderer, const Frame &frame, const Rect &box, const ableem::Color &tint,
               unsigned char alpha) {
    if (!frame.valid())
        return;
    ableem::Texture texture = frame.texture; // a handle: the colour and alpha mods are the texture's own
    texture.setColorMod(tint);
    texture.setAlphaMod(alpha);
    for (const FramePiece &p : framePieces(texture.size(), frame.imageScale, frame.slice, frame.bleed, frame.fill, box))
        renderer.copy(texture, &p.src, &p.dst);
}

//*******************************
// pickImageFile
//*******************************
string pickImageFile(const string &file, const string &file2x, float outputScale, float &scale) {
    if (outputScale > 1.0f && !file2x.empty()) {
        scale = 2.0f;
        return file2x;
    }
    if (!file.empty()) {
        scale = 1.0f;
        return file;
    }
    scale = 2.0f;
    if (!file2x.empty())
        return file2x;
    scale = 1.0f;
    return "";
}

//*******************************
// FrameSet
//*******************************
void FrameSet::assign(const map<string, FrameSpec> &specs) {
    entries_.clear();
    for (const auto &s : specs) {
        Entry e;
        e.spec = s.second;
        entries_[s.first] = e;
    }
}

void FrameSet::release() {
    for (auto &e : entries_) {
        e.second.loaded = Frame();
        e.second.tried = false;
    }
}

string FrameSet::pickFile(const FrameSpec &spec, float outputScale, float &scale) {
    return pickImageFile(spec.file, spec.file2x, outputScale, scale);
}

Frame FrameSet::frame(ableem::Renderer &renderer, const string &name) {
    auto it = entries_.find(name);
    if (it == entries_.end())
        return Frame();
    Entry &e = it->second;
    if (!e.tried) {
        e.tried = true; // one try per load: a bad file is logged once, not every frame
        float scale = 1.0f;
        const string file = pickFile(e.spec, renderer.outputScale(), scale);
        ableem::Texture texture = ableem::Texture::loadFile(renderer, file);
        if (texture.valid() && frameFits(texture.size(), scale, e.spec.slice)) {
            texture.setBlendMode(ableem::BlendMode::Blend);
            e.loaded.texture = texture;
            e.loaded.imageScale = scale;
            e.loaded.slice = e.spec.slice;
            e.loaded.bleed = e.spec.bleed;
            e.loaded.fill = e.spec.fill;
            e.loaded.tint = e.spec.tint;
        } else if (texture.valid()) {
            PLOG_WARNING << "Frame '" << name << "': " << file << " is smaller than its slices - drawn by the code";
        }
    }
    return e.loaded;
}

} // namespace abgui
