//! Independent PP-OCRv6 CPU pipeline for upright desktop text.
use crate::{Backend, Image, invalid};
use image::{RgbImage, imageops};
use ort::{
    session::{Session, builder::GraphOptimizationLevel},
    value::Tensor,
};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::{
    fs, io,
    path::{Path, PathBuf},
    time::Instant,
};

fn error(e: impl std::fmt::Display) -> io::Error {
    io::Error::other(e.to_string())
}

pub struct Options {
    pub assets: PathBuf,
    pub threads: usize,
    pub max_side: u32,
}

pub struct OcrBackend {
    detector: Session,
    recognizer: Session,
    dictionary: Vec<String>,
    max_side: u32,
}

#[derive(Deserialize)]
struct Manifest {
    files: Vec<Asset>,
}
#[derive(Deserialize)]
struct Asset {
    path: String,
    sha256: String,
}
#[derive(Deserialize)]
struct RecognitionConfig {
    #[serde(rename = "PostProcess")]
    post_process: Dictionary,
}
#[derive(Deserialize)]
struct Dictionary {
    name: String,
    character_dict: Vec<String>,
}

fn verified_models(assets: &Path) -> io::Result<()> {
    let manifest: Manifest =
        serde_json::from_str(include_str!("../models.lock.json")).map_err(error)?;
    for asset in manifest
        .files
        .iter()
        .filter(|asset| asset.path.starts_with("models/"))
    {
        let path = assets.join(&asset.path);
        let bytes = fs::read(&path).map_err(|e| {
            error(format!(
                "{}: {e}; run python3 scripts/setup_assets.py",
                path.display()
            ))
        })?;
        if format!("{:x}", Sha256::digest(&bytes)) != asset.sha256 {
            return Err(invalid(&format!("SHA256 mismatch: {}", path.display())));
        }
    }
    Ok(())
}

fn session(path: &Path, threads: usize) -> io::Result<Session> {
    let session = Session::builder()
        .map_err(error)?
        .with_optimization_level(GraphOptimizationLevel::Level3)
        .map_err(error)?
        .with_intra_threads(threads)
        .map_err(error)?
        .with_inter_threads(1)
        .map_err(error)?
        .with_intra_op_spinning(false)
        .map_err(error)?
        .with_inter_op_spinning(false)
        .map_err(error)?
        .commit_from_file(path)
        .map_err(error)?;
    if session.inputs().len() != 1 || session.outputs().len() != 1 {
        return Err(invalid("expected one input and one output per OCR model"));
    }
    Ok(session)
}

impl OcrBackend {
    pub fn new(options: Options) -> io::Result<Self> {
        if !(1..=32).contains(&options.threads) || !(320..=2048).contains(&options.max_side) {
            return Err(invalid(
                "threads must be 1..32; det-max-side must be 320..2048",
            ));
        }
        let start = Instant::now();
        verified_models(&options.assets)?;
        let runtime = options
            .assets
            .join("onnxruntime-linux-x64-1.30.0/lib/libonnxruntime.so");
        ort::init_from(&runtime)
            .map_err(error)?
            .with_name("SpectacleOCR")
            .commit();
        let config: RecognitionConfig =
            serde_yaml::from_slice(&fs::read(options.assets.join("models/rec.yml"))?)
                .map_err(error)?;
        if config.post_process.name != "CTCLabelDecode"
            || config.post_process.character_dict.is_empty()
        {
            return Err(invalid("unsupported recognition dictionary"));
        }
        let mut dictionary = vec![String::new()]; // CTC blank
        dictionary.extend(config.post_process.character_dict);
        dictionary.push(" ".into());
        let mut backend = Self {
            detector: session(&options.assets.join("models/det.onnx"), options.threads)?,
            recognizer: session(&options.assets.join("models/rec.onnx"), options.threads)?,
            dictionary,
            max_side: options.max_side,
        };
        // Warm both sessions and validate output contracts before advertising readiness.
        backend.detect(&RgbImage::from_pixel(64, 64, image::Rgb([255; 3])))?;
        backend.read_line(&RgbImage::from_pixel(320, 48, image::Rgb([255; 3])))?;
        eprintln!(
            "PP-OCRv6 small ready: CPU, {} threads, {} classes, loaded/warmed in {:.2}s",
            options.threads,
            backend.dictionary.len(),
            start.elapsed().as_secs_f32()
        );
        Ok(backend)
    }

    fn detect(&mut self, image: &RgbImage) -> io::Result<Vec<Rect>> {
        let (w, h) = detection_size(image.width(), image.height(), self.max_side);
        let resized = imageops::resize(image, w, h, imageops::FilterType::Triangle);
        let data = normalize(&resized, true, w);
        let input =
            Tensor::from_array(([1usize, 3, h as usize, w as usize], data)).map_err(error)?;
        let outputs = self.detector.run(ort::inputs![input]).map_err(error)?;
        let (shape, probabilities) = outputs[0].try_extract_tensor::<f32>().map_err(error)?;
        if shape.len() != 4
            || shape[0] != 1
            || shape[1] != 1
            || shape[2] != h as i64
            || shape[3] != w as i64
        {
            return Err(invalid("unexpected detector output shape"));
        }
        let mut boxes = components(probabilities, w as usize, h as usize)?;
        for rect in &mut boxes {
            rect.x0 *= image.width() as f32 / w as f32;
            rect.x1 *= image.width() as f32 / w as f32;
            rect.y0 *= image.height() as f32 / h as f32;
            rect.y1 *= image.height() as f32 / h as f32;
        }
        Ok(reading_order(boxes))
    }

    fn read_line(&mut self, crop: &RgbImage) -> io::Result<(String, f32)> {
        let content_width = (48.0 * crop.width() as f32 / crop.height() as f32).ceil() as u32;
        if content_width > 3200 {
            return Err(invalid(
                "text line exceeds recognition width limit (3200); select a smaller region",
            ));
        }
        let content_width = content_width.max(1);
        let width = content_width.max(320).div_ceil(32) * 32;
        let resized = imageops::resize(crop, content_width, 48, imageops::FilterType::Triangle);
        let input = Tensor::from_array((
            [1usize, 3, 48, width as usize],
            normalize(&resized, false, width),
        ))
        .map_err(error)?;
        let outputs = self.recognizer.run(ort::inputs![input]).map_err(error)?;
        let (shape, probabilities) = outputs[0].try_extract_tensor::<f32>().map_err(error)?;
        if shape.len() != 3
            || shape[0] != 1
            || shape[1] <= 0
            || shape[2] != self.dictionary.len() as i64
        {
            return Err(invalid("recognizer output shape does not match dictionary"));
        }
        decode_ctc(probabilities, &self.dictionary)
    }
}

impl Backend for OcrBackend {
    fn recognize(&mut self, image: &Image) -> io::Result<String> {
        let start = Instant::now();
        let rgb = RgbImage::from_raw(image.width, image.height, image.rgb.clone())
            .ok_or_else(|| invalid("invalid image"))?;
        let boxes = self.detect(&rgb)?;
        if boxes.len() > 256 {
            return Err(invalid(
                "more than 256 text regions; select a smaller screenshot",
            ));
        }
        let detected = start.elapsed();
        let mut lines = Vec::new();
        for rect in &boxes {
            if start.elapsed().as_secs() >= 15 {
                return Err(io::Error::new(
                    io::ErrorKind::TimedOut,
                    "OCR exceeded 15 second processing budget",
                ));
            }
            let x0 = rect.x0.floor().max(0.0) as u32;
            let y0 = rect.y0.floor().max(0.0) as u32;
            let x1 = (rect.x1.ceil() as u32).min(image.width);
            let y1 = (rect.y1.ceil() as u32).min(image.height);
            if x1 <= x0 || y1 <= y0 {
                continue;
            }
            let crop = imageops::crop_imm(&rgb, x0, y0, x1 - x0, y1 - y0).to_image();
            let (text, confidence) = self.read_line(&crop)?;
            if confidence >= 0.5 && !text.trim().is_empty() {
                lines.push(text.trim().to_owned());
            }
        }
        eprintln!(
            "OCR {}x{}: {} regions, {} lines, det={}ms total={}ms",
            image.width,
            image.height,
            boxes.len(),
            lines.len(),
            detected.as_millis(),
            start.elapsed().as_millis()
        );
        Ok(lines.join("\n"))
    }
}

fn detection_size(w: u32, h: u32, limit: u32) -> (u32, u32) {
    let scale = (limit as f32 / w.max(h) as f32).min(1.0);
    let side = |v: u32| ((v as f32 * scale / 32.0).round() as u32 * 32).max(32);
    (side(w), side(h))
}

// Official model preprocessing uses BGR channels. Rec padding is zero after
// normalization; detector uses ImageNet channel statistics.
fn normalize(image: &RgbImage, detection: bool, canvas_width: u32) -> Vec<f32> {
    let plane = (canvas_width * image.height()) as usize;
    let mut data = vec![0.0; 3 * plane];
    for (x, y, pixel) in image.enumerate_pixels() {
        let offset = (y * canvas_width + x) as usize;
        for channel in 0..3 {
            let value = pixel[2 - channel] as f32 / 255.0;
            data[channel * plane + offset] = if detection {
                (value - [0.485, 0.456, 0.406][channel]) / [0.229, 0.224, 0.225][channel]
            } else {
                (value - 0.5) * 2.0
            };
        }
    }
    data
}

fn decode_ctc(probabilities: &[f32], dictionary: &[String]) -> io::Result<(String, f32)> {
    if dictionary.len() < 2
        || !probabilities.len().is_multiple_of(dictionary.len())
        || probabilities.iter().any(|p| !p.is_finite())
    {
        return Err(invalid("invalid CTC probabilities"));
    }
    let mut text = String::new();
    let mut previous = 0;
    let mut score = 0.0;
    let mut count = 0;
    for step in probabilities.chunks_exact(dictionary.len()) {
        let (index, value) = step
            .iter()
            .enumerate()
            .max_by(|a, b| a.1.total_cmp(b.1))
            .unwrap();
        if index != 0 && index != previous {
            text.push_str(&dictionary[index]);
            score += value;
            count += 1;
        }
        previous = index;
    }
    Ok((
        text,
        if count == 0 {
            0.0
        } else {
            score / count as f32
        },
    ))
}

#[derive(Clone, Debug)]
struct Rect {
    x0: f32,
    y0: f32,
    x1: f32,
    y1: f32,
}
impl Rect {
    fn width(&self) -> f32 {
        self.x1 - self.x0
    }
    fn height(&self) -> f32 {
        self.y1 - self.y0
    }
    fn center_y(&self) -> f32 {
        (self.y0 + self.y1) * 0.5
    }
}

// DB probability regions → axis-aligned crops. This intentionally targets
// upright screenshots; perspective/rotated document rectification is deferred.
fn components(prob: &[f32], width: usize, height: usize) -> io::Result<Vec<Rect>> {
    if prob.len() != width * height || prob.iter().any(|p| !p.is_finite()) {
        return Err(invalid("invalid detector probability map"));
    }
    let mut seen = vec![false; prob.len()];
    let mut pending = Vec::new();
    let mut boxes = Vec::new();
    for origin in 0..prob.len() {
        if seen[origin] || prob[origin] <= 0.2 {
            continue;
        }
        seen[origin] = true;
        pending.push(origin);
        let (mut x0, mut y0, mut x1, mut y1) = (width, height, 0, 0);
        let mut count = 0;
        let mut score = 0.0;
        while let Some(index) = pending.pop() {
            let (x, y) = (index % width, index / width);
            x0 = x0.min(x);
            y0 = y0.min(y);
            x1 = x1.max(x + 1);
            y1 = y1.max(y + 1);
            count += 1;
            score += prob[index];
            for ny in y.saturating_sub(1)..=(y + 1).min(height - 1) {
                for nx in x.saturating_sub(1)..=(x + 1).min(width - 1) {
                    let next = ny * width + nx;
                    if !seen[next] && prob[next] > 0.2 {
                        seen[next] = true;
                        pending.push(next);
                    }
                }
            }
        }
        if count < 8 || score / (count as f32) < 0.45 || y1 - y0 < 2 || x1 - x0 < 2 {
            continue;
        }
        if boxes.len() >= 3000 {
            return Err(invalid("too many detector components"));
        }
        let (w, h) = ((x1 - x0) as f32, (y1 - y0) as f32);
        let margin = (w * h * 1.4 / (2.0 * (w + h))).max(1.0);
        boxes.push(Rect {
            x0: (x0 as f32 - margin).max(0.0),
            y0: (y0 as f32 - margin).max(0.0),
            x1: (x1 as f32 + margin).min(width as f32),
            y1: (y1 as f32 + margin).min(height as f32),
        });
    }
    // Remove nested duplicate regions before ordering.
    boxes.sort_by(|a, b| (b.width() * b.height()).total_cmp(&(a.width() * a.height())));
    let mut kept: Vec<Rect> = Vec::new();
    for rect in boxes {
        if kept.iter().any(|other| {
            let overlap = (rect.x1.min(other.x1) - rect.x0.max(other.x0)).max(0.0)
                * (rect.y1.min(other.y1) - rect.y0.max(other.y0)).max(0.0);
            overlap / (rect.width() * rect.height()) > 0.7
        }) {
            continue;
        }
        kept.push(rect);
    }
    Ok(kept)
}

fn reading_order(mut boxes: Vec<Rect>) -> Vec<Rect> {
    boxes.sort_by(|a, b| a.center_y().total_cmp(&b.center_y()));
    let mut rows: Vec<Vec<Rect>> = Vec::new();
    for rect in boxes {
        if let Some(row) = rows.last_mut()
            && (row[0].center_y() - rect.center_y()).abs()
                < row[0].height().min(rect.height()) * 0.5
        {
            row.push(rect);
        } else {
            rows.push(vec![rect]);
        }
    }
    rows.into_iter()
        .flat_map(|mut row| {
            row.sort_by(|a, b| a.x0.total_cmp(&b.x0));
            row
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ctc_repeats_blank_unicode_space_and_invalid_data() {
        let dictionary = vec!["".into(), "中".into(), "A".into(), " ".into()];
        let indices = [1, 1, 0, 1, 2, 2, 3];
        let probabilities: Vec<_> = indices
            .iter()
            .flat_map(|&index| (0..4).map(move |i| if i == index { 1.0 } else { 0.0 }))
            .collect();
        assert_eq!(
            decode_ctc(&probabilities, &dictionary).unwrap(),
            ("中中A ".into(), 1.0)
        );
        assert!(decode_ctc(&[f32::NAN; 4], &dictionary).is_err());
        assert!(decode_ctc(&[0.0; 3], &dictionary).is_err());
    }
    #[test]
    fn preprocessing_uses_bgr_and_zero_rec_padding() {
        let image = RgbImage::from_pixel(1, 1, image::Rgb([255, 0, 0]));
        assert_eq!(
            normalize(&image, false, 2),
            [-1.0, 0.0, -1.0, 0.0, 1.0, 0.0]
        );
        assert_eq!(detection_size(3840, 2160, 1536), (1536, 864));
        assert_eq!(detection_size(1, 1, 1536), (32, 32));
    }
    #[test]
    fn detector_finds_separate_rows_and_ignores_noise() {
        let mut prob = vec![0.0; 64 * 64];
        for y in [8, 32] {
            for row in y..y + 4 {
                for x in 10..40 {
                    prob[row * 64 + x] = 0.9;
                }
            }
        }
        prob[0] = 0.9;
        let boxes = reading_order(components(&prob, 64, 64).unwrap());
        assert_eq!(boxes.len(), 2);
        assert!(boxes[0].y0 < 8.0 && boxes[0].y1 > 12.0);
        assert!(boxes[0].y1 < boxes[1].y0);
        assert!(components(&[f32::NAN], 1, 1).is_err());
    }
}
