//! The OpenGL ES 3.0 renderer on glow: textured quads and perspective trapezoids, batched, in the launcher's logical
//! 1280x720 pixels, drawn to the screen or into a texture (a cover is composed into one, as the C++ launcher composes
//! it into an SDL render target).
//!
//! The blending is SDL's, so a picture composes and draws as it does in the C++ launcher: `Blend` is
//! SDL_BLENDMODE_BLEND (colour by source alpha, alpha "over"), `Premultiplied` SDL_BLENDMODE_BLEND_PREMULTIPLIED. A
//! colour on a draw is SDL's colour and alpha mod. A trapezoid's sloping top and bottom are smoothed in the shader
//! (a pixel's coverage from fwidth): a turned cover has no stepped edges without multisampling, which the C++
//! renderer got from a one-pixel skirt round each strip.
//!
//! The C++ launcher's backend (backend.rs) draws in output pixels instead of the 1280x720 canvas: begin_frame,
//! set_target, set_viewport and set_clip, with SDL's other blend modes, lines, rotated copies and raw geometry. On a
//! desktop GL context (3.3 core) the shaders get a `#version 330 core` header instead of ES 3.00's.
use crate::diag::{self, Knobs, Stats};
use glow::HasContext;
use std::cell::Cell;

// the shaders without their #version line: ES 3.00 on GLES, 330 core on desktop GL
const VS: &str = r#"
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec3 a_uvq;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_edge;
uniform vec4 u_xform; // logical pixels to clip space: scale x, scale y, offset x, offset y
out vec3 v_uvq;
out vec4 v_color;
out float v_edge;
void main() {
    v_uvq = a_uvq;
    v_color = a_color;
    v_edge = a_edge;
    gl_Position = vec4(a_pos * u_xform.xy + u_xform.zw, 0.0, 1.0);
}"#;

const FS: &str = r#"
precision mediump float;
in vec3 v_uvq;
in vec4 v_color;
in float v_edge;
uniform sampler2D u_tex;
out vec4 o_color;
void main() {
    vec4 c = texture(u_tex, v_uvq.xy / v_uvq.z) * v_color;
    // a trapezoid: 0 on its top edge, 1 on its bottom one; a quad: -1 (no smoothing)
    if (v_edge >= 0.0) {
        float w = fwidth(v_edge);
        if (w > 0.0)
            c *= clamp(min(v_edge, 1.0 - v_edge) / w, 0.0, 1.0);
    }
    o_color = c;
}"#;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Blend {
    /// SDL_BLENDMODE_BLEND
    Blend,
    /// SDL_BLENDMODE_BLEND_PREMULTIPLIED: the source's colour is already times its alpha
    Premultiplied,
    /// SDL_BLENDMODE_NONE: the source replaces what is there, alpha too
    None,
    /// SDL_BLENDMODE_ADD: colour by source alpha added, alpha kept
    Add,
    /// SDL_BLENDMODE_MOD: colour times the source's, alpha kept
    Mod,
    /// lib_ableem's mask: colour kept, alpha times the source's
    Mask,
}

/// a vertex of `geometry`: SDL_Vertex's fields (position, colour, texture coordinate)
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct GeoVertex {
    pub pos: [f32; 2],
    pub color: [u8; 4],
    pub uv: [f32; 2],
}

// Texture::mips: none made yet, made and current, made but the picture changed since
const MIPS_NONE: u8 = 0;
const MIPS_OK: u8 = 1;
const MIPS_STALE: u8 = 2;

/// a draw that shrinks a picture by more than this makes its mipmaps (auto_mips)
const MIPS_AT: f32 = 1.5;

/// A picture on the GPU. `w`/`h` are its pixels, `scale` its pixels per logical pixel (2 for a theme's @2x image, the
/// output scale for a composed cover): source rects are given in logical pixels, as the C++ Texture's are.
pub struct Texture {
    pub tex: glow::Texture,
    pub w: u32,
    pub h: u32,
    pub scale: f32,
    fbo: Option<glow::Framebuffer>,
    /// end_target makes its mipmaps (a composed cover drawn small); a screen-sized picture needs none
    mipmaps: bool,
    /// mipmaps made on the first draw that shrinks it by more than 1.5 (a cover, scaled art) - never for a picture
    /// drawn at its size (a UI piece), so they cost memory only where they are seen
    auto_mips: bool,
    mips: Cell<u8>,
}

impl Texture {
    /// its size in logical pixels
    pub fn size(&self) -> (f32, f32) {
        (self.w as f32 / self.scale, self.h as f32 / self.scale)
    }

    /// a texture that can be drawn into (target)
    pub fn is_target(&self) -> bool {
        self.fbo.is_some()
    }

    /// whether its mipmaps are made (auto_mips: by a draw that shrank it)
    pub fn has_mipmaps(&self) -> bool {
        self.mips.get() != MIPS_NONE
    }
}

/// a rect: x, y, w, h
pub type Rect = [f32; 4];
pub type Color = [f32; 4];

/// a vertical edge of a trapezoid on screen (ableem::VerticalEdge)
#[derive(Clone, Copy, Debug)]
pub struct Edge {
    pub x: f32,
    pub top: f32,
    pub bottom: f32,
}

/// a rect of whole pixels: x, y, w, h (y down from the top of the screen or target)
pub type PixelRect = [i32; 4];

#[repr(C)]
#[derive(Clone, Copy)]
struct Vertex {
    pos: [f32; 2],
    uvq: [f32; 3],
    color: [f32; 4],
    edge: f32,
}

/// what a draw is drawn with besides its texture and blend: the pixel transform, the GL viewport and the scissor
/// (None = off), in GL's coordinates
#[derive(Clone, Copy, PartialEq, Debug)]
struct DrawState {
    xform: [f32; 4],
    viewport: [i32; 4],
    scissor: Option<[i32; 4]>,
}

/// a command's vertices: quads (four each, drawn with the static index buffer) or triangles (three each, geometry)
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Prim {
    Quads,
    Triangles,
}

/// a draw of the frame, replayed by flush_frame: `count` vertices of the frame's array from `first`
struct Cmd {
    first: usize,
    count: usize,
    prim: Prim,
    tex: glow::Texture,
    blend: Blend,
    state: DrawState,
}

/// the quads one draw may have: the static index buffer's (u16 indices, 60000 vertices)
const IBO_QUADS: usize = 15000;
/// the vertex buffers a frame's flushes go round: each upload goes to one the GPU is not reading any more
const VBO_RING: usize = 3;

pub struct Gfx {
    pub gl: glow::Context,
    program: glow::Program,
    vao: glow::VertexArray,
    vbos: [glow::Buffer; VBO_RING],
    ring: usize,
    u_xform: Option<glow::UniformLocation>,
    white: Texture,
    /// the frame's vertices and its draws so far: drawn by flush_frame, in one buffer upload
    verts: Vec<Vertex>,
    cmds: Vec<Cmd>,
    /// the texture and blend of the last of `cmds` while it takes more vertices (None: the next draw starts one)
    bound: Option<(glow::Texture, Blend, Prim)>,
    /// the transform, viewport and scissor as set now: a new command draws with it
    state: DrawState,
    /// the screen's drawable size, and where the 1280x720 canvas sits in it (letterboxed at its own shape)
    screen: (i32, i32),
    pub canvas_scale: f32,
    canvas_offset: (f32, f32),
    pub draw_calls: u32,
    /// the textures made by texture() / target() and not freed yet (AB_SHELL_TRACE's slow frames say it)
    alive: u32,
    /// one fence per frame still being drawn (end_frame)
    in_flight: std::collections::VecDeque<glow::Fence>,
    /// the targets being drawn into, the innermost last: end_target goes back to the one before (a cover composed
    /// while a screen's picture is drawn into a target), else to the screen
    targets: Vec<(glow::Framebuffer, i32, i32, f32, f32)>,
    /// output-pixel mode (begin_frame): the screen's viewport (SDL_RenderSetViewport, None = all of it) and the clip
    /// rect of whatever is drawn into (SDL_RenderSetClipRect), both in pixels
    viewport: Option<PixelRect>,
    clip: Option<PixelRect>,
    /// the most anisotropy a texture may get (EXT_texture_filter_anisotropic), 0 without the extension
    anisotropy: f32,
    /// a desktop GL context (3.3 core) rather than GLES
    desktop: bool,
    /// the diagnostics knobs (diag.rs), read once here
    pub knobs: Knobs,
    /// the counters since the last stats line (knobs.stats)
    pub stats: Stats,
    /// the stats line a second of frames made, until the caller takes it (backend.rs: stats)
    pub stats_line: Option<String>,
}

/// end_frame: how many frames may be queued before the next one waits. A driver whose swap does not wait (Mesa's
/// llvmpipe offscreen, a VM) otherwise takes frames faster than it draws them, and whatever has to wait for the GPU
/// (a texture changed, mipmaps made) waits for the whole queue - seconds of it on the VM
const FRAMES_IN_FLIGHT: usize = 2;

// EXT_texture_filter_anisotropic's enums (not in GLES 3.0's core)
const TEXTURE_MAX_ANISOTROPY: u32 = 0x84FE;
const MAX_TEXTURE_MAX_ANISOTROPY: u32 = 0x84FF;
/// the anisotropy a mipmapped texture gets at most: a turned cover's columns stay sharp, without the cost of 16
const ANISOTROPY_CAP: f32 = 4.0;

unsafe fn program(gl: &glow::Context, vs: &str, fs: &str) -> Result<glow::Program, String> {
    let p = gl.create_program()?;
    let mut shaders = Vec::new();
    for (kind, src) in [(glow::VERTEX_SHADER, vs), (glow::FRAGMENT_SHADER, fs)] {
        let s = gl.create_shader(kind)?;
        gl.shader_source(s, src);
        gl.compile_shader(s);
        if !gl.get_shader_compile_status(s) {
            return Err(format!("shader: {}", gl.get_shader_info_log(s)));
        }
        gl.attach_shader(p, s);
        shaders.push(s);
    }
    gl.link_program(p);
    if !gl.get_program_link_status(p) {
        return Err(format!("link: {}", gl.get_program_info_log(p)));
    }
    for s in shaders {
        gl.detach_shader(p, s);
        gl.delete_shader(s);
    }
    Ok(p)
}

fn bytes<T>(v: &[T]) -> &[u8] {
    unsafe { std::slice::from_raw_parts(v.as_ptr() as *const u8, std::mem::size_of_val(v)) }
}

pub const CANVAS_W: f32 = 1280.0;
pub const CANVAS_H: f32 = 720.0;

impl Gfx {
    pub unsafe fn new(gl: glow::Context) -> Result<Gfx, String> {
        let desktop = !gl.version().is_embedded;
        let header = if desktop { "#version 330 core\n" } else { "#version 300 es\n" };
        let program = program(&gl, &format!("{header}{VS}"), &format!("{header}{FS}"))?;
        let vao = gl.create_vertex_array()?;
        gl.bind_vertex_array(Some(vao));
        let vbos = [gl.create_buffer()?, gl.create_buffer()?, gl.create_buffer()?];
        gl.bind_buffer(glow::ARRAY_BUFFER, Some(vbos[0]));
        Self::vertex_pointers(&gl, 0);
        for a in 0..4 {
            gl.enable_vertex_attrib_array(a);
        }
        // the quads' indices, made once: 0,1,2, 2,1,3 and so on every four vertices
        let ibo = gl.create_buffer()?;
        gl.bind_buffer(glow::ELEMENT_ARRAY_BUFFER, Some(ibo));
        let mut quad_indices: Vec<u16> = Vec::with_capacity(IBO_QUADS * 6);
        for k in 0..IBO_QUADS as u16 {
            let b = k * 4;
            quad_indices.extend_from_slice(&[b, b + 1, b + 2, b + 2, b + 1, b + 3]);
        }
        gl.buffer_data_u8_slice(glow::ELEMENT_ARRAY_BUFFER, bytes(&quad_indices), glow::STATIC_DRAW);
        gl.use_program(Some(program));
        gl.uniform_1_i32(gl.get_uniform_location(program, "u_tex").as_ref(), 0);
        let u_xform = gl.get_uniform_location(program, "u_xform");
        gl.enable(glow::BLEND);
        gl.disable(glow::CULL_FACE);
        gl.disable(glow::DEPTH_TEST);
        gl.active_texture(glow::TEXTURE0);
        let knobs = Knobs::read();
        // AB_GL_ANISO=0 (or 1) leaves it off
        let anisotropy = if knobs.aniso_cap > 1.0 && gl.supported_extensions().contains("GL_EXT_texture_filter_anisotropic") {
            gl.get_parameter_f32(MAX_TEXTURE_MAX_ANISOTROPY).min(ANISOTROPY_CAP).min(knobs.aniso_cap)
        } else {
            0.0
        };
        let white = Self::upload(&gl, 1, 1, &[255, 255, 255, 255], 1.0, false, 0.0)?;
        Ok(Gfx {
            gl,
            program,
            vao,
            vbos,
            ring: 0,
            u_xform,
            white,
            verts: Vec::with_capacity(16384),
            cmds: Vec::with_capacity(256),
            bound: None,
            state: DrawState { xform: [0.0; 4], viewport: [0, 0, CANVAS_W as i32, CANVAS_H as i32], scissor: None },
            screen: (CANVAS_W as i32, CANVAS_H as i32),
            canvas_scale: 1.0,
            canvas_offset: (0.0, 0.0),
            draw_calls: 0,
            alive: 0,
            in_flight: std::collections::VecDeque::new(),
            targets: Vec::new(),
            viewport: None,
            clip: None,
            anisotropy,
            desktop,
            knobs,
            stats: Stats::default(),
            stats_line: None,
        })
    }

    /// GL_VERSION, GL_RENDERER and whether anisotropic filtering is there: the one line the backend logs at start
    pub fn describe(&self) -> String {
        unsafe {
            let gl = &self.gl;
            let aniso = if self.anisotropy > 0.0 {
                format!("EXT_texture_filter_anisotropic (up to {}x used)", self.anisotropy)
            } else {
                "no anisotropic filtering".to_string()
            };
            let k = &self.knobs;
            format!(
                "{} | {} | {} | {} | knobs ({}): AB_GL_MIPMAPS={} AB_GL_ANISO={} AB_GL_STATS={}",
                gl.get_parameter_string(glow::VERSION),
                gl.get_parameter_string(glow::RENDERER),
                if self.desktop { "desktop GL, GLSL 330 core" } else { "GLES, GLSL 300 es" },
                aniso,
                k.from,
                k.mipmaps as u8,
                k.aniso_cap,
                k.stats as u8
            )
        }
    }

    #[allow(clippy::too_many_arguments)]
    unsafe fn upload(
        gl: &glow::Context,
        w: u32,
        h: u32,
        rgba: &[u8],
        scale: f32,
        mipmaps: bool,
        anisotropy: f32,
    ) -> Result<Texture, String> {
        let t = gl.create_texture()?;
        gl.bind_texture(glow::TEXTURE_2D, Some(t));
        gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 1);
        gl.tex_image_2d(
            glow::TEXTURE_2D,
            0,
            glow::RGBA8 as i32,
            w as i32,
            h as i32,
            0,
            glow::RGBA,
            glow::UNSIGNED_BYTE,
            if rgba.is_empty() { None } else { Some(rgba) },
        );
        let min = if mipmaps { glow::LINEAR_MIPMAP_LINEAR } else { glow::LINEAR };
        gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, min as i32);
        gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAG_FILTER, glow::LINEAR as i32);
        gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_WRAP_S, glow::CLAMP_TO_EDGE as i32);
        gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_WRAP_T, glow::CLAMP_TO_EDGE as i32);
        if mipmaps && anisotropy > 0.0 {
            gl.tex_parameter_f32(glow::TEXTURE_2D, TEXTURE_MAX_ANISOTROPY, anisotropy);
        }
        if mipmaps && !rgba.is_empty() {
            gl.generate_mipmap(glow::TEXTURE_2D);
        }
        let mips = Cell::new(if mipmaps && !rgba.is_empty() { MIPS_OK } else { MIPS_NONE });
        Ok(Texture { tex: t, w, h, scale, fbo: None, mipmaps, auto_mips: false, mips })
    }

    /// an image (straight RGBA, rows top first) of `scale` pixels per logical pixel
    pub fn texture(&mut self, w: u32, h: u32, rgba: &[u8], scale: f32, mipmaps: bool) -> Result<Texture, String> {
        self.cut(diag::CUT_TEXTURE_OP);
        self.bound = None;
        let mipmaps = mipmaps && self.knobs.mipmaps;
        let t = unsafe { Self::upload(&self.gl, w, h, rgba, scale, mipmaps, self.anisotropy) }?;
        self.alive += 1;
        if !rgba.is_empty() {
            self.stats.uploads += 1;
            self.stats.upload_bytes += rgba.len() as u64;
            if mipmaps {
                self.stats.mipmaps += 1;
            }
        }
        Ok(t)
    }

    /// an image whose mipmaps are made only when a draw shrinks it by more than 1.5 (a cover, scaled art): a UI
    /// piece drawn at its size never gets them
    pub fn texture_auto_mips(&mut self, w: u32, h: u32, rgba: &[u8], scale: f32) -> Result<Texture, String> {
        let mut t = self.texture(w, h, rgba, scale, false)?;
        t.auto_mips = true;
        Ok(t)
    }

    /// an image scaled by whole pixels when drawn (nearest): the performance overlay's bitmap text
    pub fn texture_nearest(&mut self, w: u32, h: u32, rgba: &[u8]) -> Result<Texture, String> {
        let t = self.texture(w, h, rgba, 1.0, false)?;
        self.set_nearest(&t);
        Ok(t)
    }

    /// `t` drawn with nearest filtering from now on (SDL's scale quality "0" at the texture's creation)
    pub fn set_nearest(&mut self, t: &Texture) {
        self.touch(t.tex, diag::CUT_TEXTURE_OP);
        unsafe {
            self.gl.bind_texture(glow::TEXTURE_2D, Some(t.tex));
            self.gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, glow::NEAREST as i32);
            self.gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAG_FILTER, glow::NEAREST as i32);
        }
        self.bound = None;
    }

    /// a texture to draw into (`begin_target`), `w` x `h` logical pixels at `scale` pixels each, mipmapped
    pub fn target(&mut self, w: f32, h: f32, scale: f32) -> Result<Texture, String> {
        self.make_target(w, h, scale, true)
    }

    /// a target without mipmaps, drawn at its own size: a screen's picture (the backdrop, a transition's two)
    pub fn target_plain(&mut self, w: f32, h: f32, scale: f32) -> Result<Texture, String> {
        self.make_target(w, h, scale, false)
    }

    /// a target of `w` x `h` pixels whose mipmaps are made only when a draw shrinks it (a composed cover in the C++
    /// launcher); cleared to transparent black, as SDL's new targets are
    pub fn target_auto_mips(&mut self, w: u32, h: u32) -> Result<Texture, String> {
        let mut t = self.make_target(w as f32, h as f32, 1.0, false)?;
        t.auto_mips = true;
        Ok(t)
    }

    fn make_target(&mut self, w: f32, h: f32, scale: f32, mipmaps: bool) -> Result<Texture, String> {
        let pw = (w * scale).round().max(1.0) as u32;
        let ph = (h * scale).round().max(1.0) as u32;
        let mut t = self.texture(pw, ph, &[], scale, mipmaps)?;
        unsafe {
            let gl = &self.gl;
            if t.mipmaps {
                gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, glow::LINEAR_MIPMAP_LINEAR as i32);
            }
            let fbo = gl.create_framebuffer()?;
            gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
            gl.framebuffer_texture_2d(glow::FRAMEBUFFER, glow::COLOR_ATTACHMENT0, glow::TEXTURE_2D, Some(t.tex), 0);
            let status = gl.check_framebuffer_status(glow::FRAMEBUFFER);
            if status == glow::FRAMEBUFFER_COMPLETE {
                // a new texture's pixels are undefined in GL; SDL's new target is transparent black. Nothing of the
                // frame's draws reads it yet, so they stay recorded
                self.gl_scissor(None);
                let gl = &self.gl;
                gl.clear_color(0.0, 0.0, 0.0, 0.0);
                gl.clear(glow::COLOR_BUFFER_BIT);
            }
            self.rebind_current();
            let gl = &self.gl;
            if status != glow::FRAMEBUFFER_COMPLETE {
                gl.delete_framebuffer(fbo);
                gl.delete_texture(t.tex);
                self.alive = self.alive.saturating_sub(1);
                return Err(format!("framebuffer incomplete: 0x{status:x}"));
            }
            t.fbo = Some(fbo);
        }
        Ok(t)
    }

    /// replaces a rectangle of `t`'s pixels (RGBA rows of `w`, top first)
    pub fn update(&mut self, t: &Texture, x: u32, y: u32, w: u32, h: u32, rgba: &[u8]) {
        self.update_pitch(t, x, y, w, h, rgba, w * 4);
    }

    /// `update` from rows `pitch` bytes apart (a part of a bigger picture: a streaming texture's buffer)
    #[allow(clippy::too_many_arguments)]
    pub fn update_pitch(&mut self, t: &Texture, x: u32, y: u32, w: u32, h: u32, rgba: &[u8], pitch: u32) {
        // the frame's earlier draws of it are drawn with its old pixels first
        self.touch(t.tex, diag::CUT_TEXTURE_OP);
        self.bound = None;
        self.stats.uploads += 1;
        self.stats.upload_bytes += w as u64 * h as u64 * 4;
        unsafe {
            let gl = &self.gl;
            gl.bind_texture(glow::TEXTURE_2D, Some(t.tex));
            gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 1);
            gl.pixel_store_i32(glow::UNPACK_ROW_LENGTH, (pitch / 4) as i32);
            gl.tex_sub_image_2d(
                glow::TEXTURE_2D,
                0,
                x as i32,
                y as i32,
                w as i32,
                h as i32,
                glow::RGBA,
                glow::UNSIGNED_BYTE,
                glow::PixelUnpackData::Slice(rgba),
            );
            gl.pixel_store_i32(glow::UNPACK_ROW_LENGTH, 0);
        }
        // an auto-mipmapped picture makes them again on its next shrinking draw
        if t.auto_mips && t.mips.get() == MIPS_OK {
            t.mips.set(MIPS_STALE);
        }
    }

    /// after the swap: a fence for this frame, then waits for the oldest while more than FRAMES_IN_FLIGHT are queued
    pub fn end_frame(&mut self) {
        unsafe {
            if let Ok(f) = self.gl.fence_sync(glow::SYNC_GPU_COMMANDS_COMPLETE, 0) {
                self.in_flight.push_back(f);
            }
            while self.in_flight.len() > FRAMES_IN_FLIGHT {
                let f = self.in_flight.pop_front().unwrap();
                let start = std::time::Instant::now();
                // a second at most per call, so a lost GPU never hangs the shell
                let r = self.gl.client_wait_sync(f, glow::SYNC_FLUSH_COMMANDS_BIT, 1_000_000_000);
                if r != glow::ALREADY_SIGNALED {
                    self.stats.fence_waits += 1;
                    self.stats.fence_wait += start.elapsed();
                }
                self.gl.delete_sync(f);
            }
        }
        if self.knobs.stats {
            if let Some(line) = self.stats.frame_done() {
                self.stats_line = Some(line);
            }
        }
    }

    pub fn textures_alive(&self) -> u32 {
        self.alive
    }

    pub fn free(&mut self, t: Texture) {
        // the frame's draws of it, or into it, are drawn before it goes
        if t.fbo.is_some_and(|f| self.targets.iter().any(|&(fbo, ..)| fbo == f)) {
            self.flush_frame(diag::CUT_TEXTURE_OP);
        } else {
            self.touch(t.tex, diag::CUT_TEXTURE_OP);
        }
        self.bound = None;
        self.alive = self.alive.saturating_sub(1);
        unsafe {
            if let Some(f) = t.fbo {
                // drawing into it: back to the screen, as SDL does when a target is destroyed
                if self.targets.iter().any(|&(fbo, ..)| fbo == f) {
                    self.targets.retain(|&(fbo, ..)| fbo != f);
                    self.rebind_current();
                }
                self.gl.delete_framebuffer(f);
            }
            self.gl.delete_texture(t.tex);
        }
    }

    // the transform, viewport and scissor are set in GL at once (a clear sees them) and kept in `state`, which the
    // frame's next command is recorded with
    unsafe fn set_xform(&mut self, sx: f32, sy: f32, tx: f32, ty: f32) {
        self.gl.uniform_4_f32(self.u_xform.as_ref(), sx, sy, tx, ty);
        self.state.xform = [sx, sy, tx, ty];
    }

    unsafe fn gl_viewport(&mut self, x: i32, y: i32, w: i32, h: i32) {
        self.gl.viewport(x, y, w, h);
        self.state.viewport = [x, y, w, h];
    }

    unsafe fn gl_scissor(&mut self, r: Option<[i32; 4]>) {
        match r {
            Some([x, y, w, h]) => {
                self.gl.enable(glow::SCISSOR_TEST);
                self.gl.scissor(x, y, w, h);
            }
            None => self.gl.disable(glow::SCISSOR_TEST),
        }
        self.state.scissor = r;
    }

    /// the frame starts: the screen cleared to black, the 1280x720 canvas as large as fits, centred
    pub fn begin_screen(&mut self, w: i32, h: i32) {
        self.draw_calls = 0;
        self.screen = (w, h);
        self.targets.clear();
        self.viewport = None;
        self.clip = None;
        let k = (w as f32 / CANVAS_W).min(h as f32 / CANVAS_H);
        let ox = ((w as f32 - CANVAS_W * k) / 2.0).floor();
        let oy = ((h as f32 - CANVAS_H * k) / 2.0).floor();
        self.canvas_scale = k;
        self.canvas_offset = (ox, oy);
        unsafe {
            self.gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            self.gl_scissor(None);
            self.gl_viewport(0, 0, w, h);
            let gl = &self.gl;
            gl.clear_color(0.0, 0.0, 0.0, 1.0);
            gl.clear(glow::COLOR_BUFFER_BIT);
            gl.use_program(Some(self.program));
            gl.bind_vertex_array(Some(self.vao));
            self.screen_xform();
        }
        self.bound = None;
    }

    /// the whole screen in canvas units: the canvas is letterboxed into it, this rect covers the bars too
    pub fn whole_screen(&self) -> Rect {
        let (w, h) = (self.screen.0 as f32, self.screen.1 as f32);
        let k = self.canvas_scale;
        let (ox, oy) = self.canvas_offset;
        [-ox / k, -oy / k, w / k, h / k]
    }

    /// the screen's size in pixels
    pub fn screen_size(&self) -> (i32, i32) {
        self.screen
    }

    unsafe fn screen_xform(&mut self) {
        let (w, h) = (self.screen.0 as f32, self.screen.1 as f32);
        let k = self.canvas_scale;
        let (ox, oy) = self.canvas_offset;
        // y down on the canvas, up in clip space
        self.set_xform(2.0 * k / w, -2.0 * k / h, 2.0 * ox / w - 1.0, 1.0 - 2.0 * oy / h);
    }

    /// draws into `t` (cleared to transparent black) until `end_target`. Its rows go top first, like an image's, so
    /// it is drawn with the same source rects as one.
    pub fn begin_target(&mut self, t: &Texture) {
        self.begin_target_cleared(t, [0.0; 4]);
    }

    /// begin_target, cleared to `clear`: text's colour at alpha 0 keeps the edges blended into it from going dark
    pub fn begin_target_cleared(&mut self, t: &Texture, clear: Color) {
        self.flush_frame(diag::CUT_TARGET);
        self.bound = None;
        self.stats.target_switches += 1;
        let (lw, lh) = t.size();
        let Some(fbo) = t.fbo else { return };
        self.targets.push((fbo, t.w as i32, t.h as i32, lw, lh));
        unsafe {
            self.gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
            self.gl_viewport(0, 0, t.w as i32, t.h as i32);
            let gl = &self.gl;
            gl.clear_color(clear[0], clear[1], clear[2], clear[3]);
            gl.clear(glow::COLOR_BUFFER_BIT);
            self.set_xform(2.0 / lw, 2.0 / lh, -1.0, -1.0);
        }
    }

    pub fn end_target(&mut self, t: &Texture) {
        self.flush_frame(diag::CUT_TARGET);
        self.bound = None;
        self.stats.target_switches += 1;
        if t.fbo.is_some() {
            self.targets.pop();
        }
        if t.mipmaps {
            self.stats.mipmaps += 1;
        }
        unsafe {
            if t.mipmaps {
                self.gl.bind_texture(glow::TEXTURE_2D, Some(t.tex));
                self.gl.generate_mipmap(glow::TEXTURE_2D);
                t.mips.set(MIPS_OK);
            }
            match self.targets.last() {
                Some(&(fbo, pw, ph, lw, lh)) => {
                    self.gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
                    self.gl_viewport(0, 0, pw, ph);
                    self.set_xform(2.0 / lw, 2.0 / lh, -1.0, -1.0);
                }
                None => {
                    self.gl.bind_framebuffer(glow::FRAMEBUFFER, None);
                    self.gl_viewport(0, 0, self.screen.0, self.screen.1);
                    self.screen_xform();
                }
            }
        }
    }

    // ---- output-pixel mode: what the C++ Renderer draws through (backend.rs) ----

    /// a frame in output pixels starts on the screen of `w` x `h` (nothing cleared: the Renderer clears): no canvas
    /// mapping, no target, the viewport and clip rect as they were set
    pub fn begin_frame(&mut self, w: i32, h: i32) {
        self.flush_frame(diag::CUT_PRESENT);
        self.draw_calls = 0;
        self.screen = (w, h);
        self.canvas_scale = 1.0;
        self.canvas_offset = (0.0, 0.0);
        self.targets.clear();
        unsafe {
            self.gl.use_program(Some(self.program));
            self.gl.bind_vertex_array(Some(self.vao));
            self.gl.active_texture(glow::TEXTURE0);
            self.gl.enable(glow::BLEND);
        }
        self.bound = None;
        self.rebind_current();
    }

    /// draws into `t` (None: the screen) until the next set_target, nothing cleared - SDL_SetRenderTarget. A target
    /// is addressed from its top-left pixel; the screen through its viewport
    pub fn set_target(&mut self, t: Option<&Texture>) {
        self.flush_frame(diag::CUT_TARGET);
        self.bound = None;
        self.stats.target_switches += 1;
        self.targets.clear();
        if let Some(t) = t {
            let Some(fbo) = t.fbo else { return self.rebind_current() };
            self.targets.push((fbo, t.w as i32, t.h as i32, t.w as f32, t.h as f32));
            // drawn into: the mipmaps made of it are out of date
            if t.mips.get() == MIPS_OK {
                t.mips.set(MIPS_STALE);
            }
        }
        self.rebind_current();
    }

    /// the screen's viewport (SDL_RenderSetViewport): drawing on the screen goes to `r`, addressed from its top-left
    /// corner, and stays inside it. None = the whole screen. Targets are always drawn whole, as SDL keeps a target's
    /// viewport apart
    pub fn set_viewport(&mut self, r: Option<PixelRect>) {
        self.stats.state_sets += 1;
        // the same rect again (every frame, and around each overlay): the bound state is right, the batch goes on
        if self.viewport == r {
            self.stats.state_unchanged += 1;
            return;
        }
        self.cut(diag::CUT_STATE);
        self.viewport = r;
        self.rebind_current();
    }

    /// the clip rect (SDL_RenderSetClipRect): pixels of the current target (on the screen: of the viewport) outside
    /// `r` are left alone. None = no clipping
    pub fn set_clip(&mut self, r: Option<PixelRect>) {
        self.stats.state_sets += 1;
        if self.clip == r {
            self.stats.state_unchanged += 1;
            return;
        }
        self.cut(diag::CUT_STATE);
        self.clip = r;
        self.rebind_current();
    }

    /// the size in pixels of what is drawn into: the target, or the screen's viewport
    pub fn current_size(&self) -> (i32, i32) {
        match self.targets.last() {
            Some(&(_, w, h, ..)) => (w, h),
            None => match self.viewport {
                Some([_, _, w, h]) => (w, h),
                None => self.screen,
            },
        }
    }

    // binds the target on top of `targets` (or the screen), its viewport, scissor and pixel transform
    fn rebind_current(&mut self) {
        unsafe {
            let (fb_h, ox, oy, w, h) = match self.targets.last() {
                Some(&(fbo, pw, ph, lw, lh)) => {
                    self.gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
                    self.gl_viewport(0, 0, pw, ph);
                    self.set_xform(2.0 / lw, 2.0 / lh, -1.0, -1.0);
                    (None, 0, 0, pw, ph)
                }
                None => {
                    self.gl.bind_framebuffer(glow::FRAMEBUFFER, None);
                    let [vx, vy, vw, vh] = self.viewport.unwrap_or([0, 0, self.screen.0, self.screen.1]);
                    // GL's window y runs up from the bottom
                    self.gl_viewport(vx, self.screen.1 - vy - vh, vw, vh);
                    if self.canvas_scale == 1.0 && self.canvas_offset == (0.0, 0.0) {
                        self.set_xform(2.0 / vw as f32, -2.0 / vh as f32, -1.0, 1.0);
                    } else {
                        self.screen_xform();
                    }
                    (Some(self.screen.1), vx, vy, vw, vh)
                }
            };
            match self.clip {
                Some([cx, cy, cw, ch]) => {
                    // the clip rect within the viewport (screen) or the target, cut to it
                    let x0 = (ox + cx).max(ox);
                    let y0 = (oy + cy).max(oy);
                    let x1 = (ox + cx + cw).min(ox + w);
                    let y1 = (oy + cy + ch).min(oy + h);
                    let (sw, sh) = ((x1 - x0).max(0), (y1 - y0).max(0));
                    match fb_h {
                        Some(sh_total) => self.gl_scissor(Some([x0, sh_total - y0 - sh, sw, sh])),
                        None => self.gl_scissor(Some([x0, y0, sw, sh])),
                    }
                }
                None => self.gl_scissor(None),
            }
        }
    }

    /// clears all of what is drawn into to `c` (SDL_RenderClear: the whole target, past the viewport and the clip)
    pub fn clear(&mut self, c: Color) {
        self.flush_frame(diag::CUT_CLEAR);
        unsafe {
            self.gl_scissor(None);
            let viewport = self.viewport.filter(|_| self.targets.is_empty());
            if viewport.is_some() {
                self.gl_viewport(0, 0, self.screen.0, self.screen.1);
            }
            let gl = &self.gl;
            gl.clear_color(c[0], c[1], c[2], c[3]);
            gl.clear(glow::COLOR_BUFFER_BIT);
        }
        self.rebind_current();
    }

    // the frame's next `n` vertices go to a command of `tex`, `blend` and `prim` drawn with the state as it is now:
    // the last one goes on when it is the same and has room, else a new one starts
    fn bind(&mut self, tex: glow::Texture, blend: Blend, prim: Prim, n: usize) {
        let state = self.state;
        if self.bound == Some((tex, blend, prim)) {
            let last = self.cmds.last().unwrap();
            if last.state != state {
                self.cut(diag::CUT_STATE);
            } else if prim == Prim::Triangles || last.count + n <= IBO_QUADS * 4 {
                return;
            } else {
                self.cut(diag::CUT_FULL);
            }
        } else {
            self.cut(diag::CUT_TEXTURE);
        }
        self.cmds.push(Cmd { first: self.verts.len(), count: 0, prim, tex, blend, state });
        self.bound = Some((tex, blend, prim));
    }

    /// draws what is batched
    pub fn flush(&mut self) {
        self.flush_frame(diag::CUT_PRESENT);
    }

    // ends the frame's command being added to, for the reason `why` (diag::CUT_*: the stats say what ends the
    // batches) - drawn later, by flush_frame
    fn cut(&mut self, why: usize) {
        if self.bound.take().is_none() {
            return;
        }
        let Some(c) = self.cmds.last() else { return };
        if c.count == 0 {
            self.cmds.pop();
            return;
        }
        self.stats.cuts[why] += 1;
        self.stats.draws += 1;
        self.stats.quads += match c.prim {
            Prim::Quads => c.count as u64 / 4,
            Prim::Triangles => c.count as u64 / 6,
        };
    }

    // cut, and the frame drawn first when one of its commands draws `tex` (it is about to change or go)
    fn touch(&mut self, tex: glow::Texture, why: usize) {
        self.cut(why);
        if self.cmds.iter().any(|c| c.tex == tex) {
            self.draw_frame();
        }
    }

    // cut, and the frame's commands drawn: before a target switch, a clear, a readback, the present
    fn flush_frame(&mut self, why: usize) {
        self.cut(why);
        self.draw_frame();
    }

    // the vertex attributes read from vertex `base` of the bound vertex buffer on
    unsafe fn vertex_pointers(gl: &glow::Context, base: usize) {
        let stride = std::mem::size_of::<Vertex>() as i32;
        let o = (base * std::mem::size_of::<Vertex>()) as i32;
        gl.vertex_attrib_pointer_f32(0, 2, glow::FLOAT, false, stride, o);
        gl.vertex_attrib_pointer_f32(1, 3, glow::FLOAT, false, stride, o + 8);
        gl.vertex_attrib_pointer_f32(2, 4, glow::FLOAT, false, stride, o + 20);
        gl.vertex_attrib_pointer_f32(3, 1, glow::FLOAT, false, stride, o + 36);
    }

    // the recorded commands drawn in their order: the frame's vertices in one upload to the next buffer of the ring,
    // then each command with its texture, blend and state set where they change from the one before
    fn draw_frame(&mut self) {
        if self.cmds.is_empty() {
            self.verts.clear();
            return;
        }
        let data = bytes(&self.verts);
        self.stats.flushes += 1;
        self.stats.vbo_uploads += 1;
        self.stats.vbo_upload_bytes += data.len() as u64;
        self.ring = (self.ring + 1) % VBO_RING;
        unsafe {
            let gl = &self.gl;
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(self.vbos[self.ring]));
            gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, data, glow::STREAM_DRAW);
            // the vertex the attribute pointers start at: a quad command draws from its index in the static index
            // buffer, so they move only when one starts off the four-vertex grid or past the buffer's 60000 vertices
            let mut base: Option<usize> = None;
            let mut tex: Option<glow::Texture> = None;
            let mut blend: Option<Blend> = None;
            let mut state: Option<DrawState> = None;
            for c in &self.cmds {
                if tex != Some(c.tex) {
                    gl.bind_texture(glow::TEXTURE_2D, Some(c.tex));
                    tex = Some(c.tex);
                }
                if blend != Some(c.blend) {
                    Self::blend_func(gl, c.blend);
                    blend = Some(c.blend);
                }
                if state != Some(c.state) {
                    Self::apply_state(gl, self.u_xform.as_ref(), &c.state, state.as_ref());
                    state = Some(c.state);
                }
                match c.prim {
                    Prim::Quads => {
                        let fits = base.is_some_and(|b| (c.first - b) % 4 == 0 && (c.first - b + c.count) <= IBO_QUADS * 4);
                        if !fits {
                            Self::vertex_pointers(gl, c.first);
                            base = Some(c.first);
                        }
                        let q0 = (c.first - base.unwrap()) / 4;
                        gl.draw_elements(glow::TRIANGLES, (c.count / 4 * 6) as i32, glow::UNSIGNED_SHORT, (q0 * 12) as i32);
                    }
                    Prim::Triangles => {
                        if base.is_none() {
                            Self::vertex_pointers(gl, c.first);
                            base = Some(c.first);
                        }
                        gl.draw_arrays(glow::TRIANGLES, (c.first - base.unwrap()) as i32, c.count as i32);
                    }
                }
            }
            // GL's state as it is set now again, for what comes before the next draw (a clear sees the scissor)
            Self::apply_state(gl, self.u_xform.as_ref(), &self.state, state.as_ref());
        }
        self.draw_calls += self.cmds.len() as u32;
        self.cmds.clear();
        self.verts.clear();
    }

    // `s` set in GL where it differs from `was` (None: all of it)
    unsafe fn apply_state(gl: &glow::Context, u_xform: Option<&glow::UniformLocation>, s: &DrawState, was: Option<&DrawState>) {
        if was.map(|w| w.xform) != Some(s.xform) {
            let [sx, sy, tx, ty] = s.xform;
            gl.uniform_4_f32(u_xform, sx, sy, tx, ty);
        }
        if was.map(|w| w.viewport) != Some(s.viewport) {
            let [x, y, w, h] = s.viewport;
            gl.viewport(x, y, w, h);
        }
        if was.map(|w| w.scissor) != Some(s.scissor) {
            match s.scissor {
                Some([x, y, w, h]) => {
                    if !matches!(was, Some(DrawState { scissor: Some(_), .. })) {
                        gl.enable(glow::SCISSOR_TEST);
                    }
                    gl.scissor(x, y, w, h);
                }
                None => gl.disable(glow::SCISSOR_TEST),
            }
        }
    }

    unsafe fn blend_func(gl: &glow::Context, blend: Blend) {
        match blend {
            Blend::Blend => gl.blend_func_separate(glow::SRC_ALPHA, glow::ONE_MINUS_SRC_ALPHA, glow::ONE, glow::ONE_MINUS_SRC_ALPHA),
            Blend::Premultiplied => gl.blend_func(glow::ONE, glow::ONE_MINUS_SRC_ALPHA),
            Blend::None => gl.blend_func(glow::ONE, glow::ZERO),
            Blend::Add => gl.blend_func_separate(glow::SRC_ALPHA, glow::ONE, glow::ZERO, glow::ONE),
            Blend::Mod => gl.blend_func_separate(glow::ZERO, glow::SRC_COLOR, glow::ZERO, glow::ONE),
            Blend::Mask => gl.blend_func_separate(glow::ZERO, glow::ONE, glow::ZERO, glow::SRC_ALPHA),
        }
    }

    fn push_quad(&mut self, v: [Vertex; 4]) {
        self.verts.extend_from_slice(&v);
        if let Some(c) = self.cmds.last_mut() {
            c.count += 4;
        }
    }

    // an auto-mipmapped texture drawn `shrink` times smaller than its pixels: its mipmaps made (again) first
    fn mips_for(&mut self, t: &Texture, shrink: f32) {
        if !t.auto_mips || !self.knobs.mipmaps || shrink <= MIPS_AT || t.mips.get() == MIPS_OK {
            return;
        }
        // the frame's earlier draws of it are drawn without the new mipmaps
        self.touch(t.tex, diag::CUT_MIPMAPS);
        self.bound = None;
        self.stats.mipmaps += 1;
        unsafe {
            let gl = &self.gl;
            gl.bind_texture(glow::TEXTURE_2D, Some(t.tex));
            gl.generate_mipmap(glow::TEXTURE_2D);
            if t.mips.get() == MIPS_NONE {
                gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, glow::LINEAR_MIPMAP_LINEAR as i32);
                if self.anisotropy > 0.0 {
                    gl.tex_parameter_f32(glow::TEXTURE_2D, TEXTURE_MAX_ANISOTROPY, self.anisotropy);
                }
            }
        }
        t.mips.set(MIPS_OK);
    }

    /// `src` of `t` (logical pixels of it; None = all of it) into `dst`, tinted (SDL's colour and alpha mod)
    pub fn copy(&mut self, t: &Texture, src: Option<Rect>, dst: Rect, tint: Color, blend: Blend) {
        self.copy_ex(t, src, dst, tint, blend, 0.0, None, false, false);
    }

    /// `copy` turned by `angle` degrees clockwise about `center` (relative to dst's top-left; None = its middle), and
    /// mirrored across or upside down - SDL_RenderCopyExF
    #[allow(clippy::too_many_arguments)]
    pub fn copy_ex(
        &mut self,
        t: &Texture,
        src: Option<Rect>,
        dst: Rect,
        tint: Color,
        blend: Blend,
        angle: f64,
        center: Option<[f32; 2]>,
        flip_h: bool,
        flip_v: bool,
    ) {
        let (lw, lh) = t.size();
        let s = src.unwrap_or([0.0, 0.0, lw, lh]);
        if dst[2] != 0.0 && dst[3] != 0.0 {
            let shrink = (s[2] * t.scale / dst[2].abs()).min(s[3] * t.scale / dst[3].abs());
            self.mips_for(t, shrink);
        }
        let (mut u0, mut v0, mut u1, mut v1) = (s[0] / lw, s[1] / lh, (s[0] + s[2]) / lw, (s[1] + s[3]) / lh);
        if flip_h {
            std::mem::swap(&mut u0, &mut u1);
        }
        if flip_v {
            std::mem::swap(&mut v0, &mut v1);
        }
        self.bind(t.tex, blend, Prim::Quads, 4);
        let [x, y, w, h] = dst;
        let (cx, cy) = match center {
            Some([cx, cy]) => (x + cx, y + cy),
            None => (x + w / 2.0, y + h / 2.0),
        };
        let (sin, cos) = if angle == 0.0 { (0.0, 1.0) } else { (angle.to_radians().sin() as f32, angle.to_radians().cos() as f32) };
        let vert = |px: f32, py: f32, u: f32, v: f32| {
            let (dx, dy) = (px - cx, py - cy);
            Vertex {
                pos: [cx + dx * cos - dy * sin, cy + dx * sin + dy * cos],
                uvq: [u, v, 1.0],
                color: tint,
                edge: -1.0,
            }
        };
        self.push_quad([
            vert(x, y, u0, v0),
            vert(x + w, y, u1, v0),
            vert(x, y + h, u0, v1),
            vert(x + w, y + h, u1, v1),
        ]);
    }

    /// a filled rect (SDL_RenderFillRect with the Blend draw mode)
    pub fn fill(&mut self, dst: Rect, color: Color) {
        self.fill_blend(dst, color, Blend::Blend);
    }

    /// a filled rect in a draw blend mode (SDL_SetRenderDrawBlendMode + SDL_RenderFillRect)
    pub fn fill_blend(&mut self, dst: Rect, color: Color, blend: Blend) {
        let white = self.white.tex;
        self.bind(white, blend, Prim::Quads, 4);
        let [x, y, w, h] = dst;
        let vert = |px: f32, py: f32| Vertex { pos: [px, py], uvq: [0.5, 0.5, 1.0], color, edge: -1.0 };
        self.push_quad([vert(x, y), vert(x + w, y), vert(x, y + h), vert(x + w, y + h)]);
    }

    /// a one-pixel line from pixel `a` to pixel `b`, both ends drawn (SDL_RenderDrawLine)
    pub fn line(&mut self, a: [f32; 2], b: [f32; 2], color: Color, blend: Blend) {
        if a[1] == b[1] || a[0] == b[0] {
            // across or down: the pixels' own rect
            let (x0, x1) = (a[0].min(b[0]), a[0].max(b[0]));
            let (y0, y1) = (a[1].min(b[1]), a[1].max(b[1]));
            return self.fill_blend([x0, y0, x1 - x0 + 1.0, y1 - y0 + 1.0], color, blend);
        }
        // a slope: a quad a pixel wide through the pixels' centres, half a pixel past each end
        let (ax, ay, bx, by) = (a[0] + 0.5, a[1] + 0.5, b[0] + 0.5, b[1] + 0.5);
        let len = ((bx - ax).powi(2) + (by - ay).powi(2)).sqrt();
        let (dx, dy) = ((bx - ax) / len * 0.5, (by - ay) / len * 0.5);
        let (nx, ny) = (-dy, dx);
        let white = self.white.tex;
        self.bind(white, blend, Prim::Quads, 4);
        let vert = |px: f32, py: f32| Vertex { pos: [px, py], uvq: [0.5, 0.5, 1.0], color, edge: -1.0 };
        self.push_quad([
            vert(ax - dx + nx, ay - dy + ny),
            vert(bx + dx + nx, by + dy + ny),
            vert(ax - dx - nx, ay - dy - ny),
            vert(bx + dx - nx, by + dy - ny),
        ]);
    }

    /// triangles of `t` (None: untextured), SDL_RenderGeometry: positions in pixels, uv 0..1 of the texture, the
    /// colours multiplying it
    pub fn geometry(&mut self, t: Option<&Texture>, verts: &[GeoVertex], indices: &[i32], blend: Blend) {
        if verts.is_empty() || verts.len() > 60000 {
            return;
        }
        let tex = t.map(|t| t.tex).unwrap_or(self.white.tex);
        let n = verts.len() as i32;
        let count = if indices.is_empty() { (n - n % 3) as usize } else { indices.len() - indices.len() % 3 };
        if count == 0 {
            return;
        }
        // triangles are drawn unindexed (the static index buffer is the quads'): each index's vertex in its place
        self.bind(tex, blend, Prim::Triangles, count);
        let textured = t.is_some();
        let vertex = |v: &GeoVertex| {
            let c = v.color;
            Vertex {
                pos: v.pos,
                uvq: if textured { [v.uv[0], v.uv[1], 1.0] } else { [0.5, 0.5, 1.0] },
                color: [c[0] as f32 / 255.0, c[1] as f32 / 255.0, c[2] as f32 / 255.0, c[3] as f32 / 255.0],
                edge: -1.0,
            }
        };
        if indices.is_empty() {
            self.verts.extend(verts[..count].iter().map(vertex));
        } else {
            self.verts.extend(indices[..count].iter().map(|&i| vertex(&verts[i.clamp(0, n - 1) as usize])));
        }
        if let Some(c) = self.cmds.last_mut() {
            c.count += count;
        }
    }

    /// `src` of `t` mapped onto the trapezoid between two vertical edges, perspective-correct (each edge's height is
    /// its nearness): Renderer::copyTrapezoidFaded - the tint from `top` at the top to `bottom` at the bottom, the
    /// source upside down with `flip`. A left edge right of the right one draws the source mirrored.
    #[allow(clippy::too_many_arguments)]
    pub fn trapezoid(
        &mut self,
        t: &Texture,
        src: Rect,
        left: Edge,
        right: Edge,
        top: Color,
        bottom: Color,
        flip: bool,
    ) {
        self.trapezoid_blend(t, src, left, right, top, bottom, flip, Blend::Blend);
    }

    /// `trapezoid` in a blend mode (the texture's own, in the C++ Renderer)
    #[allow(clippy::too_many_arguments)]
    pub fn trapezoid_blend(
        &mut self,
        t: &Texture,
        src: Rect,
        left: Edge,
        right: Edge,
        top: Color,
        bottom: Color,
        flip: bool,
        blend: Blend,
    ) {
        let lh = left.bottom - left.top;
        let rh = right.bottom - right.top;
        if (right.x - left.x).abs() < 1.0 / self.canvas_scale.max(0.01) || lh <= 0.0 || rh <= 0.0 {
            return;
        }
        let (tw, th) = t.size();
        // a cover drawn smaller than its pixels (mostly on its height, the trapezoid's taller side)
        self.mips_for(t, src[3] * t.scale / (lh.max(rh) * self.canvas_scale.max(0.01)));
        let (u0, u1) = (src[0] / tw, (src[0] + src[2]) / tw);
        let (mut v0, mut v1) = (src[1] / th, (src[1] + src[3]) / th);
        if flip {
            std::mem::swap(&mut v0, &mut v1);
        }
        self.bind(t.tex, blend, Prim::Quads, 4);
        // the texture coordinates times each edge's height: the shader divides them back, so 1/depth runs
        // linearly across the screen and the picture does not bend along the diagonal
        let vert = |e: &Edge, y: f32, u: f32, v: f32, q: f32, color: Color, edge: f32| Vertex {
            pos: [e.x, y],
            uvq: [u * q, v * q, q],
            color,
            edge,
        };
        self.push_quad([
            vert(&left, left.top, u0, v0, lh, top, 0.0),
            vert(&right, right.top, u1, v0, rh, top, 0.0),
            vert(&left, left.bottom, u0, v1, lh, bottom, 1.0),
            vert(&right, right.bottom, u1, v1, rh, bottom, 1.0),
        ]);
    }

    /// the back buffer as RGBA rows, top first
    pub fn read_screen(&mut self) -> (u32, u32, Vec<u8>) {
        self.flush_frame(diag::CUT_READBACK);
        let (w, h) = (self.screen.0 as u32, self.screen.1 as u32);
        self.stats.readbacks += 1;
        self.stats.readback_bytes += w as u64 * h as u64 * 4;
        let mut px = vec![0u8; (w * h * 4) as usize];
        unsafe {
            self.gl.read_pixels(
                0,
                0,
                w as i32,
                h as i32,
                glow::RGBA,
                glow::UNSIGNED_BYTE,
                glow::PixelPackData::Slice(&mut px),
            );
        }
        let row = (w * 4) as usize;
        let mut flipped = Vec::with_capacity(px.len());
        for y in (0..h as usize).rev() {
            flipped.extend_from_slice(&px[y * row..(y + 1) * row]);
        }
        for p in flipped.chunks_mut(4) {
            p[3] = 255;
        }
        (w, h, flipped)
    }

    /// `r` (pixels, y down; None = all of it) of what is drawn into - the screen (past its viewport, as
    /// SDL_RenderReadPixels reads the whole output) or the target - as RGBA rows, top first, alpha as it is
    pub fn read_pixels(&mut self, r: Option<PixelRect>) -> (u32, u32, Vec<u8>) {
        self.flush_frame(diag::CUT_READBACK);
        let on_screen = self.targets.is_empty();
        let (fw, fh) = match self.targets.last() {
            Some(&(_, w, h, ..)) => (w, h),
            None => self.screen,
        };
        let [x, y, w, h] = r.unwrap_or([0, 0, fw, fh]);
        let (w, h) = (w.max(0), h.max(0));
        let mut px = vec![0u8; (w * h * 4) as usize];
        if w == 0 || h == 0 {
            return (0, 0, px);
        }
        let gl_y = if on_screen { fh - y - h } else { y };
        self.stats.readbacks += 1;
        self.stats.readback_bytes += px.len() as u64;
        unsafe {
            self.gl.pixel_store_i32(glow::PACK_ALIGNMENT, 1);
            self.gl.read_pixels(
                x,
                gl_y,
                w,
                h,
                glow::RGBA,
                glow::UNSIGNED_BYTE,
                glow::PixelPackData::Slice(&mut px),
            );
        }
        if on_screen {
            // GL's rows run up from the bottom of the screen; a target's are already top first
            let row = (w * 4) as usize;
            let mut flipped = Vec::with_capacity(px.len());
            for y in (0..h as usize).rev() {
                flipped.extend_from_slice(&px[y * row..(y + 1) * row]);
            }
            px = flipped;
        }
        (w as u32, h as u32, px)
    }
}
