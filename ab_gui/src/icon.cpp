// SPDX-License-Identifier: GPL-3.0-or-later
//
// abgui icons: loading one, its halo, and the IconSet. See the header.
//
#include <ab_gui/icon.h>

#include <ab_gui/frame.h>
#include <ab_gui/style.h>

#include <ableem/engine/log.h>

using namespace std;

namespace abgui {

//*******************************
// loadIcon / loadIconHalo
//*******************************
ableem::Texture loadIcon(ableem::Renderer &renderer, const IconSpec &spec) {
    float scale = 1.0f;
    const string file = pickImageFile(spec.file, spec.file2x, renderer.outputScale(), scale);
    if (file.empty())
        return ableem::Texture();
    if (scale == 1.0f)
        return ableem::Texture::loadFile(renderer, file); // the 1x file: the very call a theme image always made
    return ableem::Texture::loadFile(renderer, file, scale);
}

ableem::Texture loadIconHalo(ableem::Renderer &renderer, const IconSpec &spec) {
    const string &file = spec.file.empty() ? spec.file2x : spec.file;
    if (file.empty())
        return ableem::Texture();
    // a loaded texture cannot be read back pixel by pixel: the outline is made from a fresh decode of the file
    return Style::outlineOf(renderer, ableem::Image::loadFile(file));
}

//*******************************
// IconSet
//*******************************
void IconSet::assign(const map<string, IconSpec> &specs, bool halo) {
    entries_.clear();
    for (const auto &s : specs) {
        Entry e;
        e.spec = s.second;
        entries_[s.first] = e;
    }
    halo_ = halo;
}

void IconSet::release() {
    for (auto &e : entries_) {
        e.second.icon = ableem::Texture();
        e.second.halo = ableem::Texture();
        e.second.triedIcon = e.second.triedHalo = false;
    }
}

IconSpec IconSet::spec(const string &name) const {
    auto it = entries_.find(name);
    return it == entries_.end() ? IconSpec() : it->second.spec;
}

string IconSet::pickFile(const IconSpec &spec, float outputScale, float &scale) {
    return pickImageFile(spec.file, spec.file2x, outputScale, scale);
}

ableem::Texture IconSet::icon(ableem::Renderer &renderer, const string &name) {
    auto it = entries_.find(name);
    if (it == entries_.end())
        return ableem::Texture();
    Entry &e = it->second;
    if (!e.triedIcon) {
        e.triedIcon = true; // one try per load: a bad file is logged once, not every frame
        e.icon = loadIcon(renderer, e.spec);
        if (!e.icon.valid()) {
            PLOG_WARNING << "Icon '" << name << "': " << (e.spec.file.empty() ? e.spec.file2x : e.spec.file)
                         << " does not load";
        }
    }
    return e.icon;
}

ableem::Texture IconSet::halo(ableem::Renderer &renderer, const string &name) {
    if (!halo_)
        return ableem::Texture();
    auto it = entries_.find(name);
    if (it == entries_.end())
        return ableem::Texture();
    Entry &e = it->second;
    if (!e.triedHalo) {
        e.triedHalo = true;
        e.halo = loadIconHalo(renderer, e.spec);
    }
    return e.halo;
}

} // namespace abgui
