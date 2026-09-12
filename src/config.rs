//! Shared subset of KDE's INI config: simple scalar entries in [OCR].
use std::{
    env, fs, io,
    path::{Path, PathBuf},
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Settings {
    pub threads: usize,
    pub max_side: u32,
}
impl Default for Settings {
    fn default() -> Self {
        Self {
            threads: 4,
            max_side: 1536,
        }
    }
}
pub fn path() -> PathBuf {
    env::var_os("SPECTACLE_OCR_CONFIG")
        .filter(|v| !v.is_empty())
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            env::var_os("XDG_CONFIG_HOME")
                .filter(|v| !v.is_empty())
                .map(PathBuf::from)
                .unwrap_or_else(|| {
                    PathBuf::from(env::var_os("HOME").unwrap_or_default()).join(".config")
                })
                .join("spectacle-ocrrc")
        })
}
pub fn read(path: &Path) -> io::Result<Settings> {
    match fs::read_to_string(path) {
        Ok(text) => parse(&text),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(Settings::default()),
        Err(e) => Err(e),
    }
}
fn parse(text: &str) -> io::Result<Settings> {
    if text.len() > 65536 {
        return Err(crate::invalid("configuration exceeds 64 KiB"));
    }
    let mut settings = Settings::default();
    let mut in_ocr = false;
    for line in text.lines().map(str::trim) {
        if line.starts_with('[') {
            in_ocr = line == "[OCR]";
            continue;
        }
        if !in_ocr || line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        if let Some((key, value)) = line.split_once('=') {
            match key.trim() {
                "Threads" => {
                    settings.threads = value
                        .trim()
                        .parse()
                        .map_err(|_| crate::invalid("invalid Threads setting"))?
                }
                "MaxSide" => {
                    settings.max_side = value
                        .trim()
                        .parse()
                        .map_err(|_| crate::invalid("invalid MaxSide setting"))?
                }
                _ => (),
            }
        }
    }
    if !(1..=32).contains(&settings.threads) || !(320..=2048).contains(&settings.max_side) {
        return Err(crate::invalid(
            "configuration value outside supported range",
        ));
    }
    Ok(settings)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn kde_defaults_groups_and_ranges() {
        assert_eq!(parse("").unwrap(), Settings::default());
        assert_eq!(
            parse("[Other]\nThreads=99\n[OCR]\nThreads=6\nMaxSide=2048\nEnabled=false\n").unwrap(),
            Settings {
                threads: 6,
                max_side: 2048
            }
        );
        assert!(parse("[OCR]\nThreads=0").is_err());
        assert!(parse("[OCR]\nMaxSide=invalid").is_err());
    }
}
