//! CPU image-to-LaTeX inference using the pinned Pix2Text MFR 1.5 export.
use crate::{Image, invalid};
use image::{RgbImage, imageops};
use ort::{session::Session, value::Tensor};
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::{
    collections::HashMap,
    fs, io,
    path::Path,
    time::{Duration, Instant},
};

fn error(e: impl std::fmt::Display) -> io::Error {
    io::Error::other(e.to_string())
}

pub struct Formula {
    encoder: Session,
    decoder: Session,
    vocabulary: HashMap<i64, Vec<u8>>,
}

fn vocabulary(json: &str) -> io::Result<HashMap<i64, Vec<u8>>> {
    let document: Value = serde_json::from_str(json).map_err(error)?;
    let entries = document["model"]["vocab"]
        .as_object()
        .ok_or_else(|| invalid("missing formula vocabulary"))?;
    // Invert the standard GPT-2 byte alphabet used by this BPE tokenizer.
    let mut alphabet = HashMap::new();
    let mut extra = 256;
    for byte in 0u32..256 {
        let code = if (33..=126).contains(&byte) || (161..=172).contains(&byte) || byte >= 174 {
            byte
        } else {
            extra += 1;
            extra - 1
        };
        alphabet.insert(char::from_u32(code).unwrap(), byte as u8);
    }
    let mut result = HashMap::new();
    for (token, id) in entries {
        let id = id
            .as_i64()
            .ok_or_else(|| invalid("invalid formula token ID"))?;
        if id <= 2 {
            continue;
        }
        let bytes = token
            .chars()
            .map(|c| {
                alphabet
                    .get(&c)
                    .copied()
                    .ok_or_else(|| invalid("unsupported formula token alphabet"))
            })
            .collect::<io::Result<Vec<_>>>()?;
        result.insert(id, bytes);
    }
    Ok(result)
}

impl Formula {
    pub fn new(assets: &Path, threads: usize) -> io::Result<Self> {
        let manifest: Value =
            serde_json::from_str(include_str!("../formula.lock.json")).map_err(error)?;
        for item in manifest["files"].as_array().unwrap() {
            let path = assets.join(item["path"].as_str().unwrap());
            let data = fs::read(&path).map_err(|e| {
                error(format!(
                    "{}: {e}; download formula models first",
                    path.display()
                ))
            })?;
            if format!("{:x}", Sha256::digest(&data)) != item["sha256"].as_str().unwrap() {
                return Err(invalid(
                    "formula model SHA256 mismatch; reinstall formula models",
                ));
            }
        }
        let runtime = assets.join("onnxruntime-linux-x64-1.30.0/lib/libonnxruntime.so");
        ort::init_from(runtime)
            .map_err(error)?
            .with_name("SpectacleOCR")
            .commit();
        let session = |name: &str| -> io::Result<Session> {
            Session::builder()
                .map_err(error)?
                .with_intra_threads(threads)
                .map_err(error)?
                .with_inter_threads(1)
                .map_err(error)?
                .with_intra_op_spinning(false)
                .map_err(error)?
                .commit_from_file(assets.join("formula").join(name))
                .map_err(error)
        };
        Ok(Self {
            encoder: session("encoder_model.onnx")?,
            decoder: session("decoder_model.onnx")?,
            vocabulary: vocabulary(&fs::read_to_string(assets.join("formula/tokenizer.json"))?)?,
        })
    }

    pub fn recognize(&mut self, image: &Image, deadline: Instant) -> io::Result<String> {
        let rgb = RgbImage::from_raw(image.width, image.height, image.rgb.clone())
            .ok_or_else(|| invalid("invalid formula image"))?;
        // The model's processor specifies a square 384px canvas and bicubic resize.
        let resized = imageops::resize(&rgb, 384, 384, imageops::FilterType::CatmullRom);
        let mut pixels = vec![0f32; 3 * 384 * 384];
        for (i, pixel) in resized.pixels().enumerate() {
            for c in 0..3 {
                pixels[c * 384 * 384 + i] = pixel[c] as f32 / 127.5 - 1.0;
            }
        }
        let input = Tensor::from_array(([1usize, 3, 384, 384], pixels)).map_err(error)?;
        let encoded = self.encoder.run(ort::inputs![input]).map_err(error)?;
        let (shape, values) = encoded[0].try_extract_tensor::<f32>().map_err(error)?;
        if shape.as_ref() != [1, 578, 384] {
            return Err(invalid("unexpected formula encoder shape"));
        }
        let hidden = Tensor::from_array(([1usize, 578, 384], values.to_vec())).map_err(error)?;
        let mut ids = vec![1i64];
        let mut bytes = Vec::new();
        for _ in 0..1024 {
            if Instant::now() >= deadline {
                return Err(io::Error::new(
                    io::ErrorKind::TimedOut,
                    "formula recognition timed out; select a smaller formula",
                ));
            }
            let tokens = Tensor::from_array(([1usize, ids.len()], ids.clone())).map_err(error)?;
            let outputs = self
                .decoder
                .run(ort::inputs!["input_ids" => tokens, "encoder_hidden_states" => &hidden])
                .map_err(error)?;
            let (shape, logits) = outputs[0].try_extract_tensor::<f32>().map_err(error)?;
            if shape.len() != 3 || shape[0] != 1 || shape[1] != ids.len() as i64 || shape[2] <= 0 {
                return Err(invalid("unexpected formula decoder shape"));
            }
            let width = shape[2] as usize;
            let row = &logits[(ids.len() - 1) * width..ids.len() * width];
            if row.iter().any(|v| !v.is_finite()) {
                return Err(invalid("non-finite formula logits"));
            }
            let next = row
                .iter()
                .enumerate()
                .max_by(|a, b| a.1.total_cmp(b.1))
                .unwrap()
                .0 as i64;
            if next == 2 {
                let text = String::from_utf8(bytes).map_err(error)?;
                return Ok(text.trim().to_owned());
            }
            bytes.extend(
                self.vocabulary
                    .get(&next)
                    .ok_or_else(|| invalid("unknown formula token"))?,
            );
            ids.push(next);
        }
        Err(invalid(
            "formula exceeded 1024 tokens; select a smaller formula",
        ))
    }
}

pub fn deadline() -> Instant {
    Instant::now() + Duration::from_secs(23)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn byte_alphabet_preserves_spaces_commands_and_unicode() {
        let tokens =
            vocabulary(r#"{"model":{"vocab":{"<s>":1,"Ġ":3,"\\frac":4,"ä¸Ń":5}}}"#).unwrap();
        assert_eq!(tokens[&3], b" ");
        assert_eq!(tokens[&4], b"\\frac");
        assert_eq!(String::from_utf8(tokens[&5].clone()).unwrap(), "中");
    }
}
