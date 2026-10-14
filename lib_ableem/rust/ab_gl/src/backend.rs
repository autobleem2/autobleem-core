//! The loadable renderer's C table (lib_ableem/src/ui/render_backend.h, mirrored here field by field): the one
//! export `ab_render_backend` hands lib_ableem's Renderer a versioned table of functions over gfx::Gfx in output
//! pixels. The C++ side owns the window and the GL context and swaps; this side draws through the GL functions it
//! is given.
use crate::gfx::{Blend, Edge, GeoVertex, Gfx, PixelRect, Rect, Texture};
use std::ffi::{c_char, c_void, CString};
use std::os::raw::c_int;

pub const ABR_API_VERSION: u32 = 1;

pub const ABR_TEX_TARGET: u32 = 1;
pub const ABR_TEX_NEAREST: u32 = 2;
pub const ABR_TEX_AUTO_MIPMAPS: u32 = 4;

pub const ABR_FLIP_HORIZONTAL: i32 = 1;
pub const ABR_FLIP_VERTICAL: i32 = 2;

pub const ABR_GL_CORE: i32 = 1;
pub const ABR_GL_ES: i32 = 4;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AbrRect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AbrFRect {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AbrPoint {
    pub x: f32,
    pub y: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AbrColor {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AbrEdge {
    pub x: f32,
    pub top: f32,
    pub bottom: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AbrGlVersion {
    pub profile: i32,
    pub major: i32,
    pub minor: i32,
}

#[repr(C)]
pub struct AbrWindowNeeds {
    pub size: u32,
    pub opengl: i32,
    pub count: i32,
    pub versions: [AbrGlVersion; 4],
}

/// a backend made on a context
pub struct AbrContext {
    gfx: Gfx,
}

/// a texture of it
pub type AbrTexture = Texture;

pub type AbrGetProc = unsafe extern "C" fn(name: *const c_char) -> *mut c_void;

#[repr(C)]
pub struct AbrApi {
    pub size: u32,
    pub version: u32,
    pub name: *const c_char,
    pub window_needs: unsafe extern "C" fn(*mut AbrWindowNeeds),
    pub create: unsafe extern "C" fn(Option<AbrGetProc>, *mut c_char, i32) -> *mut AbrContext,
    pub destroy: unsafe extern "C" fn(*mut AbrContext),
    pub describe: unsafe extern "C" fn(*mut AbrContext, *mut c_char, i32) -> i32,
    pub begin_frame: unsafe extern "C" fn(*mut AbrContext, i32, i32),
    pub end_frame: unsafe extern "C" fn(*mut AbrContext),
    pub set_viewport: unsafe extern "C" fn(*mut AbrContext, *const AbrRect),
    pub set_clip: unsafe extern "C" fn(*mut AbrContext, *const AbrRect),
    pub texture_create: unsafe extern "C" fn(*mut AbrContext, i32, i32, *const c_void, i32, u32) -> *mut AbrTexture,
    pub texture_update: unsafe extern "C" fn(*mut AbrContext, *mut AbrTexture, *const AbrRect, *const c_void, i32),
    pub texture_destroy: unsafe extern "C" fn(*mut AbrContext, *mut AbrTexture),
    pub set_target: unsafe extern "C" fn(*mut AbrContext, *mut AbrTexture) -> i32,
    pub clear: unsafe extern "C" fn(*mut AbrContext, AbrColor),
    pub fill_rects: unsafe extern "C" fn(*mut AbrContext, *const AbrFRect, i32, AbrColor, i32),
    pub draw_lines: unsafe extern "C" fn(*mut AbrContext, *const AbrPoint, i32, AbrColor, i32),
    #[allow(clippy::type_complexity)]
    pub copy: unsafe extern "C" fn(
        *mut AbrContext,
        *mut AbrTexture,
        *const AbrFRect,
        *const AbrFRect,
        AbrColor,
        i32,
        f64,
        *const AbrPoint,
        i32,
    ),
    pub trapezoid: unsafe extern "C" fn(
        *mut AbrContext,
        *mut AbrTexture,
        *const AbrFRect,
        AbrEdge,
        AbrEdge,
        AbrColor,
        AbrColor,
        i32,
        i32,
    ),
    pub geometry: unsafe extern "C" fn(*mut AbrContext, *mut AbrTexture, *const GeoVertex, i32, *const i32, i32, i32),
    pub read_pixels: unsafe extern "C" fn(*mut AbrContext, *const AbrRect, *mut c_void, i32) -> i32,
    pub draw_calls: unsafe extern "C" fn(*mut AbrContext) -> u32,
    pub textures_alive: unsafe extern "C" fn(*mut AbrContext) -> u32,
}

// the table is immutable and its name a static string
unsafe impl Sync for AbrApi {}

fn blend(b: i32) -> Blend {
    match b {
        0 => Blend::None,
        2 => Blend::Add,
        3 => Blend::Mod,
        4 => Blend::Premultiplied,
        5 => Blend::Mask,
        _ => Blend::Blend,
    }
}

fn color(c: AbrColor) -> [f32; 4] {
    [c.r as f32 / 255.0, c.g as f32 / 255.0, c.b as f32 / 255.0, c.a as f32 / 255.0]
}

fn frect(r: *const AbrFRect) -> Option<Rect> {
    unsafe { r.as_ref() }.map(|r| [r.x, r.y, r.w, r.h])
}

fn prect(r: *const AbrRect) -> Option<PixelRect> {
    unsafe { r.as_ref() }.map(|r| [r.x, r.y, r.w, r.h])
}

// a string into the caller's buffer, cut to fit and ended with a 0; its length
unsafe fn put(s: &str, out: *mut c_char, size: i32) -> i32 {
    if out.is_null() || size <= 0 {
        return s.len() as i32;
    }
    let n = s.len().min(size as usize - 1);
    std::ptr::copy_nonoverlapping(s.as_ptr(), out as *mut u8, n);
    *out.add(n) = 0;
    n as i32
}

unsafe extern "C" fn window_needs(out: *mut AbrWindowNeeds) {
    let Some(out) = out.as_mut() else { return };
    if (out.size as usize) < std::mem::size_of::<AbrWindowNeeds>() {
        return;
    }
    // GLES 3.0 (the console, the Pi), else desktop GL 3.3 core (a PC's driver without GLES): the two the
    // shaders are written for (gfx.rs)
    out.opengl = 1;
    out.count = 2;
    out.versions[0] = AbrGlVersion { profile: ABR_GL_ES, major: 3, minor: 0 };
    out.versions[1] = AbrGlVersion { profile: ABR_GL_CORE, major: 3, minor: 3 };
}

unsafe extern "C" fn create(get_proc: Option<AbrGetProc>, err: *mut c_char, err_size: i32) -> *mut AbrContext {
    let Some(get_proc) = get_proc else {
        put("no GL function loader", err, err_size);
        return std::ptr::null_mut();
    };
    let gl = glow::Context::from_loader_function(|name| match CString::new(name) {
        Ok(name) => get_proc(name.as_ptr()) as *const c_void,
        Err(_) => std::ptr::null(),
    });
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| Gfx::new(gl))) {
        Ok(Ok(gfx)) => Box::into_raw(Box::new(AbrContext { gfx })),
        Ok(Err(e)) => {
            put(&e, err, err_size);
            std::ptr::null_mut()
        }
        Err(_) => {
            put("the GL setup panicked", err, err_size);
            std::ptr::null_mut()
        }
    }
}

unsafe extern "C" fn destroy(c: *mut AbrContext) {
    if !c.is_null() {
        drop(Box::from_raw(c));
    }
}

unsafe extern "C" fn describe(c: *mut AbrContext, out: *mut c_char, size: i32) -> i32 {
    let Some(c) = c.as_mut() else { return 0 };
    put(&c.gfx.describe(), out, size)
}

unsafe extern "C" fn begin_frame(c: *mut AbrContext, w: i32, h: i32) {
    if let Some(c) = c.as_mut() {
        c.gfx.begin_frame(w, h);
    }
}

unsafe extern "C" fn end_frame(c: *mut AbrContext) {
    if let Some(c) = c.as_mut() {
        c.gfx.flush();
        c.gfx.end_frame();
    }
}

unsafe extern "C" fn set_viewport(c: *mut AbrContext, r: *const AbrRect) {
    if let Some(c) = c.as_mut() {
        c.gfx.set_viewport(prect(r));
    }
}

unsafe extern "C" fn set_clip(c: *mut AbrContext, r: *const AbrRect) {
    if let Some(c) = c.as_mut() {
        c.gfx.set_clip(prect(r));
    }
}

unsafe extern "C" fn texture_create(
    c: *mut AbrContext,
    w: i32,
    h: i32,
    rgba: *const c_void,
    pitch: i32,
    flags: u32,
) -> *mut AbrTexture {
    let Some(c) = c.as_mut() else { return std::ptr::null_mut() };
    if w <= 0 || h <= 0 {
        return std::ptr::null_mut();
    }
    let (w, h) = (w as u32, h as u32);
    let made = if flags & ABR_TEX_TARGET != 0 {
        if flags & ABR_TEX_AUTO_MIPMAPS != 0 {
            c.gfx.target_auto_mips(w, h)
        } else {
            c.gfx.target_plain(w as f32, h as f32, 1.0)
        }
    } else {
        // rows `pitch` apart: copied tight when they are not
        let row = w as usize * 4;
        let tight: Vec<u8>;
        let px: &[u8] = if rgba.is_null() {
            &[]
        } else if pitch as usize == row || pitch <= 0 {
            std::slice::from_raw_parts(rgba as *const u8, row * h as usize)
        } else {
            let src = std::slice::from_raw_parts(rgba as *const u8, pitch as usize * (h as usize - 1) + row);
            tight = (0..h as usize).flat_map(|y| src[y * pitch as usize..y * pitch as usize + row].iter().copied()).collect();
            &tight
        };
        if flags & ABR_TEX_AUTO_MIPMAPS != 0 {
            c.gfx.texture_auto_mips(w, h, px, 1.0)
        } else {
            c.gfx.texture(w, h, px, 1.0, false)
        }
    };
    match made {
        Ok(t) => {
            if flags & ABR_TEX_NEAREST != 0 {
                c.gfx.set_nearest(&t);
            }
            Box::into_raw(Box::new(t))
        }
        Err(_) => std::ptr::null_mut(),
    }
}

unsafe extern "C" fn texture_update(c: *mut AbrContext, t: *mut AbrTexture, r: *const AbrRect, rgba: *const c_void, pitch: i32) {
    let (Some(c), Some(t)) = (c.as_mut(), t.as_ref()) else { return };
    if rgba.is_null() {
        return;
    }
    let [x, y, w, h] = prect(r).unwrap_or([0, 0, t.w as i32, t.h as i32]);
    if w <= 0 || h <= 0 || x < 0 || y < 0 || (x + w) as u32 > t.w || (y + h) as u32 > t.h {
        return;
    }
    let pitch = if pitch <= 0 { w * 4 } else { pitch };
    let len = pitch as usize * (h as usize - 1) + w as usize * 4;
    let px = std::slice::from_raw_parts(rgba as *const u8, len);
    c.gfx.update_pitch(t, x as u32, y as u32, w as u32, h as u32, px, pitch as u32);
}

unsafe extern "C" fn texture_destroy(c: *mut AbrContext, t: *mut AbrTexture) {
    let Some(c) = c.as_mut() else { return };
    if !t.is_null() {
        c.gfx.free(*Box::from_raw(t));
    }
}

unsafe extern "C" fn set_target(c: *mut AbrContext, t: *mut AbrTexture) -> i32 {
    let Some(c) = c.as_mut() else { return 0 };
    match t.as_ref() {
        None => {
            c.gfx.set_target(None);
            1
        }
        Some(t) if t.is_target() => {
            c.gfx.set_target(Some(t));
            1
        }
        Some(_) => 0,
    }
}

unsafe extern "C" fn clear(c: *mut AbrContext, col: AbrColor) {
    if let Some(c) = c.as_mut() {
        c.gfx.clear(color(col));
    }
}

unsafe extern "C" fn fill_rects(c: *mut AbrContext, rects: *const AbrFRect, count: i32, col: AbrColor, b: i32) {
    let Some(c) = c.as_mut() else { return };
    if rects.is_null() || count <= 0 {
        return;
    }
    let (col, b) = (color(col), blend(b));
    for r in std::slice::from_raw_parts(rects, count as usize) {
        c.gfx.fill_blend([r.x, r.y, r.w, r.h], col, b);
    }
}

unsafe extern "C" fn draw_lines(c: *mut AbrContext, points: *const AbrPoint, count: i32, col: AbrColor, b: i32) {
    let Some(c) = c.as_mut() else { return };
    if points.is_null() || count <= 0 {
        return;
    }
    let (col, b) = (color(col), blend(b));
    for p in std::slice::from_raw_parts(points, 2 * count as usize).chunks(2) {
        c.gfx.line([p[0].x, p[0].y], [p[1].x, p[1].y], col, b);
    }
}

#[allow(clippy::too_many_arguments)]
unsafe extern "C" fn copy(
    c: *mut AbrContext,
    t: *mut AbrTexture,
    src: *const AbrFRect,
    dst: *const AbrFRect,
    tint: AbrColor,
    b: i32,
    angle: f64,
    center: *const AbrPoint,
    flip: i32,
) {
    let (Some(c), Some(t)) = (c.as_mut(), t.as_ref()) else { return };
    let dst = frect(dst).unwrap_or_else(|| {
        let (w, h) = c.gfx.current_size();
        [0.0, 0.0, w as f32, h as f32]
    });
    let center = center.as_ref().map(|p| [p.x, p.y]);
    c.gfx.copy_ex(
        t,
        frect(src),
        dst,
        color(tint),
        blend(b),
        angle,
        center,
        flip & ABR_FLIP_HORIZONTAL != 0,
        flip & ABR_FLIP_VERTICAL != 0,
    );
}

#[allow(clippy::too_many_arguments)]
unsafe extern "C" fn trapezoid(
    c: *mut AbrContext,
    t: *mut AbrTexture,
    src: *const AbrFRect,
    left: AbrEdge,
    right: AbrEdge,
    top: AbrColor,
    bottom: AbrColor,
    flip: i32,
    b: i32,
) {
    let (Some(c), Some(t)) = (c.as_mut(), t.as_ref()) else { return };
    let src = frect(src).unwrap_or([0.0, 0.0, t.w as f32, t.h as f32]);
    let edge = |e: AbrEdge| Edge { x: e.x, top: e.top, bottom: e.bottom };
    c.gfx.trapezoid_blend(t, src, edge(left), edge(right), color(top), color(bottom), flip != 0, blend(b));
}

unsafe extern "C" fn geometry(
    c: *mut AbrContext,
    t: *mut AbrTexture,
    vertices: *const GeoVertex,
    count: i32,
    indices: *const i32,
    index_count: i32,
    b: i32,
) {
    let Some(c) = c.as_mut() else { return };
    if vertices.is_null() || count <= 0 {
        return;
    }
    let v = std::slice::from_raw_parts(vertices, count as usize);
    let i: &[i32] = if indices.is_null() || index_count <= 0 {
        &[]
    } else {
        std::slice::from_raw_parts(indices, index_count as usize)
    };
    c.gfx.geometry(t.as_ref(), v, i, blend(b));
}

unsafe extern "C" fn read_pixels(c: *mut AbrContext, r: *const AbrRect, out: *mut c_void, pitch: i32) -> i32 {
    let Some(c) = c.as_mut() else { return 0 };
    if out.is_null() {
        return 0;
    }
    let (w, h, px) = c.gfx.read_pixels(prect(r));
    if w == 0 || h == 0 {
        return 0;
    }
    let row = w as usize * 4;
    let pitch = if pitch <= 0 { row } else { pitch as usize };
    for y in 0..h as usize {
        std::ptr::copy_nonoverlapping(px[y * row..].as_ptr(), (out as *mut u8).add(y * pitch), row);
    }
    1
}

unsafe extern "C" fn draw_calls(c: *mut AbrContext) -> u32 {
    c.as_ref().map(|c| c.gfx.draw_calls).unwrap_or(0)
}

unsafe extern "C" fn textures_alive(c: *mut AbrContext) -> u32 {
    c.as_ref().map(|c| c.gfx.textures_alive()).unwrap_or(0)
}

static API: AbrApi = AbrApi {
    size: std::mem::size_of::<AbrApi>() as u32,
    version: ABR_API_VERSION,
    name: c"gles3".as_ptr(),
    window_needs,
    create,
    destroy,
    describe,
    begin_frame,
    end_frame,
    set_viewport,
    set_clip,
    texture_create,
    texture_update,
    texture_destroy,
    set_target,
    clear,
    fill_rects,
    draw_lines,
    copy,
    trapezoid,
    geometry,
    read_pixels,
    draw_calls,
    textures_alive,
};

/// the backend's one export: its table
#[no_mangle]
pub extern "C" fn ab_render_backend() -> *const AbrApi {
    &API
}

// c_int is i32 on every target this builds for (the table's int32_t)
const _: () = assert!(std::mem::size_of::<c_int>() == 4);
