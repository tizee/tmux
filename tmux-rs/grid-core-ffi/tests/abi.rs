//! Behavior tests through the C ABI: what tmux C code will observe.
//!
//! These call the exported extern "C" functions exactly as the C shim will,
//! asserting the handle's mirror fields (`gd->hsize` reads in C) stay in
//! sync and values roundtrip across the boundary.

use grid_core::cell::UTF8_SIZE;
use grid_core_ffi::*;
use std::ffi::CStr;

fn c_cell(ch: &str, width: u8) -> rust_grid_cell {
    let mut data = [0u8; UTF8_SIZE];
    data[..ch.len()].copy_from_slice(ch.as_bytes());
    rust_grid_cell {
        data,
        have: ch.len() as u8,
        size: ch.len() as u8,
        width,
        attr: 0,
        flags: 0,
        fg: 8,
        bg: 8,
        us: 8,
        link: 0,
    }
}

unsafe fn storage_lines(gd: *const rust_grid) -> u32 {
    let (mut lines, mut cells, mut extd) = (0, 0, 0);
    let (mut linebytes, mut cellbytes, mut extdbytes) = (0, 0, 0);
    rust_grid_storage(
        gd,
        &mut lines,
        &mut cells,
        &mut extd,
        &mut linebytes,
        &mut cellbytes,
        &mut extdbytes,
    );
    lines
}

#[test]
fn create_destroy_roundtrip_without_leak_or_crash() {
    for _ in 0..100 {
        let gd = rust_grid_create(80, 24, 1000);
        unsafe { rust_grid_destroy(gd) };
    }
    unsafe { rust_grid_destroy(std::ptr::null_mut()) }; // NULL tolerated
}

#[test]
fn mirror_fields_track_history_like_c_struct_reads() {
    // C code reads gd->hsize / gd->scroll_added directly; the mirror must
    // update on every mutating call.
    let gd = rust_grid_create(80, 24, 100);
    unsafe {
        assert_eq!((*gd).sx, 80);
        assert_eq!((*gd).sy, 24);
        assert_eq!((*gd).hsize, 0);

        rust_grid_scroll_history(gd, 8);
        rust_grid_scroll_history(gd, 8);
        assert_eq!((*gd).hsize, 2, "gd->hsize must reflect the scrolls");
        assert_eq!((*gd).scroll_added, 2);
        assert_eq!((*gd).hscrolled, 2);

        rust_grid_clear_history(gd);
        assert_eq!((*gd).hsize, 0);
        assert!((*gd).scroll_generation > 0);

        rust_grid_destroy(gd);
    }
}

#[test]
fn cell_roundtrip_across_the_abi() {
    let gd = rust_grid_create(80, 24, 0);
    unsafe {
        let mut gc = c_cell("中", 2);
        gc.attr = 0x1ff;
        gc.fg = 0x123456 | 0x0200_0000; // RGB
        gc.link = 7;
        rust_grid_set_cell(gd, 3, 4, &gc);

        let mut out = c_cell(" ", 1);
        rust_grid_get_cell(gd, 3, 4, &mut out);
        assert_eq!(&out.data[..3], "中".as_bytes());
        assert_eq!(out.width, 2);
        assert_eq!(out.attr, 0x1ff);
        assert_eq!(out.fg, 0x123456 | 0x0200_0000);
        assert_eq!(out.link, 7);

        // Out of range reads the default cell, exactly like C.
        rust_grid_get_cell(gd, 999, 999, &mut out);
        assert_eq!(out.data[0], b' ');
        assert_eq!(out.width, 1);

        rust_grid_destroy(gd);
    }
}

#[test]
fn set_cells_and_string_cells_roundtrip() {
    let gd = rust_grid_create(80, 24, 0);
    unsafe {
        let style = c_cell(" ", 1);
        let text = b"hello ffi  ";
        rust_grid_set_cells(gd, 0, 0, &style, text.as_ptr() as *const _, text.len());

        let s = rust_grid_string_cells(gd, 0, 0, 80, RUST_GRID_STRING_TRIM_SPACES);
        assert_eq!(CStr::from_ptr(s).to_str().unwrap(), "hello ffi");
        rust_grid_string_free(s);

        assert_eq!(rust_grid_line_length(gd, 0), 9);
        rust_grid_destroy(gd);
    }
}

#[test]
fn duplicate_lines_between_handles_is_deep() {
    let src = rust_grid_create(80, 4, 0);
    let dst = rust_grid_create(80, 4, 0);
    unsafe {
        rust_grid_set_cell(src, 0, 0, &c_cell("界", 2));
        rust_grid_duplicate_lines(dst, 0, src, 0, 1);

        // Mutate source; destination must not change (the crash-class
        // contract, now observable through the ABI).
        rust_grid_clear_lines(src, 0, 1, 8);
        let mut out = c_cell(" ", 1);
        rust_grid_get_cell(dst, 0, 0, &mut out);
        assert_eq!(&out.data[..3], "界".as_bytes());

        assert_eq!(rust_grid_compare(src, dst), 1, "grids now differ");
        rust_grid_destroy(src);
        rust_grid_destroy(dst);
    }
}

#[test]
fn reflow_updates_mirror_geometry() {
    let gd = rust_grid_create(10, 4, 100);
    unsafe {
        let style = c_cell(" ", 1);
        let text = b"abcdefghij";
        rust_grid_set_cells(gd, 0, 0, &style, text.as_ptr() as *const _, text.len());
        rust_grid_reflow(gd, 5);
        assert_eq!((*gd).sx, 5, "gd->sx must reflect the reflow");
        assert_eq!((*gd).sy, 4);
        assert!((*gd).hsize > 0, "split pushes rows into history");
        rust_grid_destroy(gd);
    }
}

#[test]
fn wrap_position_roundtrip_via_out_params() {
    let gd = rust_grid_create(10, 4, 100);
    unsafe {
        let style = c_cell(" ", 1);
        let text = b"abcdefghij";
        rust_grid_set_cells(gd, 0, 0, &style, text.as_ptr() as *const _, text.len());
        let (mut wx, mut wy) = (0u32, 0u32);
        rust_grid_wrap_position(gd, 7, 0, &mut wx, &mut wy);
        rust_grid_reflow(gd, 5);
        let (mut px, mut py) = (0u32, 0u32);
        rust_grid_unwrap_position(gd, &mut px, &mut py, wx, wy);
        let mut out = c_cell(" ", 1);
        rust_grid_get_cell(gd, px, py, &mut out);
        assert_eq!(out.data[0], b'h', "position survives reflow via ABI");
        rust_grid_destroy(gd);
    }
}

#[test]
fn c_resize_geometry_writes_survive_adjust_lines() {
    // screen_resize writes gd->hsize/gd->hscrolled directly, then calls
    // grid_adjust_lines(gd, gd->hsize + new_sy). The Rust engine must import
    // those public C writes before resizing its owned line storage.
    let gd = rust_grid_create(80, 10, 100);
    unsafe {
        (*gd).hsize += 4;
        (*gd).hscrolled += 4;

        rust_grid_adjust_lines(gd, (*gd).hsize + 6);

        assert_eq!((*gd).hsize, 4, "C-written history size survives");
        assert_eq!((*gd).hscrolled, 4, "C-written scroll offset survives");
        assert_eq!((*gd).sy, 6, "viewport height derives from total lines");
        assert_eq!(storage_lines(gd), 10, "history + viewport line count");

        rust_grid_clear_history(gd);
        assert_eq!((*gd).hsize, 0);
        assert_eq!((*gd).sy, 6);
        assert_eq!(storage_lines(gd), 6);
        rust_grid_destroy(gd);
    }
}

#[test]
fn c_copy_clone_geometry_writes_survive_set_hscrolled() {
    // window_copy_clone_screen creates a backing grid, duplicates total rows,
    // then writes gd->sy/gd->hsize directly before grid_set_hscrolled().
    let gd = rust_grid_create(80, 12, 100);
    unsafe {
        (*gd).sy = 7;
        (*gd).hsize = 5;

        rust_grid_set_hscrolled(gd, 2);

        assert_eq!((*gd).sy, 7, "C-written viewport height survives");
        assert_eq!((*gd).hsize, 5, "C-written history size survives");
        assert_eq!((*gd).hscrolled, 2, "explicit scroll offset is applied");
        assert_eq!(storage_lines(gd), 12, "history + viewport line count");

        rust_grid_clear_history(gd);
        assert_eq!((*gd).hsize, 0);
        assert_eq!((*gd).sy, 7);
        assert_eq!(storage_lines(gd), 7);
        rust_grid_destroy(gd);
    }
}

#[test]
fn abi_struct_layout_matches_c_expectations() {
    // struct grid_cell mirror: utf8_data (32+3 bytes) then u16 attr at a
    // 2-byte boundary, ints 4-aligned — the same padding C computes. If
    // this drifts, the C shim would read garbage.
    assert_eq!(std::mem::offset_of!(rust_grid_cell, have), 32);
    assert_eq!(std::mem::offset_of!(rust_grid_cell, size), 33);
    assert_eq!(std::mem::offset_of!(rust_grid_cell, width), 34);
    assert_eq!(std::mem::offset_of!(rust_grid_cell, attr), 36);
    assert_eq!(std::mem::offset_of!(rust_grid_cell, flags), 38);
    assert_eq!(std::mem::offset_of!(rust_grid_cell, fg), 40);
    assert_eq!(std::mem::offset_of!(rust_grid_cell, bg), 44);
    assert_eq!(std::mem::offset_of!(rust_grid_cell, us), 48);
    assert_eq!(std::mem::offset_of!(rust_grid_cell, link), 52);
    assert_eq!(std::mem::size_of::<rust_grid_cell>(), 56);

    // Handle header: the fields C reads, in C struct grid order.
    assert_eq!(std::mem::offset_of!(rust_grid, sx), 4);
    assert_eq!(std::mem::offset_of!(rust_grid, sy), 8);
    assert_eq!(std::mem::offset_of!(rust_grid, hscrolled), 12);
    assert_eq!(std::mem::offset_of!(rust_grid, hsize), 16);
    assert_eq!(std::mem::offset_of!(rust_grid, hlimit), 20);
}
