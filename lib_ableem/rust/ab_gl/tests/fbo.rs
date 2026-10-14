//! Draws into a render target (an FBO) through the backend's C table and reads it back, on a headless GLES 3.0
//! context: EGL without a surface (Mesa's surfaceless platform, llvmpipe on a machine without a GPU), loaded with
//! dlopen so the crate needs nothing more. Where there is no libEGL or no such context the test says SKIP and passes.
#![cfg(target_os = "linux")]

use ab_gl::backend::{ab_render_backend, AbrColor, AbrContext, AbrFRect, AbrPoint, AbrRect, AbrTexture};
use std::ffi::{c_char, c_void, CString};
use std::sync::OnceLock;

#[link(name = "dl")]
extern "C" {
    fn dlopen(name: *const c_char, flags: i32) -> *mut c_void;
    fn dlsym(handle: *mut c_void, name: *const c_char) -> *mut c_void;
}

const RTLD_NOW: i32 = 2;
const EGL_NONE: i32 = 0x3038;
const EGL_RENDERABLE_TYPE: i32 = 0x3040;
const EGL_OPENGL_ES3_BIT: i32 = 0x40;
const EGL_CONTEXT_MAJOR_VERSION: i32 = 0x3098;
const EGL_OPENGL_ES_API: u32 = 0x30A0;
const EGL_PLATFORM_SURFACELESS_MESA: u32 = 0x31DD;

type GetProc = unsafe extern "C" fn(*const c_char) -> *mut c_void;
static EGL_GET_PROC: OnceLock<GetProc> = OnceLock::new();

unsafe extern "C" fn get_proc(name: *const c_char) -> *mut c_void {
    (EGL_GET_PROC.get().unwrap())(name)
}

unsafe fn sym<T>(lib: *mut c_void, name: &str) -> Option<T> {
    let n = CString::new(name).unwrap();
    let p = dlsym(lib, n.as_ptr());
    (!p.is_null()).then(|| std::mem::transmute_copy(&p))
}

/// a current GLES 3 context without a surface, or why not
unsafe fn headless_context() -> Result<(), String> {
    let lib = dlopen(c"libEGL.so.1".as_ptr(), RTLD_NOW);
    if lib.is_null() {
        return Err("no libEGL.so.1".into());
    }
    let get_proc_address: GetProc = sym(lib, "eglGetProcAddress").ok_or("no eglGetProcAddress")?;
    EGL_GET_PROC.set(get_proc_address).ok();
    type GetPlatformDisplay = unsafe extern "C" fn(u32, *mut c_void, *const i32) -> *mut c_void;
    type Initialize = unsafe extern "C" fn(*mut c_void, *mut i32, *mut i32) -> u32;
    type BindApi = unsafe extern "C" fn(u32) -> u32;
    type ChooseConfig = unsafe extern "C" fn(*mut c_void, *const i32, *mut *mut c_void, i32, *mut i32) -> u32;
    type CreateContext = unsafe extern "C" fn(*mut c_void, *mut c_void, *mut c_void, *const i32) -> *mut c_void;
    type MakeCurrent = unsafe extern "C" fn(*mut c_void, *mut c_void, *mut c_void, *mut c_void) -> u32;
    let p = get_proc_address(c"eglGetPlatformDisplayEXT".as_ptr());
    if p.is_null() {
        return Err("no eglGetPlatformDisplayEXT".into());
    }
    let get_display: GetPlatformDisplay = std::mem::transmute(p);
    let initialize: Initialize = sym(lib, "eglInitialize").ok_or("no eglInitialize")?;
    let bind_api: BindApi = sym(lib, "eglBindAPI").ok_or("no eglBindAPI")?;
    let choose: ChooseConfig = sym(lib, "eglChooseConfig").ok_or("no eglChooseConfig")?;
    let create: CreateContext = sym(lib, "eglCreateContext").ok_or("no eglCreateContext")?;
    let make_current: MakeCurrent = sym(lib, "eglMakeCurrent").ok_or("no eglMakeCurrent")?;

    let dpy = get_display(EGL_PLATFORM_SURFACELESS_MESA, std::ptr::null_mut(), std::ptr::null());
    if dpy.is_null() || initialize(dpy, std::ptr::null_mut(), std::ptr::null_mut()) == 0 {
        return Err("no surfaceless EGL display".into());
    }
    if bind_api(EGL_OPENGL_ES_API) == 0 {
        return Err("eglBindAPI(GLES) failed".into());
    }
    let attrs = [EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE];
    let mut config = std::ptr::null_mut();
    let mut n = 0;
    if choose(dpy, attrs.as_ptr(), &mut config, 1, &mut n) == 0 || n < 1 {
        return Err("no GLES 3 EGL config".into());
    }
    let ctx_attrs = [EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE];
    let ctx = create(dpy, config, std::ptr::null_mut(), ctx_attrs.as_ptr());
    if ctx.is_null() {
        return Err("eglCreateContext(GLES 3.0) failed".into());
    }
    if make_current(dpy, std::ptr::null_mut(), std::ptr::null_mut(), ctx) == 0 {
        return Err("eglMakeCurrent without a surface failed".into());
    }
    Ok(())
}

fn rgba(r: u8, g: u8, b: u8, a: u8) -> AbrColor {
    AbrColor { r, g, b, a }
}

#[test]
fn draws_into_a_target_and_reads_it_back() {
    if let Err(e) = unsafe { headless_context() } {
        println!("SKIP: no headless GLES 3.0 context ({e})");
        return;
    }
    unsafe {
        let api = &*ab_render_backend();
        assert_eq!(api.version, 1);
        let mut err = [0 as c_char; 256];
        let c: *mut AbrContext = (api.create)(Some(get_proc), err.as_mut_ptr(), err.len() as i32);
        assert!(!c.is_null(), "create: {:?}", std::ffi::CStr::from_ptr(err.as_ptr()));
        let mut line = [0 as c_char; 512];
        (api.describe)(c, line.as_mut_ptr(), line.len() as i32);
        println!("{}", std::ffi::CStr::from_ptr(line.as_ptr()).to_string_lossy());

        const W: i32 = 64;
        const H: i32 = 32;
        let target: *mut AbrTexture = (api.texture_create)(c, W, H, std::ptr::null(), 0, 1);
        assert!(!target.is_null());
        // a 2x2 texture: red, green / blue, white
        let px: [u8; 16] = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255];
        let tex = (api.texture_create)(c, 2, 2, px.as_ptr() as *const c_void, 8, 2);
        assert!(!tex.is_null());

        (api.begin_frame)(c, 320, 200);
        assert_eq!((api.set_target)(c, target), 1);
        (api.clear)(c, rgba(0, 0, 0, 255));
        // an opaque fill, half-transparent white blended over black, the texture 1:1 nearest, a flipped copy
        (api.fill_rects)(c, &AbrFRect { x: 0.0, y: 0.0, w: 8.0, h: 8.0 }, 1, rgba(10, 20, 30, 255), 0);
        (api.fill_rects)(c, &AbrFRect { x: 8.0, y: 0.0, w: 8.0, h: 8.0 }, 1, rgba(255, 255, 255, 128), 1);
        (api.copy)(c, tex, std::ptr::null(), &AbrFRect { x: 16.0, y: 0.0, w: 2.0, h: 2.0 },
                   rgba(255, 255, 255, 255), 0, 0.0, std::ptr::null(), 0);
        (api.copy)(c, tex, std::ptr::null(), &AbrFRect { x: 20.0, y: 0.0, w: 2.0, h: 2.0 },
                   rgba(255, 255, 255, 255), 0, 0.0, std::ptr::null(), 1);
        // a clipped fill: only its part inside the clip lands
        (api.set_clip)(c, &AbrRect { x: 0, y: 16, w: 4, h: 4 });
        (api.fill_rects)(c, &AbrFRect { x: 0.0, y: 16.0, w: 16.0, h: 16.0 }, 1, rgba(0, 255, 0, 255), 0);
        (api.set_clip)(c, std::ptr::null());
        (api.draw_lines)(c, [AbrPoint { x: 30.0, y: 10.0 }, AbrPoint { x: 40.0, y: 10.0 }].as_ptr(), 1,
                         rgba(255, 0, 255, 255), 0);

        let mut out = vec![0u8; (W * H * 4) as usize];
        assert_eq!((api.read_pixels)(c, std::ptr::null(), out.as_mut_ptr() as *mut c_void, W * 4), 1);
        let at = |x: i32, y: i32| -> [u8; 4] {
            let i = ((y * W + x) * 4) as usize;
            [out[i], out[i + 1], out[i + 2], out[i + 3]]
        };
        assert_eq!(at(3, 3), [10, 20, 30, 255], "opaque fill");
        let half = at(12, 4);
        assert!((126..=130).contains(&half[0]) && half[0] == half[1] && half[1] == half[2], "blend: {half:?}");
        assert_eq!(at(16, 0), [255, 0, 0, 255], "texture top-left");
        assert_eq!(at(17, 1), [255, 255, 255, 255], "texture bottom-right");
        assert_eq!(at(20, 0), [0, 255, 0, 255], "flipped copy top-left");
        assert_eq!(at(2, 18), [0, 255, 0, 255], "inside the clip");
        assert_eq!(at(8, 18), [0, 0, 0, 255], "outside the clip");
        assert_eq!(at(35, 10), [255, 0, 255, 255], "line");
        assert_eq!(at(60, 30), [0, 0, 0, 255], "untouched");

        (api.set_target)(c, std::ptr::null_mut());
        (api.end_frame)(c);
        (api.texture_destroy)(c, tex);
        (api.texture_destroy)(c, target);
        assert_eq!((api.textures_alive)(c), 0);
        (api.destroy)(c);
    }
}
