//! The glyph atlas: square pages of coverage (one byte per pixel) packed in shelves. The UI side mirrors each page
//! in a texture and uploads the part that changed; when MAX_PAGES are full everything is dropped and the glyphs
//! of the next frames come back one by one (the generation tells a caller its slots went stale).
use std::collections::HashMap;
use std::hash::Hash;

pub const PAGE_SIZE: u32 = 1024;
pub const MAX_PAGES: usize = 4;
const PAD: u32 = 1; // empty pixels between glyphs, so linear filtering never picks a neighbour

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Rect {
    pub x: u32,
    pub y: u32,
    pub w: u32,
    pub h: u32,
}

impl Rect {
    fn union(self, o: Rect) -> Rect {
        let x = self.x.min(o.x);
        let y = self.y.min(o.y);
        let r = (self.x + self.w).max(o.x + o.w);
        let b = (self.y + self.h).max(o.y + o.h);
        Rect { x, y, w: r - x, h: b - y }
    }
}

/// Where a glyph's bitmap is: its page and rectangle, and its offset from the pen position (left, -top).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Slot {
    pub page: u16,
    pub rect: Rect,
    pub left: i32,
    pub top: i32,
}

struct Shelf {
    y: u32,
    h: u32,
    x: u32,
}

struct Page {
    pixels: Vec<u8>,
    shelves: Vec<Shelf>,
    next_y: u32,
    dirty: Option<Rect>,
}

impl Page {
    fn new() -> Page {
        // a new texture's pixels are undefined: the first upload is the whole (empty) page
        let all = Rect { x: 0, y: 0, w: PAGE_SIZE, h: PAGE_SIZE };
        Page { pixels: vec![0; (PAGE_SIZE * PAGE_SIZE) as usize], shelves: Vec::new(), next_y: 0, dirty: Some(all) }
    }

    /// A shelf no more than a third taller than the glyph with room left, else a new shelf below the last.
    fn place(&mut self, w: u32, h: u32) -> Option<(u32, u32)> {
        let (pw, ph) = (w + PAD, h + PAD);
        if pw > PAGE_SIZE || ph > PAGE_SIZE {
            return None;
        }
        for s in self.shelves.iter_mut() {
            if s.h >= ph && s.h <= ph + ph / 3 + 2 && PAGE_SIZE - s.x >= pw {
                let at = (s.x, s.y);
                s.x += pw;
                return Some(at);
            }
        }
        if PAGE_SIZE - self.next_y < ph {
            return None;
        }
        let y = self.next_y;
        self.next_y += ph;
        self.shelves.push(Shelf { y, h: ph, x: pw });
        Some((0, y))
    }

    fn blit(&mut self, x: u32, y: u32, w: u32, h: u32, coverage: &[u8]) {
        for row in 0..h {
            let src = &coverage[(row * w) as usize..((row + 1) * w) as usize];
            let at = ((y + row) * PAGE_SIZE + x) as usize;
            self.pixels[at..at + w as usize].copy_from_slice(src);
        }
        let r = Rect { x, y, w, h };
        self.dirty = Some(self.dirty.map_or(r, |d| d.union(r)));
    }
}

pub struct Atlas<K> {
    pages: Vec<Page>,
    slots: HashMap<K, Option<Slot>>,
    generation: u32,
}

impl<K: Hash + Eq + Copy> Atlas<K> {
    pub fn new() -> Atlas<K> {
        Atlas { pages: Vec::new(), slots: HashMap::new(), generation: 0 }
    }

    pub fn pages(&self) -> usize {
        self.pages.len()
    }

    /// Bumped every time the atlas is emptied: slots handed out before are no longer valid.
    pub fn generation(&self) -> u32 {
        self.generation
    }

    /// The glyph's slot, rasterising it through `raster` the first time (None = a glyph with no ink, a space).
    /// `raster` gives (left, top, width, height, coverage).
    pub fn get<F>(&mut self, key: K, raster: F) -> Option<Slot>
    where
        F: FnOnce() -> Option<(i32, i32, u32, u32, Vec<u8>)>,
    {
        if let Some(s) = self.slots.get(&key) {
            return *s;
        }
        let slot = match raster() {
            Some((left, top, w, h, cov)) if w > 0 && h > 0 => self.insert(left, top, w, h, &cov),
            _ => None,
        };
        self.slots.insert(key, slot);
        slot
    }

    fn insert(&mut self, left: i32, top: i32, w: u32, h: u32, cov: &[u8]) -> Option<Slot> {
        for attempt in 0..2 {
            for (i, page) in self.pages.iter_mut().enumerate() {
                if let Some((x, y)) = page.place(w, h) {
                    page.blit(x, y, w, h, cov);
                    return Some(Slot { page: i as u16, rect: Rect { x, y, w, h }, left, top });
                }
            }
            if self.pages.len() < MAX_PAGES {
                let mut page = Page::new();
                let (x, y) = page.place(w, h)?; // bigger than a page: never drawn
                page.blit(x, y, w, h, cov);
                self.pages.push(page);
                return Some(Slot { page: (self.pages.len() - 1) as u16, rect: Rect { x, y, w, h }, left, top });
            }
            if attempt == 0 {
                self.clear();
            }
        }
        None
    }

    /// Drops every glyph; the pages stay allocated and are cleared.
    pub fn clear(&mut self) {
        self.slots.clear();
        for p in self.pages.iter_mut() {
            p.pixels.fill(0);
            p.shelves.clear();
            p.next_y = 0;
            p.dirty = Some(Rect { x: 0, y: 0, w: PAGE_SIZE, h: PAGE_SIZE });
        }
        self.generation = self.generation.wrapping_add(1);
    }

    pub fn all_dirty(&mut self) {
        for p in self.pages.iter_mut() {
            p.dirty = Some(Rect { x: 0, y: 0, w: PAGE_SIZE, h: PAGE_SIZE });
        }
    }

    /// The page's changed rectangle, once, and its pixels as white RGBA into `dst` at the same position
    /// (`pitch` bytes per row of a whole page).
    pub fn take_dirty(&mut self, page: usize, dst: &mut [u8], pitch: usize) -> Option<Rect> {
        let p = self.pages.get_mut(page)?;
        let r = p.dirty.take()?;
        for y in r.y..r.y + r.h {
            let src = &p.pixels[(y * PAGE_SIZE + r.x) as usize..(y * PAGE_SIZE + r.x + r.w) as usize];
            let row = &mut dst[y as usize * pitch + r.x as usize * 4..y as usize * pitch + (r.x + r.w) as usize * 4];
            for (px, a) in row.chunks_exact_mut(4).zip(src) {
                px.copy_from_slice(&[255, 255, 255, *a]);
            }
        }
        Some(r)
    }

    #[cfg(test)]
    pub fn coverage_at(&self, page: usize, x: u32, y: u32) -> u8 {
        self.pages[page].pixels[(y * PAGE_SIZE + x) as usize]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn glyph(w: u32, h: u32, v: u8) -> Option<(i32, i32, u32, u32, Vec<u8>)> {
        Some((1, h as i32, w, h, vec![v; (w * h) as usize]))
    }

    #[test]
    fn a_glyph_is_rasterised_once_and_kept() {
        let mut a: Atlas<u32> = Atlas::new();
        let s = a.get(7, || glyph(10, 12, 200)).unwrap();
        let again = a.get(7, || panic!("rasterised twice")).unwrap();
        assert_eq!(s, again);
        assert_eq!(a.coverage_at(0, s.rect.x + 3, s.rect.y + 3), 200);
    }

    #[test]
    fn glyphs_do_not_overlap() {
        let mut a: Atlas<u32> = Atlas::new();
        let mut rects = Vec::new();
        for k in 0..500u32 {
            let s = a.get(k, || glyph(5 + k % 23, 8 + k % 17, 1)).unwrap();
            rects.push((s.page, s.rect));
        }
        for (i, (pa, a)) in rects.iter().enumerate() {
            for (pb, b) in rects.iter().skip(i + 1) {
                let apart = pa != pb || a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
                assert!(apart, "{a:?} overlaps {b:?}");
            }
        }
    }

    #[test]
    fn an_empty_glyph_takes_no_room() {
        let mut a: Atlas<u32> = Atlas::new();
        assert!(a.get(1, || None).is_none());
        assert!(a.get(2, || glyph(0, 0, 0)).is_none());
        assert_eq!(a.pages(), 0);
    }

    #[test]
    fn full_pages_are_cleared_and_the_generation_moves() {
        let mut a: Atlas<u32> = Atlas::new();
        let g = a.generation();
        let mut k = 0;
        while a.generation() == g {
            a.get(k, || glyph(200, 200, 9)).unwrap();
            k += 1;
            assert!(k < 1000);
        }
        assert_eq!(a.pages(), MAX_PAGES);
        assert!(a.get(k - 1, || panic!("the last glyph is kept after the clear")).is_some());
        assert!(a.get(0, || None).is_none()); // the first one went with the clear (rasterised again)
    }

    #[test]
    fn the_dirty_part_comes_out_once_as_white_rgba() {
        let mut a: Atlas<u32> = Atlas::new();
        let pitch = PAGE_SIZE as usize * 4;
        let mut dst = vec![0u8; pitch * PAGE_SIZE as usize];
        a.get(1, || glyph(4, 3, 77)).unwrap();
        assert_eq!(a.take_dirty(0, &mut dst, pitch).unwrap().w, PAGE_SIZE); // a new page comes out whole
        let s = a.get(2, || glyph(4, 3, 77)).unwrap();
        let r = a.take_dirty(0, &mut dst, pitch).unwrap();
        assert_eq!(r, s.rect);
        let at = r.y as usize * pitch + r.x as usize * 4;
        assert_eq!(&dst[at..at + 4], &[255, 255, 255, 77]);
        assert!(a.take_dirty(0, &mut dst, pitch).is_none());
        a.all_dirty();
        assert_eq!(a.take_dirty(0, &mut dst, pitch).unwrap().w, PAGE_SIZE);
    }
}
