//! Behavior tests: text extraction and reflow (resize to a new width).
//!
//! Contracts transcribed from grid.c `grid_string_cells`, `grid_reflow`,
//! `grid_wrap_position`/`grid_unwrap_position` and `grid_line_length`.
//! Reflow is the other historical crash zone (line splitting/joining moves
//! cell storage between lines); the roundtrip tests pin the visible text,
//! and the soak pins consistency under repeated resize.

use grid_core::grid::StringOpts;
use grid_core::{line_flags, Grid, GridCell, Utf8Data};

fn cell(ch: &str, width: u8) -> GridCell {
    let mut gc = GridCell::default_cell();
    gc.data = Utf8Data::from_str(ch, width);
    gc
}

/// Write a string one character per column (ASCII only).
fn write_str(g: &mut Grid, px: u32, py: u32, s: &str) {
    for (i, ch) in s.chars().enumerate() {
        g.set_cell(px + i as u32, py, &cell(&ch.to_string(), 1));
    }
}

/// Write a wide (2-column) character followed by its padding cell.
fn write_wide(g: &mut Grid, px: u32, py: u32, ch: &str) {
    g.set_cell(px, py, &cell(ch, 2));
    g.set_padding(px + 1, py);
}

/// Read a whole line as plain text with trailing spaces trimmed.
fn line_text(g: &Grid, py: u32) -> String {
    g.string_cells(
        0,
        py,
        g.sx(),
        StringOpts {
            trim_spaces: true,
            ..Default::default()
        },
    )
}

// --- string_cells ----------------------------------------------------------

#[test]
fn string_cells_returns_written_text() {
    let mut g = Grid::new(80, 24, 0);
    write_str(&mut g, 0, 0, "hello world");
    assert_eq!(line_text(&g, 0), "hello world");
}

#[test]
fn string_cells_skips_padding_after_wide_chars() {
    let mut g = Grid::new(80, 24, 0);
    write_wide(&mut g, 0, 0, "中");
    write_wide(&mut g, 2, 0, "文");
    write_str(&mut g, 4, 0, "ok");
    assert_eq!(line_text(&g, 0), "中文ok");
}

#[test]
fn string_cells_reads_tab_as_tab_character() {
    let mut g = Grid::new(80, 24, 0);
    let mut tab = GridCell::default_cell();
    tab.set_tab(8);
    g.set_cell(0, 0, &tab);
    write_str(&mut g, 8, 0, "x");
    let s = g.string_cells(0, 0, 80, StringOpts::default());
    assert!(
        s.starts_with('\t'),
        "tab cell must extract as a single \\t, got {:?}",
        s
    );
}

#[test]
fn string_cells_respects_range_and_unwritten_tail() {
    let mut g = Grid::new(80, 24, 0);
    write_str(&mut g, 0, 0, "abcdef");
    // Range shorter than the text.
    assert_eq!(g.string_cells(1, 0, 3, StringOpts::default()), "bcd");
    // Default (used-cells) mode stops at the last written cell.
    assert_eq!(g.string_cells(0, 0, 80, StringOpts::default()), "abcdef");
}

#[test]
fn string_cells_out_of_range_line_is_empty() {
    let g = Grid::new(80, 24, 0);
    assert_eq!(g.string_cells(0, 999, 80, StringOpts::default()), "");
}

#[test]
fn line_length_ignores_trailing_spaces() {
    let mut g = Grid::new(80, 24, 0);
    write_str(&mut g, 0, 0, "abc   ");
    assert_eq!(g.line_length(0), 3);
    assert_eq!(g.line_length(1), 0, "untouched line has length 0");
}

// --- reflow: shrink (split) -------------------------------------------------

#[test]
fn reflow_splits_long_line_and_marks_wrapped() {
    let mut g = Grid::new(10, 4, 100);
    write_str(&mut g, 0, 0, "abcdefghij"); // exactly full width
    g.reflow(5);
    // 10 columns -> two rows of 5; the first carries WRAPPED.
    // Extra rows shift into history (sy stays 4).
    assert_eq!(g.sy(), 4);
    let total = g.hsize() + g.sy();
    let mut text = String::new();
    let mut wrapped_seen = false;
    for py in 0..total {
        let t = line_text(&g, py);
        if g.line_flags(py) & line_flags::WRAPPED != 0 {
            wrapped_seen = true;
        }
        text.push_str(&t);
    }
    assert!(wrapped_seen, "split line must be flagged WRAPPED");
    assert!(
        text.contains("abcdefghij"),
        "all text survives the split, got {:?}",
        text
    );
}

#[test]
fn reflow_split_respects_wide_char_boundaries() {
    let mut g = Grid::new(10, 4, 100);
    // "ab中cd" = columns: a b 中 中 c d
    write_str(&mut g, 0, 0, "ab");
    write_wide(&mut g, 2, 0, "中");
    write_str(&mut g, 4, 0, "cd");
    g.reflow(3); // 中 cannot straddle the boundary at column 3
    let total = g.hsize() + g.sy();
    let all: String = (0..total).map(|py| line_text(&g, py)).collect();
    assert!(
        all.contains("ab") && all.contains("中") && all.contains("cd"),
        "wide char must stay intact after split, got {:?}",
        all
    );
    // No line may exceed the new width in columns.
    for py in 0..total {
        let mut width = 0u32;
        for px in 0..g.sx() {
            let c = g.get_cell(px, py);
            if c.flags & grid_core::flags::PADDING == 0 {
                width += c.data.width as u32;
            }
        }
        assert!(width <= 3 + 1, "line {} too wide after reflow", py);
    }
}

// --- reflow: grow (join) -----------------------------------------------------

#[test]
fn reflow_joins_wrapped_lines_when_width_grows() {
    let mut g = Grid::new(10, 4, 100);
    write_str(&mut g, 0, 0, "abcdefghij");
    g.reflow(5); // split into two wrapped rows
    g.reflow(10); // grow back: join into one line again
    let total = g.hsize() + g.sy();
    let joined: Vec<String> = (0..total)
        .map(|py| line_text(&g, py))
        .filter(|t| !t.is_empty())
        .collect();
    assert_eq!(
        joined,
        vec!["abcdefghij".to_string()],
        "wrapped halves must rejoin into the original line"
    );
}

#[test]
fn reflow_does_not_join_unwrapped_lines() {
    let mut g = Grid::new(10, 4, 100);
    write_str(&mut g, 0, 0, "one");
    write_str(&mut g, 0, 1, "two");
    g.reflow(20);
    assert_eq!(line_text(&g, 0), "one");
    assert_eq!(line_text(&g, 1), "two", "distinct lines must not merge");
}

#[test]
fn reflow_roundtrip_preserves_text_and_styles() {
    let mut g = Grid::new(20, 6, 100);
    let mut styled = cell("S", 1);
    styled.fg = 2 | grid_core::colour::FLAG_256;
    styled.attr = 0x1ff; // extended-cell path
    write_str(&mut g, 0, 0, "hello wide ");
    g.set_cell(11, 0, &styled);
    write_wide(&mut g, 12, 0, "界");
    g.set_line_flag(0, line_flags::WRAPPED, false);

    g.reflow(7);
    g.reflow(20);

    let total = g.hsize() + g.sy();
    let all: String = (0..total).map(|py| line_text(&g, py)).collect();
    assert!(all.contains("hello wide S界"), "got {:?}", all);
    // The styled cell keeps its style through split+join.
    let mut found = false;
    for py in 0..total {
        for px in 0..g.sx() {
            let c = g.get_cell(px, py);
            if c.data.bytes() == b"S" {
                assert_eq!(c.fg, 2 | grid_core::colour::FLAG_256);
                assert_eq!(c.attr, 0x1ff);
                found = true;
            }
        }
    }
    assert!(found, "styled cell lost in reflow");
}

#[test]
fn reflow_bumps_scroll_generation() {
    let mut g = Grid::new(10, 4, 100);
    let before = g.scroll_generation();
    g.reflow(8);
    assert!(g.scroll_generation() > before);
}

// --- wrap/unwrap position -----------------------------------------------------

#[test]
fn wrap_position_roundtrips_through_reflow() {
    // The cursor-tracking contract: a position converted to wrapped
    // coordinates before reflow maps back to the same character after.
    let mut g = Grid::new(10, 4, 100);
    write_str(&mut g, 0, 0, "abcdefghij");
    let target = g.get_cell(7, 0); // 'h'
    let (wx, wy) = g.wrap_position(7, 0);
    g.reflow(5);
    let (px, py) = g.unwrap_position(wx, wy);
    let after = g.get_cell(px, py);
    assert!(
        after.cells_equal(&target),
        "position must survive reflow: got {:?}, want {:?}",
        after,
        target
    );
}

#[test]
fn wrap_position_past_end_of_line_is_sticky() {
    let mut g = Grid::new(10, 4, 100);
    write_str(&mut g, 0, 0, "abc");
    let (wx, _) = g.wrap_position(9, 0); // beyond cellused
    assert_eq!(wx, u32::MAX, "past-end maps to UINT_MAX (end sticky)");
    let (px, py) = g.unwrap_position(wx, 0);
    assert_eq!((px, py), (3, 0), "unwrap lands just past the text");
}

// --- soak: repeated resize (the reflow crash profile) -------------------------

#[test]
fn soak_repeated_reflow_keeps_text_consistent() {
    let mut g = Grid::new(40, 10, 200);
    // Several logical lines of mixed content, some wrapped.
    for row in 0..8u32 {
        let s = format!("row{:02}-abcdefghijklmnopqrstuvwxy", row);
        write_str(&mut g, 0, row, &s);
    }
    write_wide(&mut g, 30, 2, "宽");

    let snapshot: Vec<String> = {
        let total = g.hsize() + g.sy();
        (0..total).map(|py| line_text(&g, py)).collect()
    };
    let full: String = snapshot.concat();

    // Zig-zag resize like an interactively dragged pane.
    for sx in [23, 17, 61, 9, 33, 40] {
        g.reflow(sx);
        let total = g.hsize() + g.sy();
        let now: String = (0..total).map(|py| line_text(&g, py)).collect();
        assert_eq!(
            now.replace(char::is_whitespace, ""),
            full.replace(char::is_whitespace, ""),
            "text lost or duplicated at width {}",
            sx
        );
    }
}
