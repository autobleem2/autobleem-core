// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui icons (docs/ab-gui-plan.md, G5a): fixed images by name - a d-pad arrow, a meta-row badge, a tab's picture -
// drawn at their own size, never stretched (a frame, frame.h, is the 9-slice kind). Every number is logical (the
// 1280x720 canvas): an icon's 1x file has its logical size, its @2x twin twice the pixels, loaded above output scale 1
// with pixel scale 2 (Texture::loadFile(renderer, path, pixelScale)) so its size() is the 1x one's and a caller never
// knows which was drawn. Under an icon a program may draw its halo: a dark outline made from the icon's own alpha
// (Style::outlineOf, UIREV-27) so a light icon reads on a light background - a theme whose icons carry their own glow
// switches it off (theme.json "iconHalo": false).
//
// A program describes its icons as IconSpecs by name and keeps them in an IconSet - FrameSet's twin - which loads each
// texture (and its halo) the first time it is asked for and drops them when the display goes (release()); the Context
// hands them out through its iconProvider/iconHaloProvider. Unlike frames, which icon a name means is the program's to
// resolve before assign(): AutoBleem's is the theme's own entry, else the default theme's, else its built-in file
// (UIREV-30), so a theme may replace one icon and keep the rest. The artist's side is docs/ab-gui-evoui-art-spec.md.
//
#pragma once

#include <ableem/ui/renderer.h>
#include <ableem/ui/texture.h>

#include <map>
#include <string>

namespace abgui {

// an icon as a program describes it: the files are absolute paths ("" = none of that size)
struct IconSpec {
    std::string file;   // the 1x image - its size is the icon's logical size
    std::string file2x; // the @2x image (twice the pixels)
};

// The icon's texture at the renderer's output scale: the @2x file above scale 1 when there is one (pixel scale 2,
// size() logical), else the 1x file loaded exactly as Texture::loadFile(renderer, file) always did, else the @2x one.
// Invalid when neither loads.
ableem::Texture loadIcon(ableem::Renderer &renderer, const IconSpec &spec);
// The icon's halo (Style::outlineOf of the 1x file - the outline is placed by the icon's logical size, so it is made
// from the 1x pixels whichever file is drawn; from the @2x one when there is no 1x). Invalid when there is no image.
ableem::Texture loadIconHalo(ableem::Renderer &renderer, const IconSpec &spec);

//********************
// IconSet
//********************
class IconSet {
public:
    // the icons by name (the old ones and their textures dropped), and whether their halos are drawn
    void assign(const std::map<std::string, IconSpec> &specs, bool halo = true);
    // drops every loaded texture and halo and keeps the specs - before the renderer goes (a display release); the next
    // icon()/halo() loads again
    void release();
    bool empty() const { return entries_.empty(); }
    bool has(const std::string &name) const { return entries_.count(name) != 0; }
    // the spec `name` was assigned (an empty one when there is none)
    IconSpec spec(const std::string &name) const;
    // whether the halos are drawn (assign's `halo`)
    bool haloOn() const { return halo_; }

    // the icon `name`, loaded on the first call (loadIcon); an invalid texture when there is no such icon or its file
    // does not load (logged once, then remembered)
    ableem::Texture icon(ableem::Renderer &renderer, const std::string &name);
    // its halo, made on the first call (loadIconHalo); an invalid texture when the halos are off, or there is no icon
    ableem::Texture halo(ableem::Renderer &renderer, const std::string &name);

    // which file to draw at `outputScale` (pickImageFile, frame.h - the rule FrameSet picks by)
    static std::string pickFile(const IconSpec &spec, float outputScale, float &scale);

private:
    struct Entry {
        IconSpec spec;
        ableem::Texture icon;
        ableem::Texture halo;
        bool triedIcon = false;
        bool triedHalo = false;
    };
    std::map<std::string, Entry> entries_;
    bool halo_ = true;
};

} // namespace abgui
