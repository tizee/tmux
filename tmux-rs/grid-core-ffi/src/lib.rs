//! C ABI for grid-core.
//!
//! This is the only unsafe layer. Design:
//!
//! - `rust_grid` is a `#[repr(C)]` handle whose leading fields mirror the C
//!   `struct grid` public fields (tmux C code reads `gd->sx`, `gd->hsize`,
//!   `gd->scroll_generation` etc. directly). Every mutating call re-syncs
//!   the mirror from the owned Rust `Grid`.
//! - Cells cross the boundary as `rust_grid_cell`, a `#[repr(C)]` mirror of
//!   C `struct grid_cell` (same field order; layout asserted by tests).
//! - Strings returned by `rust_grid_string_cells` are malloc-compatible via
//!   `rust_grid_string_free`.
//!
//! Naming uses a `rust_grid_` prefix; the Phase-4 shim maps tmux's
//! `grid_*` calls onto these.

use grid_core::cell::UTF8_SIZE;
use grid_core::grid::StringOpts;
use grid_core::{Grid, GridCell, Utf8Data};
use std::ffi::CString;
use std::os::raw::{c_char, c_int};

/// Mirror of C `struct utf8_data` + style fields of `struct grid_cell`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct rust_grid_cell {
    pub data: [u8; UTF8_SIZE],
    pub have: u8,
    pub size: u8,
    pub width: u8,
    pub attr: u16,
    pub flags: u8,
    pub fg: i32,
    pub bg: i32,
    pub us: i32,
    pub link: u32,
}

impl From<GridCell> for rust_grid_cell {
    fn from(gc: GridCell) -> Self {
        Self {
            data: gc.data.data,
            have: gc.data.have,
            size: gc.data.size,
            width: gc.data.width,
            attr: gc.attr,
            flags: gc.flags,
            fg: gc.fg,
            bg: gc.bg,
            us: gc.us,
            link: gc.link,
        }
    }
}

impl From<&rust_grid_cell> for GridCell {
    fn from(c: &rust_grid_cell) -> Self {
        GridCell {
            data: Utf8Data {
                data: c.data,
                have: c.have,
                size: c.size,
                width: c.width,
            },
            attr: c.attr,
            flags: c.flags,
            fg: c.fg,
            bg: c.bg,
            us: c.us,
            link: c.link,
        }
    }
}

/// C-visible grid handle. Leading fields mirror C `struct grid` so existing
/// reads like `gd->hsize` keep working; `grid` owns the Rust state.
#[repr(C)]
pub struct rust_grid {
    pub flags: c_int,
    pub sx: u32,
    pub sy: u32,
    pub hscrolled: u32,
    pub hsize: u32,
    pub hlimit: u32,
    pub scroll_added: u32,
    pub scroll_collected: u32,
    pub scroll_generation: u32,
    grid: *mut Grid,
}

impl rust_grid {
    fn inner(&self) -> &Grid {
        // SAFETY: `grid` is set once at create and freed only at destroy.
        unsafe { &*self.grid }
    }

    fn inner_mut(&mut self) -> &mut Grid {
        // SAFETY: single-threaded tmux server; no concurrent access.
        unsafe { &mut *self.grid }
    }

    /// Re-sync the C-visible mirror fields after a mutation.
    fn sync(&mut self) {
        let (sx, sy, hscrolled, hsize, added, collected, generation) = {
            let g = self.inner();
            (
                g.sx(),
                g.sy(),
                g.hscrolled(),
                g.hsize(),
                g.scroll_added(),
                g.scroll_collected(),
                g.scroll_generation(),
            )
        };
        self.sx = sx;
        self.sy = sy;
        self.hscrolled = hscrolled;
        self.hsize = hsize;
        self.scroll_added = added;
        self.scroll_collected = collected;
        self.scroll_generation = generation;
    }

    /// Import public `struct grid` fields that tmux C code wrote directly.
    fn import_public_geometry(&mut self) {
        let (sy, hscrolled, hsize, hlimit) = (self.sy, self.hscrolled, self.hsize, self.hlimit);
        self.inner_mut()
            .import_public_geometry(sy, hscrolled, hsize, hlimit);
    }

    /// Apply C `grid_adjust_lines`, deriving the new viewport height from the
    /// total line count and the C-written history size.
    fn adjust_lines_from_public_geometry(&mut self, lines: u32) {
        let (hscrolled, hsize, hlimit) = (self.hscrolled, self.hsize, self.hlimit);
        self.inner_mut()
            .adjust_lines_from_public_geometry(lines, hscrolled, hsize, hlimit);
        self.sync();
    }
}

/// # Safety
/// Returns an owned handle; release with [`rust_grid_destroy`].
#[no_mangle]
pub extern "C" fn rust_grid_create(sx: u32, sy: u32, hlimit: u32) -> *mut rust_grid {
    let grid = Box::into_raw(Box::new(Grid::new(sx, sy, hlimit)));
    let mut h = Box::new(rust_grid {
        flags: if hlimit != 0 { 0x1 } else { 0 }, /* GRID_HISTORY */
        sx,
        sy,
        hscrolled: 0,
        hsize: 0,
        hlimit,
        scroll_added: 0,
        scroll_collected: 0,
        scroll_generation: 0,
        grid,
    });
    h.sync();
    Box::into_raw(h)
}

/// # Safety
/// `gd` must be a handle from [`rust_grid_create`], not yet destroyed.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_destroy(gd: *mut rust_grid) {
    if gd.is_null() {
        return;
    }
    let h = Box::from_raw(gd);
    drop(Box::from_raw(h.grid));
}

macro_rules! with {
    ($gd:expr, $g:ident, $body:expr) => {{
        let h = &mut *$gd;
        let $g = h.inner_mut();
        let r = $body;
        h.sync();
        r
    }};
}

/// # Safety
/// `gd` valid handle; `gc` non-null.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_get_cell(
    gd: *const rust_grid,
    px: u32,
    py: u32,
    gc: *mut rust_grid_cell,
) {
    *gc = (*gd).inner().get_cell(px, py).into();
}

/// # Safety
/// `gd` valid handle; `gc` non-null.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_set_cell(
    gd: *mut rust_grid,
    px: u32,
    py: u32,
    gc: *const rust_grid_cell,
) {
    with!(gd, g, g.set_cell(px, py, &GridCell::from(&*gc)))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_set_padding(gd: *mut rust_grid, px: u32, py: u32) {
    with!(gd, g, g.set_padding(px, py))
}

/// # Safety
/// `gd` valid handle; `s` points to `slen` bytes.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_set_cells(
    gd: *mut rust_grid,
    px: u32,
    py: u32,
    gc: *const rust_grid_cell,
    s: *const c_char,
    slen: usize,
) {
    let bytes = std::slice::from_raw_parts(s as *const u8, slen);
    let text = String::from_utf8_lossy(bytes);
    with!(gd, g, g.set_cells(px, py, &GridCell::from(&*gc), &text))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_clear(
    gd: *mut rust_grid,
    px: u32,
    py: u32,
    nx: u32,
    ny: u32,
    bg: u32,
) {
    with!(gd, g, g.clear(px, py, nx, ny, bg))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_clear_lines(gd: *mut rust_grid, py: u32, ny: u32, bg: u32) {
    with!(gd, g, g.clear_lines(py, ny, bg))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_move_lines(
    gd: *mut rust_grid,
    dy: u32,
    py: u32,
    ny: u32,
    bg: u32,
) {
    with!(gd, g, g.move_lines(dy, py, ny, bg))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_move_cells(
    gd: *mut rust_grid,
    dx: u32,
    px: u32,
    py: u32,
    nx: u32,
    bg: u32,
) {
    with!(gd, g, g.move_cells(dx, px, py, nx, bg))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_scroll_history(gd: *mut rust_grid, bg: u32) {
    with!(gd, g, g.scroll_history(bg))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_scroll_history_region(
    gd: *mut rust_grid,
    upper: u32,
    lower: u32,
    bg: u32,
) {
    with!(gd, g, g.scroll_history_region(upper, lower, bg))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_collect_history(gd: *mut rust_grid, all: c_int) {
    with!(gd, g, g.collect_history(all != 0))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_remove_history(gd: *mut rust_grid, ny: u32) {
    with!(gd, g, g.remove_history(ny))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_clear_history(gd: *mut rust_grid) {
    with!(gd, g, g.clear_history())
}

/// # Safety
/// Both handles valid and distinct.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_duplicate_lines(
    dst: *mut rust_grid,
    dy: u32,
    src: *const rust_grid,
    sy: u32,
    ny: u32,
) {
    let d = &mut *dst;
    Grid::duplicate_lines(d.inner_mut(), dy, (*src).inner(), sy, ny);
    d.sync();
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_reflow(gd: *mut rust_grid, sx: u32) {
    with!(gd, g, g.reflow(sx))
}

/// # Safety
/// Both handles valid. Returns 0 when equal (C `grid_compare` convention).
#[no_mangle]
pub unsafe extern "C" fn rust_grid_compare(ga: *const rust_grid, gb: *const rust_grid) -> c_int {
    if (*ga).inner().compare((*gb).inner()) {
        0
    } else {
        1
    }
}

/// # Safety
/// `gd` valid handle; `wx`/`wy` non-null.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_wrap_position(
    gd: *const rust_grid,
    px: u32,
    py: u32,
    wx: *mut u32,
    wy: *mut u32,
) {
    let (x, y) = (*gd).inner().wrap_position(px, py);
    *wx = x;
    *wy = y;
}

/// # Safety
/// `gd` valid handle; `px`/`py` non-null.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_unwrap_position(
    gd: *const rust_grid,
    px: *mut u32,
    py: *mut u32,
    wx: u32,
    wy: u32,
) {
    let (x, y) = (*gd).inner().unwrap_position(wx, wy);
    *px = x;
    *py = y;
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_line_length(gd: *const rust_grid, py: u32) -> u32 {
    (*gd).inner().line_length(py)
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_line_flags(gd: *const rust_grid, py: u32) -> c_int {
    (*gd).inner().line_flags(py)
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_set_line_flag(
    gd: *mut rust_grid,
    py: u32,
    flag: c_int,
    on: c_int,
) {
    with!(gd, g, g.set_line_flag(py, flag, on != 0))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_line_cellsize(gd: *const rust_grid, py: u32) -> u32 {
    (*gd).inner().line_cellsize(py)
}

/// Flags for [`rust_grid_string_cells`] (subset of C GRID_STRING_*).
pub const RUST_GRID_STRING_EMPTY_CELLS: c_int = 0x1;
pub const RUST_GRID_STRING_TRIM_SPACES: c_int = 0x2;

/// # Safety
/// `gd` valid handle. Free the result with [`rust_grid_string_free`].
#[no_mangle]
pub unsafe extern "C" fn rust_grid_string_cells(
    gd: *const rust_grid,
    px: u32,
    py: u32,
    nx: u32,
    flags: c_int,
) -> *mut c_char {
    let opts = StringOpts {
        empty_cells: flags & RUST_GRID_STRING_EMPTY_CELLS != 0,
        trim_spaces: flags & RUST_GRID_STRING_TRIM_SPACES != 0,
    };
    let s = (*gd).inner().string_cells(px, py, nx, opts);
    // Interior NULs cannot occur (cells hold UTF-8 text); fall back safely.
    match CString::new(s) {
        Ok(cs) => cs.into_raw(),
        Err(_) => CString::new("").unwrap().into_raw(),
    }
}

/// # Safety
/// `s` must come from [`rust_grid_string_cells`]; frees it.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_string_free(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_adjust_lines(gd: *mut rust_grid, lines: u32) {
    (*gd).adjust_lines_from_public_geometry(lines);
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_empty_line(gd: *mut rust_grid, py: u32, bg: u32) {
    with!(gd, g, g.empty_line(py, bg))
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_set_hscrolled(gd: *mut rust_grid, hscrolled: u32) {
    let h = &mut *gd;
    h.import_public_geometry();
    h.inner_mut().set_hscrolled(hscrolled);
    h.sync();
}

/// # Safety
/// Both handles valid and distinct; counters pre-validated by the caller.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_sync_history(
    dst: *mut rust_grid,
    src: *const rust_grid,
    added: u32,
    collected: u32,
) {
    let d = &mut *dst;
    Grid::sync_history(d.inner_mut(), (*src).inner(), added, collected);
    d.sync();
}

/// # Safety
/// `gd` valid handle; all out-pointers non-null.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_storage(
    gd: *const rust_grid,
    lines: *mut u32,
    cells: *mut u32,
    extd: *mut u32,
    linebytes: *mut usize,
    cellbytes: *mut usize,
    extdbytes: *mut usize,
) {
    let (l, c, e, lb, cb, eb) = (*gd).inner().storage();
    *lines = l;
    *cells = c;
    *extd = e;
    *linebytes = lb;
    *cellbytes = cb;
    *extdbytes = eb;
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_line_cellused(gd: *const rust_grid, py: u32) -> u32 {
    (*gd).inner().line_cellused(py)
}

/// # Safety
/// `gd` valid handle.
#[no_mangle]
pub unsafe extern "C" fn rust_grid_line_time(gd: *const rust_grid, py: u32) -> i64 {
    (*gd).inner().line_time(py)
}
