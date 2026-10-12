//! ab_text - lib_ableem's text engine: cosmic-text's shaping, font fallback and line breaking, and a glyph atlas,
//! behind the C ABI of include/ab_text.h. The C++ side (src/ui/font.cpp) owns the textures and draws.
//!
//! The border stays small and dumb: pointers + lengths in, plain structs out, caller-provided buffers. Nothing
//! unwinds across it (panic = abort, and every entry point catches a panic and answers "nothing").
mod atlas;
mod engine;

use engine::{Engine, Font};
use std::ffi::{c_char, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Mutex, OnceLock};

pub struct AbtFont(Font);

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct AbtQuad {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
    pub u: i32,
    pub v: i32,
    pub page: i32,
}

#[repr(C)]
pub struct AbtRect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

fn engine() -> &'static Mutex<Engine> {
    static E: OnceLock<Mutex<Engine>> = OnceLock::new();
    E.get_or_init(|| Mutex::new(Engine::new()))
}

/// Runs `f` with the engine locked; a panic (or a lock poisoned by one) gives `fallback`.
fn with<T>(fallback: T, f: impl FnOnce(&mut Engine) -> T) -> T {
    match catch_unwind(AssertUnwindSafe(|| engine().lock().ok().map(|mut e| f(&mut e)))) {
        Ok(Some(v)) => v,
        _ => fallback,
    }
}

unsafe fn text<'a>(p: *const c_char, len: usize) -> &'a str {
    if p.is_null() || len == 0 {
        return "";
    }
    let bytes = std::slice::from_raw_parts(p as *const u8, len);
    // invalid UTF-8 (a file name off a FAT stick) is cut at the first bad byte rather than dropped
    match std::str::from_utf8(bytes) {
        Ok(s) => s,
        Err(e) => std::str::from_utf8_unchecked(&bytes[..e.valid_up_to()]),
    }
}

unsafe fn path(p: *const c_char) -> Option<String> {
    if p.is_null() {
        return None;
    }
    Some(CStr::from_ptr(p).to_string_lossy().into_owned())
}

#[no_mangle]
pub unsafe extern "C" fn abt_add_fallback(p: *const c_char) -> i32 {
    let Some(p) = path(p) else { return -1 };
    with(-1, |e| e.faces(&p).map_or(-1, |ids| ids.len() as i32))
}

#[no_mangle]
pub unsafe extern "C" fn abt_font_load(p: *const c_char, px: f32) -> *mut AbtFont {
    let Some(p) = path(p) else { return std::ptr::null_mut() };
    if !(px > 0.0 && px < 1000.0) {
        return std::ptr::null_mut();
    }
    with(std::ptr::null_mut(), |e| e.load(&p, px).map_or(std::ptr::null_mut(), |f| Box::into_raw(Box::new(AbtFont(f)))))
}

#[no_mangle]
pub unsafe extern "C" fn abt_font_free(font: *mut AbtFont) {
    if !font.is_null() {
        drop(Box::from_raw(font));
    }
}

#[no_mangle]
pub unsafe extern "C" fn abt_font_line_height(font: *const AbtFont) -> i32 {
    font.as_ref().map_or(0, |f| f.0.line_height)
}

#[no_mangle]
pub unsafe extern "C" fn abt_font_ascent(font: *const AbtFont) -> i32 {
    font.as_ref().map_or(0, |f| f.0.ascent)
}

#[no_mangle]
pub unsafe extern "C" fn abt_measure(
    font: *const AbtFont,
    t: *const c_char,
    len: usize,
    wrap: i32,
    width: *mut i32,
    lines: *mut i32,
) {
    let (w, n) = match font.as_ref() {
        Some(f) => {
            let t = text(t, len);
            with((0, 0), |e| {
                let l = e.layout(&f.0, t, wrap);
                (l.width, l.line_widths.len() as i32)
            })
        }
        None => (0, 0),
    };
    if let Some(p) = width.as_mut() {
        *p = w;
    }
    if let Some(p) = lines.as_mut() {
        *p = n;
    }
}

#[no_mangle]
pub unsafe extern "C" fn abt_wrap(
    font: *const AbtFont,
    t: *const c_char,
    len: usize,
    wrap: i32,
    out: *mut c_char,
    cap: usize,
) -> usize {
    let Some(f) = font.as_ref() else { return 0 };
    let t = text(t, len);
    let joined = with(String::new(), |e| e.layout(&f.0, t, wrap).lines_text.join("\n"));
    if !out.is_null() {
        let n = joined.len().min(cap);
        std::ptr::copy_nonoverlapping(joined.as_ptr(), out as *mut u8, n);
    }
    joined.len()
}

/// The byte offsets where a line may break (UAX #14), the end of the text left out.
pub fn line_breaks(t: &str) -> Vec<u32> {
    unicode_linebreak::linebreaks(t).filter(|(i, _)| *i < t.len()).map(|(i, _)| i as u32).collect()
}

#[no_mangle]
pub unsafe extern "C" fn abt_line_breaks(t: *const c_char, len: usize, out: *mut u32, cap: usize) -> usize {
    let t = text(t, len);
    let breaks = catch_unwind(|| line_breaks(t)).unwrap_or_default();
    if !out.is_null() {
        let n = breaks.len().min(cap);
        std::ptr::copy_nonoverlapping(breaks.as_ptr(), out, n);
    }
    breaks.len()
}

#[no_mangle]
pub unsafe extern "C" fn abt_missing_glyphs(font: *const AbtFont, t: *const c_char, len: usize) -> usize {
    let Some(f) = font.as_ref() else { return 0 };
    let t = text(t, len);
    with(0, |e| e.layout(&f.0, t, 0).missing)
}

#[no_mangle]
pub unsafe extern "C" fn abt_quads(
    font: *const AbtFont,
    t: *const c_char,
    len: usize,
    wrap: i32,
    align: i32,
    out: *mut AbtQuad,
    cap: usize,
) -> usize {
    let Some(f) = font.as_ref() else { return 0 };
    let t = text(t, len);
    let quads = with(Vec::new(), |e| e.quads(&f.0, t, wrap, align));
    if !out.is_null() {
        let dst = std::slice::from_raw_parts_mut(out, cap.min(quads.len()));
        for (d, q) in dst.iter_mut().zip(quads.iter()) {
            let r = q.slot.rect;
            *d = AbtQuad {
                x: q.x,
                y: q.y,
                w: r.w as i32,
                h: r.h as i32,
                u: r.x as i32,
                v: r.y as i32,
                page: q.slot.page as i32,
            };
        }
    }
    quads.len()
}

#[no_mangle]
pub extern "C" fn abt_atlas_page_size() -> i32 {
    atlas::PAGE_SIZE as i32
}

#[no_mangle]
pub extern "C" fn abt_atlas_pages() -> i32 {
    with(0, |e| e.atlas.pages() as i32)
}

#[no_mangle]
pub unsafe extern "C" fn abt_atlas_take_dirty(page: i32, rect: *mut AbtRect, dst: *mut u8, pitch: i32) -> i32 {
    let size = atlas::PAGE_SIZE as usize;
    if page < 0 || dst.is_null() || rect.is_null() || (pitch as usize) < size * 4 {
        return 0;
    }
    let dst = std::slice::from_raw_parts_mut(dst, pitch as usize * size);
    let r = with(None, |e| e.atlas.take_dirty(page as usize, dst, pitch as usize));
    match r {
        Some(r) => {
            *rect = AbtRect { x: r.x as i32, y: r.y as i32, w: r.w as i32, h: r.h as i32 };
            1
        }
        None => 0,
    }
}

#[no_mangle]
pub extern "C" fn abt_atlas_all_dirty() {
    with((), |e| e.atlas.all_dirty())
}
