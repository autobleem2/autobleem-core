//! The OpenGL ES 3.0 renderer on glow: textured quads and perspective trapezoids, batched, in the launcher's logical
//! 1280x720 pixels, drawn to the screen or into a texture (a cover is composed into one, as the C++ launcher composes
//! it into an SDL render target).
//!
//! The blending is SDL's, so a picture composes and draws as it does in the C++ launcher: `Blend` is
//! SDL_BLENDMODE_BLEND (colour by source alpha, alpha "over"), `Premultiplied` SDL_BLENDMODE_BLEND_PREMULTIPLIED. A
//! colour on a draw is SDL's colour and alpha mod. A trapezoid's sloping top and bottom are smoothed in the shader
//! (a pixel's coverage from fwidth): a turned cover has no stepped edges without multisampling, which the C++
//! renderer got from a one-pixel skirt round each strip.
use glow::HasContext;

const VS: &str = r#"#version 300 es
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

const FS: &str = r#"#version 300 es
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
}

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
}

impl Texture {
    /// its size in logical pixels
    pub fn size(&self) -> (f32, f32) {
        (self.w as f32 / self.scale, self.h as f32 / self.scale)
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

#[repr(C)]
#[derive(Clone, Copy)]
struct Vertex {
    pos: [f32; 2],
    uvq: [f32; 3],
    color: [f32; 4],
    edge: f32,
}

pub struct Gfx {
    pub gl: glow::Context,
    program: glow::Program,
    vao: glow::VertexArray,
    vbo: glow::Buffer,
    ibo: glow::Buffer,
    u_xform: Option<glow::UniformLocation>,
    white: Texture,
    verts: Vec<Vertex>,
    indices: Vec<u16>,
    bound: Option<(glow::Texture, Blend)>,
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
}

/// end_frame: how many frames may be queued before the next one waits. A driver whose swap does not wait (Mesa's
/// llvmpipe offscreen, a VM) otherwise takes frames faster than it draws them, and whatever has to wait for the GPU
/// (a texture changed, mipmaps made) waits for the whole queue - seconds of it on the VM
const FRAMES_IN_FLIGHT: usize = 2;

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
        let program = program(&gl, VS, FS)?;
        let vao = gl.create_vertex_array()?;
        gl.bind_vertex_array(Some(vao));
        let vbo = gl.create_buffer()?;
        gl.bind_buffer(glow::ARRAY_BUFFER, Some(vbo));
        let stride = std::mem::size_of::<Vertex>() as i32;
        gl.enable_vertex_attrib_array(0);
        gl.vertex_attrib_pointer_f32(0, 2, glow::FLOAT, false, stride, 0);
        gl.enable_vertex_attrib_array(1);
        gl.vertex_attrib_pointer_f32(1, 3, glow::FLOAT, false, stride, 8);
        gl.enable_vertex_attrib_array(2);
        gl.vertex_attrib_pointer_f32(2, 4, glow::FLOAT, false, stride, 20);
        gl.enable_vertex_attrib_array(3);
        gl.vertex_attrib_pointer_f32(3, 1, glow::FLOAT, false, stride, 36);
        let ibo = gl.create_buffer()?;
        gl.bind_buffer(glow::ELEMENT_ARRAY_BUFFER, Some(ibo));
        gl.use_program(Some(program));
        gl.uniform_1_i32(gl.get_uniform_location(program, "u_tex").as_ref(), 0);
        let u_xform = gl.get_uniform_location(program, "u_xform");
        gl.enable(glow::BLEND);
        gl.disable(glow::CULL_FACE);
        gl.disable(glow::DEPTH_TEST);
        gl.active_texture(glow::TEXTURE0);
        let white = Self::upload(&gl, 1, 1, &[255, 255, 255, 255], 1.0, false)?;
        Ok(Gfx {
            gl,
            program,
            vao,
            vbo,
            ibo,
            u_xform,
            white,
            verts: Vec::with_capacity(4096),
            indices: Vec::with_capacity(6144),
            bound: None,
            screen: (CANVAS_W as i32, CANVAS_H as i32),
            canvas_scale: 1.0,
            canvas_offset: (0.0, 0.0),
            draw_calls: 0,
            alive: 0,
            in_flight: std::collections::VecDeque::new(),
            targets: Vec::new(),
        })
    }

    unsafe fn upload(
        gl: &glow::Context,
        w: u32,
        h: u32,
        rgba: &[u8],
        scale: f32,
        mipmaps: bool,
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
        if mipmaps && !rgba.is_empty() {
            gl.generate_mipmap(glow::TEXTURE_2D);
        }
        Ok(Texture { tex: t, w, h, scale, fbo: None, mipmaps })
    }

    /// an image (straight RGBA, rows top first) of `scale` pixels per logical pixel
    pub fn texture(&mut self, w: u32, h: u32, rgba: &[u8], scale: f32, mipmaps: bool) -> Result<Texture, String> {
        self.flush();
        self.bound = None;
        let t = unsafe { Self::upload(&self.gl, w, h, rgba, scale, mipmaps) }?;
        self.alive += 1;
        Ok(t)
    }

    /// an image scaled by whole pixels when drawn (nearest): the performance overlay's bitmap text
    pub fn texture_nearest(&mut self, w: u32, h: u32, rgba: &[u8]) -> Result<Texture, String> {
        let t = self.texture(w, h, rgba, 1.0, false)?;
        unsafe {
            self.gl.bind_texture(glow::TEXTURE_2D, Some(t.tex));
            self.gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, glow::NEAREST as i32);
            self.gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAG_FILTER, glow::NEAREST as i32);
        }
        self.bound = None;
        Ok(t)
    }

    /// a texture to draw into (`begin_target`), `w` x `h` logical pixels at `scale` pixels each, mipmapped
    pub fn target(&mut self, w: f32, h: f32, scale: f32) -> Result<Texture, String> {
        self.make_target(w, h, scale, true)
    }

    /// a target without mipmaps, drawn at its own size: a screen's picture (the backdrop, a transition's two)
    pub fn target_plain(&mut self, w: f32, h: f32, scale: f32) -> Result<Texture, String> {
        self.make_target(w, h, scale, false)
    }

    fn make_target(&mut self, w: f32, h: f32, scale: f32, mipmaps: bool) -> Result<Texture, String> {
        let pw = (w * scale).round().max(1.0) as u32;
        let ph = (h * scale).round().max(1.0) as u32;
        let mut t = self.texture(pw, ph, &[], scale, mipmaps)?;
        unsafe {
            let gl = &self.gl;
            if mipmaps {
                gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, glow::LINEAR_MIPMAP_LINEAR as i32);
            }
            let fbo = gl.create_framebuffer()?;
            gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
            gl.framebuffer_texture_2d(glow::FRAMEBUFFER, glow::COLOR_ATTACHMENT0, glow::TEXTURE_2D, Some(t.tex), 0);
            let status = gl.check_framebuffer_status(glow::FRAMEBUFFER);
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            if status != glow::FRAMEBUFFER_COMPLETE {
                gl.delete_framebuffer(fbo);
                gl.delete_texture(t.tex);
                return Err(format!("framebuffer incomplete: 0x{status:x}"));
            }
            t.fbo = Some(fbo);
        }
        Ok(t)
    }

    /// replaces a rectangle of `t`'s pixels (RGBA rows of `w`, top first)
    pub fn update(&mut self, t: &Texture, x: u32, y: u32, w: u32, h: u32, rgba: &[u8]) {
        self.flush();
        self.bound = None;
        unsafe {
            let gl = &self.gl;
            gl.bind_texture(glow::TEXTURE_2D, Some(t.tex));
            gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 1);
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
                // a second at most per call, so a lost GPU never hangs the shell
                self.gl.client_wait_sync(f, glow::SYNC_FLUSH_COMMANDS_BIT, 1_000_000_000);
                self.gl.delete_sync(f);
            }
        }
    }

    pub fn textures_alive(&self) -> u32 {
        self.alive
    }

    pub fn free(&mut self, t: Texture) {
        self.flush();
        self.bound = None;
        self.alive = self.alive.saturating_sub(1);
        unsafe {
            if let Some(f) = t.fbo {
                self.gl.delete_framebuffer(f);
            }
            self.gl.delete_texture(t.tex);
        }
    }

    unsafe fn set_xform(&self, sx: f32, sy: f32, tx: f32, ty: f32) {
        self.gl.uniform_4_f32(self.u_xform.as_ref(), sx, sy, tx, ty);
    }

    /// the frame starts: the screen cleared to black, the 1280x720 canvas as large as fits, centred
    pub fn begin_screen(&mut self, w: i32, h: i32) {
        self.draw_calls = 0;
        self.screen = (w, h);
        self.targets.clear();
        let k = (w as f32 / CANVAS_W).min(h as f32 / CANVAS_H);
        let ox = ((w as f32 - CANVAS_W * k) / 2.0).floor();
        let oy = ((h as f32 - CANVAS_H * k) / 2.0).floor();
        self.canvas_scale = k;
        self.canvas_offset = (ox, oy);
        unsafe {
            let gl = &self.gl;
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            gl.viewport(0, 0, w, h);
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

    unsafe fn screen_xform(&self) {
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
        self.flush();
        self.bound = None;
        let (lw, lh) = t.size();
        let Some(fbo) = t.fbo else { return };
        self.targets.push((fbo, t.w as i32, t.h as i32, lw, lh));
        unsafe {
            let gl = &self.gl;
            gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
            gl.viewport(0, 0, t.w as i32, t.h as i32);
            gl.clear_color(clear[0], clear[1], clear[2], clear[3]);
            gl.clear(glow::COLOR_BUFFER_BIT);
            self.set_xform(2.0 / lw, 2.0 / lh, -1.0, -1.0);
        }
    }


    pub fn end_target(&mut self, t: &Texture) {
        self.flush();
        self.bound = None;
        if t.fbo.is_some() {
            self.targets.pop();
        }
        unsafe {
            let gl = &self.gl;
            if t.mipmaps {
                gl.bind_texture(glow::TEXTURE_2D, Some(t.tex));
                gl.generate_mipmap(glow::TEXTURE_2D);
            }
            match self.targets.last() {
                Some(&(fbo, pw, ph, lw, lh)) => {
                    gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
                    gl.viewport(0, 0, pw, ph);
                    self.set_xform(2.0 / lw, 2.0 / lh, -1.0, -1.0);
                }
                None => {
                    gl.bind_framebuffer(glow::FRAMEBUFFER, None);
                    gl.viewport(0, 0, self.screen.0, self.screen.1);
                    self.screen_xform();
                }
            }
        }
    }

    fn bind(&mut self, tex: glow::Texture, blend: Blend) {
        if self.bound == Some((tex, blend)) && self.verts.len() < 60000 {
            return;
        }
        self.flush();
        unsafe {
            let gl = &self.gl;
            gl.bind_texture(glow::TEXTURE_2D, Some(tex));
            match blend {
                Blend::Blend => gl.blend_func_separate(
                    glow::SRC_ALPHA,
                    glow::ONE_MINUS_SRC_ALPHA,
                    glow::ONE,
                    glow::ONE_MINUS_SRC_ALPHA,
                ),
                Blend::Premultiplied => gl.blend_func(glow::ONE, glow::ONE_MINUS_SRC_ALPHA),
            }
        }
        self.bound = Some((tex, blend));
    }

    /// draws what is batched
    pub fn flush(&mut self) {
        if self.indices.is_empty() {
            return;
        }
        unsafe {
            let gl = &self.gl;
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(self.vbo));
            gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, bytes(&self.verts), glow::STREAM_DRAW);
            gl.bind_buffer(glow::ELEMENT_ARRAY_BUFFER, Some(self.ibo));
            gl.buffer_data_u8_slice(glow::ELEMENT_ARRAY_BUFFER, bytes(&self.indices), glow::STREAM_DRAW);
            gl.draw_elements(glow::TRIANGLES, self.indices.len() as i32, glow::UNSIGNED_SHORT, 0);
        }
        self.draw_calls += 1;
        self.verts.clear();
        self.indices.clear();
    }

    fn push_quad(&mut self, v: [Vertex; 4]) {
        let base = self.verts.len() as u16;
        self.verts.extend_from_slice(&v);
        self.indices.extend_from_slice(&[base, base + 1, base + 2, base + 2, base + 1, base + 3]);
    }

    /// `src` of `t` (logical pixels of it; None = all of it) into `dst`, tinted (SDL's colour and alpha mod)
    pub fn copy(&mut self, t: &Texture, src: Option<Rect>, dst: Rect, tint: Color, blend: Blend) {
        let (lw, lh) = t.size();
        let s = src.unwrap_or([0.0, 0.0, lw, lh]);
        let (u0, v0, u1, v1) = (s[0] / lw, s[1] / lh, (s[0] + s[2]) / lw, (s[1] + s[3]) / lh);
        self.bind(t.tex, blend);
        let [x, y, w, h] = dst;
        let vert = |px: f32, py: f32, u: f32, v: f32| Vertex {
            pos: [px, py],
            uvq: [u, v, 1.0],
            color: tint,
            edge: -1.0,
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
        let white = self.white.tex;
        self.bind(white, Blend::Blend);
        let [x, y, w, h] = dst;
        let vert = |px: f32, py: f32| Vertex { pos: [px, py], uvq: [0.5, 0.5, 1.0], color, edge: -1.0 };
        self.push_quad([vert(x, y), vert(x + w, y), vert(x, y + h), vert(x + w, y + h)]);
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
        let lh = left.bottom - left.top;
        let rh = right.bottom - right.top;
        if (right.x - left.x).abs() < 1.0 / self.canvas_scale.max(0.01) || lh <= 0.0 || rh <= 0.0 {
            return;
        }
        let (tw, th) = t.size();
        let (u0, u1) = (src[0] / tw, (src[0] + src[2]) / tw);
        let (mut v0, mut v1) = (src[1] / th, (src[1] + src[3]) / th);
        if flip {
            std::mem::swap(&mut v0, &mut v1);
        }
        self.bind(t.tex, Blend::Blend);
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
        self.flush();
        let (w, h) = (self.screen.0 as u32, self.screen.1 as u32);
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
}
