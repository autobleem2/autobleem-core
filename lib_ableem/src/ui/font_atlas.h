#pragma once
// Internal-only header: the hook between Renderer and Font's glyph atlas.

namespace ableem {
// the SDL renderer is about to go: the atlas's textures go with it (they are made again, and the glyphs uploaded
// again, on the next renderer's first text)
void fontAtlasRelease();
} // namespace ableem
