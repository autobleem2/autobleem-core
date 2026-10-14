//! The renderer's diagnostics: three knobs read once at start, and the counters AB_GL_STATS=1 logs once a second.
//!
//! The knobs come from the environment, else from the file /media/System/Data/gl.env (KEY=VALUE lines, # starts a
//! comment) - the console's launcher is started by the stick's scripts, so a line in that file changes it without a
//! rebuild. A variable in the environment wins over the file's line.
//! - AB_GL_MIPMAPS=0: no mipmaps at all (every picture filtered linearly from its own pixels)
//! - AB_GL_ANISO=0|2|4: the most anisotropic filtering a mipmapped picture gets (default 4; never past the GPU's)
//! - AB_GL_STATS=1: one line a second for the log (Stats::line): frames, and per frame the draw calls, quads, uploads,
//!   mipmaps made, target switches, readbacks, fence waits and why each batch was cut
use std::time::{Duration, Instant};

/// the file the console's knobs may be written in
pub const KNOBS_FILE: &str = "/media/System/Data/gl.env";

pub struct Knobs {
    pub mipmaps: bool,
    pub aniso_cap: f32,
    pub stats: bool,
    /// where they came from, for the start line: "default", "environment", the file, or both
    pub from: String,
}

fn file_value(text: &str, key: &str) -> Option<String> {
    text.lines().find_map(|l| {
        let l = l.trim();
        if l.starts_with('#') {
            return None;
        }
        let (k, v) = l.split_once('=')?;
        let k = k.trim().strip_prefix("export ").unwrap_or(k.trim()).trim();
        (k == key).then(|| v.trim().trim_matches('"').to_string())
    })
}

impl Knobs {
    pub fn read() -> Knobs {
        Self::parse(|k| std::env::var(k).ok(), std::fs::read_to_string(KNOBS_FILE).ok().as_deref())
    }

    /// the knobs from `env` (a variable's value) over `file` (the knobs file's text)
    pub fn parse(env: impl Fn(&str) -> Option<String>, file: Option<&str>) -> Knobs {
        let mut sources: Vec<&str> = Vec::new();
        let mut get = |key: &str| {
            if let Some(v) = env(key).filter(|v| !v.is_empty()) {
                if !sources.contains(&"environment") {
                    sources.push("environment");
                }
                return Some(v);
            }
            let v = file.and_then(|t| file_value(t, key)).filter(|v| !v.is_empty())?;
            if !sources.contains(&KNOBS_FILE) {
                sources.push(KNOBS_FILE);
            }
            Some(v)
        };
        let mipmaps = get("AB_GL_MIPMAPS").map(|v| v != "0").unwrap_or(true);
        let aniso_cap = get("AB_GL_ANISO").and_then(|v| v.parse::<f32>().ok()).map(|v| v.max(0.0)).unwrap_or(4.0);
        let stats = get("AB_GL_STATS").map(|v| v != "0").unwrap_or(false);
        let from = if sources.is_empty() { "default".to_string() } else { sources.join(" + ") };
        Knobs { mipmaps, aniso_cap, stats, from }
    }
}

// why a batch was drawn (Stats::cuts)
pub const CUT_TEXTURE: usize = 0; // another texture or blend mode
pub const CUT_TARGET: usize = 1; // set_target, begin_target / end_target
pub const CUT_STATE: usize = 2; // viewport or clip rect
pub const CUT_CLEAR: usize = 3;
pub const CUT_TEXTURE_OP: usize = 4; // a texture made, updated, freed or its filter set
pub const CUT_MIPMAPS: usize = 5;
pub const CUT_READBACK: usize = 6;
pub const CUT_PRESENT: usize = 7; // the caller's flush / end of frame
pub const CUT_FULL: usize = 8; // the batch's buffer full
const CUTS: usize = 9;
const CUT_NAMES: [&str; CUTS] = ["texture/blend", "target", "viewport/clip", "clear", "texture op", "mipmaps", "readback", "present", "full"];

/// what was done since the last line
#[derive(Default)]
pub struct Stats {
    pub frames: u64,
    pub draws: u64,
    pub quads: u64,
    pub uploads: u64,
    pub upload_bytes: u64,
    pub mipmaps: u64,
    pub target_switches: u64,
    pub readbacks: u64,
    pub readback_bytes: u64,
    pub finishes: u64,
    pub fence_waits: u64,
    pub fence_wait: Duration,
    /// viewport / clip rects set, and of those the ones that changed nothing
    pub state_sets: u64,
    pub state_unchanged: u64,
    pub cuts: [u64; CUTS],
    since: Option<Instant>,
}

impl Stats {
    /// after a frame: the line for the log when a second has passed since the last one (the counters start again)
    pub fn frame_done(&mut self) -> Option<String> {
        self.frames += 1;
        let now = Instant::now();
        let since = *self.since.get_or_insert(now);
        let secs = now.duration_since(since).as_secs_f64();
        if secs < 1.0 {
            return None;
        }
        let line = self.line(secs);
        *self = Stats { since: Some(now), ..Stats::default() };
        Some(line)
    }

    fn line(&self, secs: f64) -> String {
        let n = self.frames.max(1) as f64;
        let per = |v: u64| v as f64 / n;
        let cuts: Vec<String> = CUT_NAMES.iter().zip(self.cuts).filter(|&(_, c)| c > 0).map(|(name, c)| format!("{name} {:.1}", per(c))).collect();
        format!(
            "GL stats: {:.1} fps | per frame: {:.1} draws, {:.0} quads, {:.2} uploads ({:.0} KB), {:.2} mipmaps, {:.1} target \
             switches, {:.2} readbacks ({:.0} KB), {:.2} finish, {:.2} fence waits ({:.2} ms), {:.1} viewport/clip sets \
             ({:.1} unchanged) | batches cut by: {}",
            self.frames as f64 / secs,
            per(self.draws),
            per(self.quads),
            per(self.uploads),
            per(self.upload_bytes) / 1024.0,
            per(self.mipmaps),
            per(self.target_switches),
            per(self.readbacks),
            per(self.readback_bytes) / 1024.0,
            per(self.finishes),
            per(self.fence_waits),
            self.fence_wait.as_secs_f64() * 1000.0 / n,
            per(self.state_sets),
            per(self.state_unchanged),
            if cuts.is_empty() { "-".to_string() } else { cuts.join(", ") }
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn env<'a>(vars: &'a [(&'a str, &'a str)]) -> impl Fn(&str) -> Option<String> + 'a {
        move |k| vars.iter().find(|(n, _)| *n == k).map(|(_, v)| v.to_string())
    }

    #[test]
    fn defaults_without_env_or_file() {
        let k = Knobs::parse(env(&[]), None);
        assert!(k.mipmaps && !k.stats);
        assert_eq!(k.aniso_cap, 4.0);
        assert_eq!(k.from, "default");
    }

    #[test]
    fn the_file_sets_them_and_the_environment_wins() {
        let file = "# knobs\nAB_GL_MIPMAPS=0\nexport AB_GL_ANISO=2\nAB_GL_STATS = 1\n";
        let k = Knobs::parse(env(&[]), Some(file));
        assert!(!k.mipmaps && k.stats);
        assert_eq!(k.aniso_cap, 2.0);
        assert_eq!(k.from, KNOBS_FILE);
        let k = Knobs::parse(env(&[("AB_GL_ANISO", "0"), ("AB_GL_MIPMAPS", "1")]), Some(file));
        assert!(k.mipmaps && k.stats);
        assert_eq!(k.aniso_cap, 0.0);
        assert_eq!(k.from, format!("environment + {KNOBS_FILE}"));
    }

    #[test]
    fn the_line_is_per_frame() {
        let mut s = Stats { frames: 10, draws: 50, quads: 1000, cuts: [30, 10, 0, 0, 0, 0, 0, 10, 0], ..Stats::default() };
        let l = s.line(1.0);
        assert!(l.contains("10.0 fps"), "{l}");
        assert!(l.contains("5.0 draws"), "{l}");
        assert!(l.contains("100 quads"), "{l}");
        assert!(l.contains("texture/blend 3.0, target 1.0, present 1.0"), "{l}");
        s.frames = 0;
        assert!(s.frame_done().is_none());
    }
}
