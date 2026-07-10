//! Behavior tests for the grid core.
//!
//! Each test describes an observable contract of tmux's grid (what callers
//! like screen-write, window-copy and tty-draw rely on), not internal layout.
//! Contracts are transcribed from grid.c; the soak test at the end replays
//! the workload profile of the 2026-07 double-free crashes (heavy scroll +
//! collect + repeated clone) and must stay consistent by construction.

use grid_core::{colour, flags, line_flags, Grid, GridCell, Utf8Data};

fn cell(ch: &str, width: u8) -> GridCell {
    let mut gc = GridCell::default_cell();
    gc.data = Utf8Data::from_str(ch, width);
    gc
}

fn marker(py_tag: u8) -> GridCell {
    // A distinct simple cell per row so shifted rows are identifiable.
    let mut gc = cell("M", 1);
    gc.fg = py_tag as i32;
    gc
}

// --- Reading a fresh grid ------------------------------------------------

#[test]
fn fresh_grid_reads_default_cells_everywhere() {
    let g = Grid::new(80, 24, 100);
    let def = GridCell::default_cell();
    for (px, py) in [(0, 0), (79, 23), (40, 12)] {
        assert!(
            g.get_cell(px, py).cells_equal(&def),
            "unwritten cell at {},{} must read as default",
            px,
            py
        );
    }
}

#[test]
fn out_of_range_reads_default_not_panic() {
    let g = Grid::new(80, 24, 100);
    let def = GridCell::default_cell();
    // Past the right edge, past the bottom, and absurd values.
    assert!(g.get_cell(80, 0).cells_equal(&def));
    assert!(g.get_cell(0, 24).cells_equal(&def));
    assert!(g.get_cell(u32::MAX, u32::MAX).cells_equal(&def));
}

#[test]
fn zero_sized_grid_is_usable() {
    let g = Grid::new(0, 0, 0);
    let def = GridCell::default_cell();
    assert!(g.get_cell(0, 0).cells_equal(&def));
}

// --- Cell write/read roundtrips ------------------------------------------

#[test]
fn ascii_cell_roundtrip_preserves_value_and_style() {
    let mut g = Grid::new(80, 24, 100);
    let mut gc = cell("A", 1);
    gc.attr = 0x04; // e.g. GRID_ATTR_UNDERSCORE
    gc.fg = 3;
    gc.bg = 7;
    g.set_cell(5, 10, &gc);
    let got = g.get_cell(5, 10);
    assert!(got.cells_equal(&gc), "got {:?}, want {:?}", got, gc);
}

#[test]
fn colour_256_roundtrip() {
    let mut g = Grid::new(80, 24, 100);
    let mut gc = cell("x", 1);
    gc.fg = 200 | colour::FLAG_256;
    gc.bg = 100 | colour::FLAG_256;
    g.set_cell(0, 0, &gc);
    let got = g.get_cell(0, 0);
    assert_eq!(got.fg, 200 | colour::FLAG_256);
    assert_eq!(got.bg, 100 | colour::FLAG_256);
}

#[test]
fn rgb_colour_and_wide_attr_roundtrip() {
    // RGB colours and attr > 0xff cannot fit the compact form; the caller
    // must still read back exactly what was written.
    let mut g = Grid::new(80, 24, 100);
    let mut gc = cell("z", 1);
    gc.fg = 0x123456 | colour::FLAG_RGB;
    gc.bg = 0x654321 | colour::FLAG_RGB;
    gc.attr = 0x1ff;
    gc.us = 0x00ff00 | colour::FLAG_RGB;
    g.set_cell(2, 3, &gc);
    let got = g.get_cell(2, 3);
    assert!(got.cells_equal(&gc), "got {:?}, want {:?}", got, gc);
    assert_eq!(got.us, gc.us);
}

#[test]
fn cjk_wide_cell_roundtrip() {
    let mut g = Grid::new(80, 24, 100);
    let gc = cell("中", 2); // 3-byte UTF-8, width 2
    g.set_cell(10, 5, &gc);
    let got = g.get_cell(10, 5);
    assert_eq!(got.data.bytes(), "中".as_bytes());
    assert_eq!(got.data.width, 2);
}

#[test]
fn emoji_four_byte_cell_roundtrip() {
    // > 3 bytes exercises the interned-character path.
    let mut g = Grid::new(80, 24, 100);
    let gc = cell("🦀", 2);
    g.set_cell(0, 0, &gc);
    let got = g.get_cell(0, 0);
    assert_eq!(got.data.bytes(), "🦀".as_bytes());
    assert_eq!(got.data.width, 2);
}

#[test]
fn hyperlink_roundtrip_and_line_flag() {
    let mut g = Grid::new(80, 24, 100);
    let mut gc = cell("l", 1);
    gc.link = 42;
    g.set_cell(1, 1, &gc);
    assert_eq!(g.get_cell(1, 1).link, 42);
    assert_ne!(
        g.line_flags(1) & line_flags::HYPERLINK,
        0,
        "line containing a hyperlink must carry GRID_LINE_HYPERLINK"
    );
}

#[test]
fn tab_cell_reads_back_as_spaces_with_tab_flag() {
    let mut g = Grid::new(80, 24, 100);
    let mut gc = GridCell::default_cell();
    gc.set_tab(8);
    g.set_cell(0, 0, &gc);
    let got = g.get_cell(0, 0);
    assert_ne!(got.flags & flags::TAB, 0);
    assert_eq!(got.data.width, 8);
    assert_eq!(got.data.bytes(), b"        ");
}

#[test]
fn padding_cell_roundtrip() {
    // Callers (screen-write, window-copy) identify padding cells by the
    // PADDING flag alone. C stores them in compact form, so the character
    // reads back as '!' with width 1 — the flag is the contract.
    let mut g = Grid::new(80, 24, 100);
    let wide = cell("中", 2);
    g.set_cell(4, 0, &wide);
    g.set_padding(5, 0);
    let got = g.get_cell(5, 0);
    assert_ne!(got.flags & flags::PADDING, 0);
}

#[test]
fn set_cells_writes_a_styled_run() {
    let mut g = Grid::new(80, 24, 100);
    let mut style = GridCell::default_cell();
    style.fg = 2;
    g.set_cells(3, 7, &style, "hello");
    for (i, ch) in "hello".bytes().enumerate() {
        let got = g.get_cell(3 + i as u32, 7);
        assert_eq!(got.data.bytes(), &[ch]);
        assert_eq!(got.fg, 2);
    }
}

#[test]
fn overwrite_extended_with_simple_and_back() {
    // Regression shape: extended slots are reused/compacted; overwriting
    // must never leak stale extended data into reads.
    let mut g = Grid::new(80, 24, 100);
    let wide = cell("字", 2);
    g.set_cell(0, 0, &wide);
    let plain = cell("a", 1);
    g.set_cell(0, 0, &plain);
    assert!(g.get_cell(0, 0).cells_equal(&plain));
    g.set_cell(0, 0, &wide);
    assert!(g.get_cell(0, 0).cells_equal(&wide));
}

// --- Clearing -------------------------------------------------------------

#[test]
fn clear_area_with_coloured_bg_reads_cleared_cells() {
    let mut g = Grid::new(80, 24, 100);
    g.set_cell(5, 5, &cell("A", 1));
    g.clear(0, 5, 10, 1, 4);
    let got = g.get_cell(5, 5);
    assert_eq!(got.data.bytes(), b" ", "cleared cell shows a space");
    assert_eq!(got.bg, 4, "cleared cell carries the clear bg");
    assert_ne!(got.flags & flags::CLEARED, 0);
}

#[test]
fn clear_area_with_default_bg_beyond_written_area_is_noop() {
    // C grid_clear skips columns beyond cellsize for default bg.
    let mut g = Grid::new(80, 24, 100);
    g.clear(70, 0, 10, 1, 8);
    let def = GridCell::default_cell();
    assert!(g.get_cell(75, 0).cells_equal(&def));
}

#[test]
fn clear_lines_resets_whole_lines() {
    let mut g = Grid::new(80, 24, 100);
    g.set_cell(0, 3, &cell("X", 1));
    g.set_cell(79, 3, &cell("Y", 1));
    g.clear_lines(3, 1, 8);
    let def = GridCell::default_cell();
    assert!(g.get_cell(0, 3).cells_equal(&def));
    assert!(g.get_cell(79, 3).cells_equal(&def));
}

#[test]
fn clear_lines_unwraps_previous_line() {
    let mut g = Grid::new(80, 24, 100);
    g.set_line_flag(2, line_flags::WRAPPED, true);
    g.clear_lines(3, 1, 8);
    assert_eq!(
        g.line_flags(2) & line_flags::WRAPPED,
        0,
        "clearing a line clears WRAPPED on the line above"
    );
}

// --- Moving ---------------------------------------------------------------

#[test]
fn move_lines_transfers_content_and_empties_source() {
    let mut g = Grid::new(80, 24, 100);
    g.set_cell(0, 2, &marker(2));
    g.set_cell(0, 3, &marker(3));
    g.move_lines(10, 2, 2, 8);
    assert!(g.get_cell(0, 10).cells_equal(&marker(2)));
    assert!(g.get_cell(0, 11).cells_equal(&marker(3)));
    let def = GridCell::default_cell();
    assert!(
        g.get_cell(0, 2).cells_equal(&def),
        "moved-from line must read empty"
    );
    assert!(g.get_cell(0, 3).cells_equal(&def));
}

#[test]
fn move_lines_overlapping_ranges_stay_consistent() {
    // Overlapping move (scroll-like): the C bug class is stale aliasing
    // between source and destination slots.
    let mut g = Grid::new(80, 24, 100);
    for py in 0..6 {
        g.set_cell(0, py, &marker(py as u8));
    }
    g.move_lines(0, 1, 5, 8); // shift rows 1..6 up by one
    for py in 0..5 {
        assert!(
            g.get_cell(0, py).cells_equal(&marker(py as u8 + 1)),
            "row {} must now hold old row {}",
            py,
            py + 1
        );
    }
    let def = GridCell::default_cell();
    assert!(g.get_cell(0, 5).cells_equal(&def), "vacated row is empty");
    // Mutating a destination row must not affect any other row (no aliasing).
    g.set_cell(0, 0, &cell("Z", 1));
    assert!(g.get_cell(0, 1).cells_equal(&marker(2)));
}

#[test]
fn move_cells_shifts_within_line() {
    let mut g = Grid::new(80, 24, 100);
    let mut style = GridCell::default_cell();
    style.fg = 5;
    g.set_cells(0, 0, &style, "abcdef");
    g.move_cells(2, 0, 0, 4, 8); // move "abcd" right by two
    assert_eq!(g.get_cell(2, 0).data.bytes(), b"a");
    assert_eq!(g.get_cell(5, 0).data.bytes(), b"d");
}

// --- History --------------------------------------------------------------

#[test]
fn scroll_history_moves_viewport_top_into_history() {
    let mut g = Grid::new(80, 24, 100);
    g.set_cell(0, 0, &marker(7)); // viewport row 0 (hsize == 0)
    g.scroll_history(8);
    assert_eq!(g.hsize(), 1);
    assert_eq!(g.scroll_added(), 1);
    assert!(
        g.get_cell(0, 0).cells_equal(&marker(7)),
        "scrolled line is still readable at absolute row 0 (history)"
    );
    let def = GridCell::default_cell();
    assert!(
        g.get_cell(0, g.hsize() + g.sy() - 1).cells_equal(&def),
        "new bottom viewport line is empty"
    );
}

#[test]
fn collect_history_drops_oldest_lines_and_shifts() {
    let hlimit = 20;
    let mut g = Grid::new(80, 4, hlimit);
    // Scroll until history is exactly at the limit.
    for i in 0..hlimit {
        g.set_cell(0, g.hsize(), &marker(i as u8));
        g.scroll_history(8);
    }
    assert_eq!(g.hsize(), hlimit);
    g.collect_history(false);
    let dropped = hlimit / 10; // C frees hlimit/10 (min 1)
    assert_eq!(g.hsize(), hlimit - dropped);
    assert_eq!(g.scroll_collected(), dropped);
    // Oldest surviving line is the one written at index `dropped`.
    assert!(
        g.get_cell(0, 0).cells_equal(&marker(dropped as u8)),
        "history shifted: absolute row 0 now holds line {}",
        dropped
    );
}

#[test]
fn collect_history_below_limit_is_noop() {
    let mut g = Grid::new(80, 4, 100);
    g.scroll_history(8);
    g.scroll_history(8);
    let before = g.hsize();
    g.collect_history(false);
    assert_eq!(g.hsize(), before);
    assert_eq!(g.scroll_collected(), 0);
}

#[test]
fn remove_history_drops_newest_history_lines() {
    let mut g = Grid::new(80, 4, 100);
    for i in 0..5 {
        g.set_cell(0, g.hsize(), &marker(i));
        g.scroll_history(8);
    }
    assert_eq!(g.hsize(), 5);
    g.remove_history(2);
    assert_eq!(g.hsize(), 3);
    // Oldest lines survive.
    assert!(g.get_cell(0, 0).cells_equal(&marker(0)));
    assert!(g.get_cell(0, 2).cells_equal(&marker(2)));
}

#[test]
fn clear_history_keeps_viewport_content() {
    let mut g = Grid::new(80, 4, 100);
    for _ in 0..5 {
        g.scroll_history(8);
    }
    let gen_before = g.scroll_generation();
    g.set_cell(0, g.hsize() + 1, &marker(9)); // viewport row 1
    g.clear_history();
    assert_eq!(g.hsize(), 0);
    assert_eq!(g.hscrolled(), 0);
    assert!(g.scroll_generation() > gen_before, "generation must bump");
    assert!(
        g.get_cell(0, 1).cells_equal(&marker(9)),
        "viewport content survives clear_history"
    );
}

#[test]
fn scroll_history_region_moves_region_top_into_history() {
    let mut g = Grid::new(80, 6, 100);
    for py in 0..6 {
        g.set_cell(0, py, &marker(py as u8));
    }
    // Scroll viewport region rows 1..=3 (absolute, hsize==0).
    g.scroll_history_region(1, 3, 8);
    assert_eq!(g.hsize(), 1);
    // Old region top (marker 1) went into history at absolute row 0.
    assert!(g.get_cell(0, 0).cells_equal(&marker(1)));
    // Viewport: row above region kept (marker 0), region shifted up.
    assert!(g.get_cell(0, g.hsize()).cells_equal(&marker(0)));
    assert!(g.get_cell(0, g.hsize() + 1).cells_equal(&marker(2)));
    assert!(g.get_cell(0, g.hsize() + 2).cells_equal(&marker(3)));
    let def = GridCell::default_cell();
    assert!(
        g.get_cell(0, g.hsize() + 3).cells_equal(&def),
        "region bottom is emptied"
    );
    // Rows below the region untouched.
    assert!(g.get_cell(0, g.hsize() + 4).cells_equal(&marker(4)));
    assert!(g.get_cell(0, g.hsize() + 5).cells_equal(&marker(5)));
}

// --- Copying (the crash-class contracts) ----------------------------------

#[test]
fn duplicate_lines_is_a_deep_copy() {
    // THE contract whose violation caused the 2026 crashes: after
    // duplication, source and destination must not share cell storage.
    let mut src = Grid::new(80, 4, 100);
    let mut dst = Grid::new(80, 4, 100);
    src.set_cell(0, 0, &cell("中", 2)); // extended cell: heap-backed in C
    src.set_cell(1, 0, &marker(1));
    Grid::duplicate_lines(&mut dst, 0, &src, 0, 1);
    assert!(dst.get_cell(0, 0).cells_equal(&cell("中", 2)));

    // Mutate the source; the copy must not change.
    src.set_cell(0, 0, &cell("X", 1));
    src.clear_lines(0, 1, 8);
    assert!(
        dst.get_cell(0, 0).cells_equal(&cell("中", 2)),
        "dst must own its cells: mutating src leaked through"
    );

    // And freeing/overwriting the copy must not affect the source's later use.
    dst.clear_lines(0, 1, 8);
    let def = GridCell::default_cell();
    assert!(src.get_cell(0, 0).cells_equal(&def));
}

#[test]
fn duplicate_lines_clamps_counts_to_both_grids() {
    let src = Grid::new(80, 4, 0);
    let mut dst = Grid::new(80, 2, 0);
    // ny beyond both grids: must clamp, not panic.
    Grid::duplicate_lines(&mut dst, 0, &src, 0, 100);
    Grid::duplicate_lines(&mut dst, 1, &src, 3, 100);
}

#[test]
fn compare_reports_equal_and_different_grids() {
    let mut a = Grid::new(80, 4, 0);
    let mut b = Grid::new(80, 4, 0);
    assert!(a.compare(&b), "fresh grids compare equal");
    a.set_cell(0, 0, &cell("q", 1));
    assert!(!a.compare(&b));
    b.set_cell(0, 0, &cell("q", 1));
    assert!(a.compare(&b));
    let c = Grid::new(81, 4, 0);
    assert!(!a.compare(&c), "different geometry never compares equal");
}

// --- Soak: the crash workload profile --------------------------------------

#[test]
fn soak_scroll_collect_clone_cycle_stays_consistent() {
    // Profile of the 2026-07-10 crash: a busy pane scrolling thousands of
    // lines through a limited history while copy-mode repeatedly clones the
    // grid. In C this intermittently double-freed; here the same op sequence
    // must keep every read consistent.
    let hlimit = 50;
    let mut g = Grid::new(120, 24, hlimit);
    let mut written: u32 = 0;

    for round in 0..40 {
        // Burst of output: write a row signature, scroll, collect at limit.
        for _ in 0..37 {
            let tag = (written % 251) as u8;
            g.set_cell(0, g.hsize() + g.sy() - 1, &marker(tag));
            g.set_cell(1, g.hsize() + g.sy() - 1, &cell("界", 2));
            g.scroll_history(8);
            written += 1;
            g.collect_history(false);
        }

        // Copy-mode entry: clone the whole grid (clone_screen shape).
        let mut clone = Grid::new(g.sx(), g.sy(), 0);
        Grid::duplicate_lines(&mut clone, 0, &g, g.hsize(), g.sy());

        // Clone reads what the source viewport reads.
        for py in 0..g.sy() {
            let a = g.get_cell(0, g.hsize() + py);
            let b = clone.get_cell(0, py);
            assert!(
                a.cells_equal(&b),
                "round {}: clone viewport row {} diverged",
                round,
                py
            );
        }
        drop(clone); // C: grid_destroy — where the double-free fired

        // History invariants hold every round.
        assert!(g.hsize() <= hlimit, "history never exceeds limit + slack");
        assert_eq!(
            g.scroll_added() - g.scroll_collected(),
            g.hsize(),
            "round {}: counter arithmetic must balance (window-copy relies on it)",
            round
        );
    }

    // Everything in history + viewport still reads without inconsistency.
    for py in 0..(g.hsize() + g.sy()) {
        let _ = g.get_cell(0, py);
        let _ = g.get_cell(1, py);
    }
}

#[test]
fn cleared_extended_cell_still_reads_cleared_flag() {
    // Deliberate fix over C: grid.c loses CLEARED when the cell was
    // previously extended. Here the flag is the contract of "cleared",
    // independent of storage history.
    let mut g = Grid::new(80, 24, 0);
    g.set_cell(5, 0, &cell("中", 2)); // extended cell
    g.clear(5, 0, 1, 1, 4);
    let got = g.get_cell(5, 0);
    assert_ne!(
        got.flags & flags::CLEARED,
        0,
        "cleared cell must read CLEARED even if it was extended"
    );
    assert_eq!(got.bg, 4);
    assert_eq!(got.data.bytes(), b" ");
}
