//! ab_gl: the launcher's OpenGL ES 3.0 renderer (gfx, on glow) and, over it, the loadable renderer backend lib_ableem's
//! Renderer draws through with ABLEEM_RENDER_GL (backend, the C table of src/ui/render_backend.h). Built as an rlib
//! for the Rust shell and as a cdylib that CMake copies as ab_render_gles3.so / .dll (rust/ab_gl.cmake).
pub mod backend;
pub mod diag;
pub mod gfx;

pub use glow;
