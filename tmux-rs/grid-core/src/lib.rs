//! Safe-Rust grid core for tmux.
//!
//! This crate reimplements tmux's `grid.c` — the cell/line/history memory
//! management that historically produced double-free and heap-corruption
//! crashes (upstream issues #4777/#4962/#5267). Ownership is expressed with
//! `Vec`, so line aliasing and double-frees are unrepresentable.
//!
//! Semantics mirror the C implementation observably: callers interact through
//! the same operations (`set_cell`, `scroll_history`, `collect_history`,
//! `duplicate_lines`, ...) and see the same cell values, history sizes and
//! scroll counters. Internal layout is free to differ.

#![forbid(unsafe_code)]

pub mod cell;
pub mod codec;
pub mod grid;

pub use cell::{GridCell, Utf8Data};
pub use codec::{PackedCodec, Utf8Codec};
pub use grid::Grid;

/// Cell flags (mirror tmux.h GRID_FLAG_*).
pub mod flags {
    pub const FG256: u8 = 0x1;
    pub const BG256: u8 = 0x2;
    pub const PADDING: u8 = 0x4;
    pub const EXTENDED: u8 = 0x8;
    pub const SELECTED: u8 = 0x10;
    pub const NOPALETTE: u8 = 0x20;
    pub const CLEARED: u8 = 0x40;
    pub const TAB: u8 = 0x80;
}

/// Line flags (mirror tmux.h GRID_LINE_*).
pub mod line_flags {
    pub const WRAPPED: i32 = 0x1;
    pub const EXTENDED: i32 = 0x2;
    pub const DEAD: i32 = 0x4;
    pub const START_PROMPT: i32 = 0x8;
    pub const START_OUTPUT: i32 = 0x10;
    pub const HYPERLINK: i32 = 0x20;
}

/// Colour encoding (mirror tmux.h COLOUR_FLAG_*).
pub mod colour {
    pub const FLAG_256: i32 = 0x0100_0000;
    pub const FLAG_RGB: i32 = 0x0200_0000;
    pub const FLAG_THEME: i32 = 0x0400_0000;

    /// True for the default fg/bg palette entries (8 and 9).
    pub fn is_default(c: i32) -> bool {
        c == 8 || c == 9
    }
}
