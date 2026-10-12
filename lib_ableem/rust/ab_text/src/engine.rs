//! The engine: one FontSystem with the launcher's own fonts (no system font scan), layouts cached by font and
//! text, and the glyph atlas.
use crate::atlas::{Atlas, Slot};
use cosmic_text::{
    fontdb, Attrs, Buffer, CacheKey, Family, FontSystem, Metrics, Shaping, Stretch, Style, SwashCache,
    SwashContent, Weight, Wrap,
};
use std::collections::HashMap;
use std::sync::Arc;

/// A face at a size. Two loads of the same file at the same size share `key`, and so their cached layouts.
pub struct Font {
    pub key: u32,
    family: String,
    weight: Weight,
    style: Style,
    stretch: Stretch,
    pub px: f32,
    pub ascent: i32,
    pub line_height: i32,
}

impl Font {
    fn attrs(&self) -> Attrs<'_> {
        Attrs::new().family(Family::Name(&self.family)).weight(self.weight).style(self.style).stretch(self.stretch)
    }
}

/// A glyph of a laid-out text: what to rasterise and where its pen is (x relative to its line's start, y the
/// baseline from the text's top).
#[derive(Clone, Copy)]
struct Glyph {
    key: CacheKey,
    x: i32,
    y: i32,
    line: u32,
}

pub struct Layout {
    pub width: i32,
    pub line_widths: Vec<i32>,
    glyphs: Vec<Glyph>,
    pub missing: usize,
    pub lines_text: Vec<String>,
}

#[derive(Clone, PartialEq, Eq, Hash)]
struct LayoutKey {
    font: u32,
    wrap: i32,
    text: String,
}

/// Two generations of layouts: a lookup promotes from the old one, and when the new one is full the old one
/// goes - what was used in the last few hundred calls stays.
struct LayoutCache {
    new: HashMap<LayoutKey, Arc<Layout>>,
    old: HashMap<LayoutKey, Arc<Layout>>,
}

const LAYOUT_CACHE: usize = 1024;

impl LayoutCache {
    fn get(&mut self, k: &LayoutKey) -> Option<Arc<Layout>> {
        if let Some(l) = self.new.get(k) {
            return Some(l.clone());
        }
        let l = self.old.remove(k)?;
        self.put(k.clone(), l.clone());
        Some(l)
    }

    fn put(&mut self, k: LayoutKey, l: Arc<Layout>) {
        if self.new.len() >= LAYOUT_CACHE {
            self.old = std::mem::take(&mut self.new);
        }
        self.new.insert(k, l);
    }
}

pub struct Engine {
    fs: FontSystem,
    swash: SwashCache,
    files: HashMap<String, Vec<fontdb::ID>>,
    keys: HashMap<(String, u32), u32>,
    layouts: LayoutCache,
    pub atlas: Atlas<CacheKey>,
}

/// The quad of one glyph, relative to the text's top-left.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Quad {
    pub x: i32,
    pub y: i32,
    pub slot: Slot,
}

impl Engine {
    pub fn new() -> Engine {
        let fs = FontSystem::new_with_locale_and_db("en-US".into(), fontdb::Database::new());
        Engine {
            fs,
            swash: SwashCache::new(),
            files: HashMap::new(),
            keys: HashMap::new(),
            layouts: LayoutCache { new: HashMap::new(), old: HashMap::new() },
            atlas: Atlas::new(),
        }
    }

    /// The faces of a font file, loading (memory-mapping) it the first time.
    pub fn faces(&mut self, path: &str) -> Option<Vec<fontdb::ID>> {
        if let Some(ids) = self.files.get(path) {
            return Some(ids.clone());
        }
        let db = self.fs.db_mut();
        let before: Vec<fontdb::ID> = db.faces().map(|f| f.id).collect();
        db.load_font_file(path).ok()?;
        let ids: Vec<fontdb::ID> = db.faces().map(|f| f.id).filter(|id| !before.contains(id)).collect();
        if ids.is_empty() {
            return None;
        }
        // the font system remembers which faces matched which attributes; a new face changes the answer
        self.layouts = LayoutCache { new: HashMap::new(), old: HashMap::new() };
        self.files.insert(path.to_string(), ids.clone());
        Some(ids)
    }

    pub fn load(&mut self, path: &str, px: f32) -> Option<Font> {
        let id = *self.faces(path)?.first()?;
        let face = self.fs.db().face(id)?;
        let family = face.families.first()?.0.clone();
        let (weight, style, stretch) = (face.weight, face.style, face.stretch);
        let font = self.fs.get_font(id)?;
        let m = font.as_swash().metrics(&[]).scale(px);
        // SDL_ttf: ascent and descent each rounded up, the height their sum (FT_CEIL of ascender - descender)
        let ascent = m.ascent.ceil() as i32;
        let line_height = (m.ascent + m.descent).ceil() as i32;
        let n = self.keys.len() as u32;
        let key = *self.keys.entry((path.to_string(), px.to_bits())).or_insert(n);
        Some(Font { key, family, weight, style, stretch, px, ascent, line_height })
    }

    pub fn layout(&mut self, font: &Font, text: &str, wrap: i32) -> Arc<Layout> {
        let k = LayoutKey { font: font.key, wrap: wrap.max(0), text: text.to_string() };
        if let Some(l) = self.layouts.get(&k) {
            return l;
        }
        let l = Arc::new(self.shape(font, text, wrap));
        self.layouts.put(k, l.clone());
        l
    }

    fn shape(&mut self, font: &Font, text: &str, wrap: i32) -> Layout {
        let fs = &mut self.fs;
        let mut buf = Buffer::new(fs, Metrics::new(font.px, font.line_height.max(1) as f32));
        if wrap > 0 {
            buf.set_wrap(fs, Wrap::WordOrGlyph);
            buf.set_size(fs, Some(wrap as f32), None);
        } else {
            buf.set_wrap(fs, Wrap::None);
            buf.set_size(fs, None, None);
        }
        buf.set_text(fs, text, font.attrs(), Shaping::Advanced);
        buf.shape_until_scroll(fs, false);

        let mut glyphs = Vec::new();
        let mut line_widths = Vec::new();
        let mut lines_text = Vec::new();
        let mut missing = 0;
        for (line, run) in buf.layout_runs().enumerate() {
            // the baseline sits `ascent` below each line's top, whatever fallback face is on it - as SDL_ttf
            let baseline = line as i32 * font.line_height + font.ascent;
            let start = run.glyphs.first().map_or(0, |g| g.start);
            let end = run.glyphs.last().map_or(0, |g| g.end);
            lines_text.push(run.text[start.min(end)..end].trim_end().to_string());
            line_widths.push(run.line_w.ceil() as i32);
            for g in run.glyphs.iter() {
                if g.glyph_id == 0 && !run.text[g.start..g.end].trim().is_empty() {
                    missing += run.text[g.start..g.end].chars().count();
                }
                let p = g.physical((0.0, baseline as f32), 1.0);
                glyphs.push(Glyph { key: p.cache_key, x: p.x, y: p.y, line: line as u32 });
            }
        }
        if line_widths.is_empty() {
            line_widths.push(0);
            lines_text.push(String::new());
        }
        let width = line_widths.iter().copied().max().unwrap_or(0);
        Layout { width, line_widths, glyphs, missing, lines_text }
    }

    /// The glyph quads of a text, every glyph in the atlas. Lines are aligned within `box_w`.
    pub fn quads(&mut self, font: &Font, text: &str, wrap: i32, align: i32) -> Vec<Quad> {
        let layout = self.layout(font, text, wrap);
        let box_w = if wrap > 0 { wrap } else { layout.width };
        loop {
            let generation = self.atlas.generation();
            let mut out = Vec::with_capacity(layout.glyphs.len());
            for g in layout.glyphs.iter() {
                let lw = layout.line_widths[g.line as usize];
                let shift = match align {
                    1 => (box_w - lw) / 2,
                    2 => box_w - lw,
                    _ => 0,
                };
                let (fs, swash) = (&mut self.fs, &mut self.swash);
                let slot = self.atlas.get(g.key, || {
                    let img = swash.get_image_uncached(fs, g.key)?;
                    let (w, h) = (img.placement.width, img.placement.height);
                    let cov = match img.content {
                        SwashContent::Mask => img.data,
                        // a colour glyph (an emoji) keeps its shape; a subpixel mask its average
                        SwashContent::Color => img.data.chunks_exact(4).map(|p| p[3]).collect(),
                        SwashContent::SubpixelMask => img
                            .data
                            .chunks_exact(4)
                            .map(|p| ((p[0] as u16 + p[1] as u16 + p[2] as u16) / 3) as u8)
                            .collect(),
                    };
                    Some((img.placement.left, img.placement.top, w, h, cov))
                });
                if let Some(slot) = slot {
                    out.push(Quad { x: g.x + shift + slot.left, y: g.y - slot.top, slot });
                }
            }
            // the atlas was emptied half way: the first slots are gone, lay it all out again (once is enough -
            // one text never fills four pages)
            if self.atlas.generation() == generation {
                return out;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The launcher's fonts: AB_TEXT_FONTS, else the launcher tree this core is a submodule of.
    fn fonts_dir() -> std::path::PathBuf {
        if let Ok(d) = std::env::var("AB_TEXT_FONTS") {
            return d.into();
        }
        let d = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../../src/resources/fonts");
        assert!(d.is_dir(), "no fonts at {} - set AB_TEXT_FONTS to the launcher's src/resources/fonts", d.display());
        d
    }

    fn engine_with(primary: &str, fallbacks: &[&str]) -> (Engine, Font) {
        let mut e = Engine::new();
        for f in fallbacks {
            e.faces(fonts_dir().join(f).to_str().unwrap()).expect("fallback font");
        }
        let font = e.load(fonts_dir().join(primary).to_str().unwrap(), 22.0).expect("font");
        (e, font)
    }

    #[test]
    fn metrics_are_sdl_ttf_like() {
        let (_, f) = engine_with("RedHatText-Medium.ttf", &[]);
        assert!(f.ascent > 15 && f.ascent < 25, "ascent {}", f.ascent);
        assert!(f.line_height > f.ascent && f.line_height < 35, "line height {}", f.line_height);
    }

    #[test]
    fn widths_grow_with_text_and_lines_split_at_newlines() {
        let (mut e, f) = engine_with("RedHatText-Medium.ttf", &[]);
        let a = e.layout(&f, "Polski", 0).width;
        let b = e.layout(&f, "Polski język", 0).width;
        assert!(a > 30 && b > a);
        let two = e.layout(&f, "Polski\njęzyk", 0);
        assert_eq!(two.line_widths.len(), 2);
        assert_eq!(two.width, a.max(e.layout(&f, "język", 0).width));
    }

    #[test]
    fn wrapping_keeps_lines_inside_the_width() {
        let (mut e, f) = engine_with("RedHatText-Medium.ttf", &[]);
        let l = e.layout(&f, "The quick brown fox jumps over the lazy dog again and again", 150);
        assert!(l.line_widths.len() >= 3);
        assert!(l.line_widths.iter().all(|w| *w <= 150), "{:?}", l.line_widths);
    }

    #[test]
    fn chinese_breaks_between_characters_and_falls_back_to_noto() {
        let (mut e, f) = engine_with("RedHatText-Medium.ttf", &["NotoSansSC-Regular.otf"]);
        let text = "选择游戏后按下确认键开始游戏选择游戏后按下确认键开始游戏";
        let l = e.layout(&f, text, 200);
        assert_eq!(l.missing, 0);
        assert!(l.line_widths.len() >= 2);
        assert!(l.line_widths.iter().all(|w| *w <= 200));
    }

    #[test]
    fn line_breaks_follow_uax14() {
        assert_eq!(crate::line_breaks("Hello big world"), vec![6, 10]);
        // between ideographs, but never before the full stop or the comma
        let zh = "开始游戏。好，";
        let b = crate::line_breaks(zh);
        let at = |s: &str| zh.find(s).unwrap() as u32;
        assert!(b.contains(&at("始")) && b.contains(&at("游")));
        assert!(!b.contains(&at("。")) && !b.contains(&at("，")));
        assert!(crate::line_breaks("").is_empty());
    }

    #[test]
    fn a_missing_script_is_counted_without_a_fallback() {
        let (mut e, f) = engine_with("RedHatText-Medium.ttf", &[]);
        assert_eq!(e.layout(&f, "ok 游戏", 0).missing, 2);
    }

    #[test]
    fn cyrillic_falls_back_to_open_sans() {
        let (mut e, f) = engine_with("RedHatText-Medium.ttf", &["NotoSansSC-Regular.otf", "OpenSans-Medium.ttf"]);
        assert_eq!(e.layout(&f, "Гран Туризмо", 0).missing, 0);
    }

    #[test]
    fn quads_are_aligned_within_the_widest_line() {
        let (mut e, f) = engine_with("RedHatText-Medium.ttf", &[]);
        let text = "Wide line here\nI";
        let left = e.quads(&f, text, 0, 0);
        let right = e.quads(&f, text, 0, 2);
        let last = |q: &Vec<Quad>| *q.last().unwrap();
        assert!(last(&right).x > last(&left).x + 50);
        assert_eq!(left[0], right[0]); // the widest line does not move
    }

    #[test]
    fn quads_sit_on_the_baseline_of_their_line() {
        let (mut e, f) = engine_with("RedHatText-Medium.ttf", &[]);
        let q = e.quads(&f, "x\nx", 0, 0);
        assert_eq!(q.len(), 2);
        assert_eq!(q[1].y - q[0].y, f.line_height);
        assert!(q[0].y >= 0 && q[0].y + q[0].slot.rect.h as i32 <= f.ascent + 1);
    }
}
