//! Spectacle OCR service and standalone image recognition CLI.
mod config;
mod ocr;
use std::env;
use std::fs;
use std::io::{self, Read, Write};
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::PathBuf;
use std::time::Duration;

const MAGIC: &[u8; 8] = b"SOCR0001";
const MAX_IMAGE: usize = 64 * 1024 * 1024;
const MAX_TEXT: usize = 1024 * 1024;

struct Image {
    width: u32,
    height: u32,
    rgb: Vec<u8>,
}

fn invalid(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}

fn read_image(reader: &mut impl Read) -> io::Result<Image> {
    let mut header = [0u8; 20];
    reader.read_exact(&mut header)?;
    if &header[..8] != MAGIC {
        return Err(invalid("unsupported protocol"));
    }
    let number = |offset| u32::from_le_bytes(header[offset..offset + 4].try_into().unwrap());
    let (width, height, length) = (number(8), number(12), number(16) as usize);
    let expected = u64::from(width)
        .checked_mul(u64::from(height))
        .and_then(|pixels| pixels.checked_mul(3))
        .ok_or_else(|| invalid("image dimensions overflow"))?;
    if width == 0 || height == 0 || length > MAX_IMAGE || expected != length as u64 {
        return Err(invalid("invalid RGB dimensions or payload length"));
    }
    let mut rgb = vec![0; length];
    reader.read_exact(&mut rgb)?;
    Ok(Image { width, height, rgb })
}

trait Backend {
    fn recognize(&mut self, image: &Image) -> io::Result<String>;
}

fn respond(writer: &mut impl Write, status: u32, text: &str) -> io::Result<()> {
    if text.len() > MAX_TEXT || text.contains('\0') {
        return Err(invalid("invalid response text"));
    }
    writer.write_all(MAGIC)?;
    writer.write_all(&status.to_le_bytes())?;
    writer.write_all(&(text.len() as u32).to_le_bytes())?;
    writer.write_all(text.as_bytes())
}

fn handle(mut stream: UnixStream, backend: &mut impl Backend) -> io::Result<()> {
    stream.set_read_timeout(Some(Duration::from_secs(3)))?;
    stream.set_write_timeout(Some(Duration::from_secs(3)))?;
    let image = read_image(&mut stream)?;
    match backend.recognize(&image) {
        Ok(text) => respond(&mut stream, 0, &text),
        Err(error) => respond(&mut stream, 1, &error.to_string()),
    }
}

struct ReloadingBackend {
    backend: ocr::OcrBackend,
    assets: PathBuf,
    config_path: PathBuf,
    applied: config::Settings,
    threads_override: Option<usize>,
    side_override: Option<u32>,
}
impl Backend for ReloadingBackend {
    fn recognize(&mut self, image: &Image) -> io::Result<String> {
        let mut settings = config::read(&self.config_path)?;
        if let Some(value) = self.threads_override {
            settings.threads = value;
        }
        if let Some(value) = self.side_override {
            settings.max_side = value;
        }
        if settings != self.applied {
            let updated = ocr::OcrBackend::new(ocr::Options {
                assets: self.assets.clone(),
                threads: settings.threads,
                max_side: settings.max_side,
            })?;
            self.backend = updated;
            self.applied = settings;
            eprintln!(
                "Applied OCR configuration: threads={}, max_side={}",
                settings.threads, settings.max_side
            );
        }
        self.backend.recognize(image)
    }
}

fn run() -> io::Result<()> {
    let mut args = env::args().skip(1);
    let mut socket = None;
    let mut image_path = None;
    let mut assets = PathBuf::from("assets");
    let mut threads = None;
    let mut max_side = None;
    let mut config_path = config::path();
    let mut repeat = 1;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--socket" => {
                socket = Some(PathBuf::from(
                    args.next().ok_or_else(|| invalid("missing socket path"))?,
                ))
            }
            "--config" => {
                config_path =
                    PathBuf::from(args.next().ok_or_else(|| invalid("missing config path"))?)
            }
            "--assets" => {
                assets = PathBuf::from(args.next().ok_or_else(|| invalid("missing assets path"))?)
            }
            "--image" => {
                image_path = Some(PathBuf::from(
                    args.next().ok_or_else(|| invalid("missing image path"))?,
                ))
            }
            "--threads" => {
                threads = Some(
                    args.next()
                        .ok_or_else(|| invalid("missing threads"))?
                        .parse()
                        .map_err(|_| invalid("invalid threads"))?,
                )
            }
            "--det-max-side" => {
                max_side = Some(
                    args.next()
                        .ok_or_else(|| invalid("missing det-max-side"))?
                        .parse()
                        .map_err(|_| invalid("invalid det-max-side"))?,
                )
            }
            "--repeat" => {
                repeat = args
                    .next()
                    .ok_or_else(|| invalid("missing repeat"))?
                    .parse()
                    .map_err(|_| invalid("invalid repeat"))?
            }
            "--help" | "-h" => {
                println!(
                    "spectacle-ocr-service (--socket PATH | --image FILE) [OPTIONS]\n\nPP-OCRv6 small CPU backend.\n  --assets DIR        Downloaded assets (default: assets)\n  --config FILE       Settings file (default: XDG config/spectacle-ocrrc)\n  --threads N         Override saved CPU threads (default: 4, range: 1..32)\n  --det-max-side N    Override saved detection resolution (default: 1536, range: 320..2048)\n  --repeat N          Repeat image inference with resident models (1..20)"
                );
                return Ok(());
            }
            _ => return Err(invalid("unknown argument; use --help")),
        }
    }
    if socket.is_some() == image_path.is_some() {
        return Err(invalid("provide exactly one of --socket or --image"));
    }
    if !(1..=20).contains(&repeat) || (socket.is_some() && repeat != 1) {
        return Err(invalid("--repeat requires image mode and must be 1..20"));
    }
    let mut settings = config::read(&config_path)?;
    if let Some(value) = threads {
        settings.threads = value;
    }
    if let Some(value) = max_side {
        settings.max_side = value;
    }
    let backend = ocr::OcrBackend::new(ocr::Options {
        assets: assets.clone(),
        threads: settings.threads,
        max_side: settings.max_side,
    })?;
    let mut backend = ReloadingBackend {
        backend,
        assets,
        config_path,
        applied: settings,
        threads_override: threads,
        side_override: max_side,
    };
    if let Some(path) = image_path {
        let mut reader = image::ImageReader::open(path)?.with_guessed_format()?;
        let mut limits = image::Limits::default();
        limits.max_alloc = Some(128 * 1024 * 1024);
        reader.limits(limits);
        let rgb = reader.decode().map_err(io::Error::other)?.to_rgb8();
        if rgb.as_raw().len() > MAX_IMAGE {
            return Err(invalid("image exceeds 64 MiB RGB limit"));
        }
        let image = Image {
            width: rgb.width(),
            height: rgb.height(),
            rgb: rgb.into_raw(),
        };
        let mut result = String::new();
        for _ in 0..repeat {
            result = backend.recognize(&image)?;
        }
        println!("{result}");
        return Ok(());
    }
    let socket = socket.unwrap();
    // Never unlink an existing endpoint: it may belong to a live service.
    let listener = UnixListener::bind(&socket)?;
    fs::set_permissions(&socket, fs::Permissions::from_mode(0o600))?;
    eprintln!("PP-OCRv6 CPU backend listening on {}", socket.display());
    for stream in listener.incoming() {
        match stream.and_then(|stream| handle(stream, &mut backend)) {
            Ok(()) => (),
            Err(error) => eprintln!("request failed: {error}"),
        }
    }
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("spectacle-ocr-service: {error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn request(w: u32, h: u32, len: u32, data: &[u8]) -> Vec<u8> {
        let mut bytes = MAGIC.to_vec();
        for value in [w, h, len] {
            bytes.extend(value.to_le_bytes());
        }
        bytes.extend(data);
        bytes
    }

    #[test]
    fn accepts_rgb_and_rejects_malformed_frames() {
        assert_eq!(
            read_image(&mut request(1, 1, 3, &[1, 2, 3]).as_slice())
                .unwrap()
                .rgb,
            [1, 2, 3]
        );
        for bytes in [
            request(0, 1, 0, &[]),
            request(1, 1, 4, &[0; 4]),
            request(u32::MAX, u32::MAX, 0, &[]),
            request(1, 1, 3, &[0]),
        ] {
            assert!(read_image(&mut bytes.as_slice()).is_err());
        }
    }

    #[test]
    fn response_preserves_unicode_and_empty_success() {
        for text in ["中文\nEnglish", ""] {
            let mut frame = Vec::new();
            respond(&mut frame, 0, text).unwrap();
            assert_eq!(&frame[16..], text.as_bytes());
            assert_eq!(
                u32::from_le_bytes(frame[12..16].try_into().unwrap()) as usize,
                text.len()
            );
        }
        assert!(respond(&mut Vec::new(), 0, "a\0b").is_err());
    }
}
