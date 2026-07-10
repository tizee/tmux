//! UTF-8 character codec: converting an expanded `Utf8Data` to/from the
//! compact `utf8_char` (u32) stored in extended cells.
//!
//! Mirrors tmux `utf8_from_data`/`utf8_to_data`: characters of up to 3 bytes
//! are packed directly into the u32; longer sequences (emoji, combined
//! characters) are interned in a table and the index stored.

use crate::cell::{Utf8Data, UTF8_SIZE};
use std::collections::HashMap;

/// Compact encoded character (tmux `utf8_char`).
pub type Utf8Char = u32;

const fn set_size(size: u32) -> u32 {
    size << 24
}

const fn set_width(width: u32) -> u32 {
    (width + 1) << 29
}

fn get_size(uc: Utf8Char) -> u32 {
    (uc >> 24) & 0x1f
}

fn get_width(uc: Utf8Char) -> u32 {
    ((uc >> 29) & 0x3).wrapping_sub(1)
}

/// Encode/decode between expanded and compact character forms.
pub trait Utf8Codec {
    /// Encode; on invalid input returns the same fallback encodings as the C
    /// implementation (space / double space).
    fn encode(&mut self, ud: &Utf8Data) -> Utf8Char;
    /// Decode a compact character back to expanded form.
    fn decode(&self, uc: Utf8Char) -> Utf8Data;
}

/// Pure-Rust codec replicating tmux's packing: <=3 bytes inline, longer
/// sequences interned in an owned table.
#[derive(Default)]
pub struct PackedCodec {
    items: Vec<Vec<u8>>,
    index: HashMap<Vec<u8>, u32>,
}

impl PackedCodec {
    pub fn new() -> Self {
        Self::default()
    }
}

impl Utf8Codec for PackedCodec {
    fn encode(&mut self, ud: &Utf8Data) -> Utf8Char {
        let size = ud.size as usize;
        if ud.width > 2 || size > UTF8_SIZE {
            // C: fatalx on width; fallback encodings on size. Widths above 2
            // never reach storage in practice; map both to the fallback.
            return match ud.width {
                0 => set_size(0) | set_width(0),
                1 => set_size(1) | set_width(1) | 0x20,
                _ => set_size(1) | set_width(1) | 0x2020,
            };
        }
        let index: u32 = if size <= 3 {
            let b = &ud.data;
            ((b[2] as u32) << 16) | ((b[1] as u32) << 8) | (b[0] as u32)
        } else {
            let key = ud.bytes().to_vec();
            match self.index.get(&key) {
                Some(&i) => i,
                None => {
                    let i = self.items.len() as u32;
                    self.items.push(key.clone());
                    self.index.insert(key, i);
                    i
                }
            }
        };
        set_size(size as u32) | set_width(ud.width as u32) | index
    }

    fn decode(&self, uc: Utf8Char) -> Utf8Data {
        let size = get_size(uc) as usize;
        let width = get_width(uc) as u8;
        let mut data = [0u8; UTF8_SIZE];
        if size <= 3 {
            let index = uc & 0x00ff_ffff;
            data[0] = (index & 0xff) as u8;
            if size > 1 {
                data[1] = ((index >> 8) & 0xff) as u8;
            }
            if size > 2 {
                data[2] = ((index >> 16) & 0xff) as u8;
            }
        } else {
            let index = (uc & 0x00ff_ffff) as usize;
            if let Some(bytes) = self.items.get(index) {
                data[..bytes.len()].copy_from_slice(bytes);
            }
        }
        Utf8Data {
            data,
            have: size as u8,
            size: size as u8,
            width,
        }
    }
}
