//! The grid: history + viewport lines of cells.
//!
//! Mirrors tmux `grid.c` observable behavior. Lines are owned by a `Vec`;
//! aliasing between line slots (the C double-free class) is unrepresentable.

use crate::cell::GridCell;
use crate::codec::{PackedCodec, Utf8Char, Utf8Codec};
use crate::{colour, flags, line_flags};

/// One stored cell entry: either the compact inline form or an index into the
/// line's extended entries (internal; mirrors C `grid_cell_entry` semantics,
/// not its layout).
#[derive(Clone, Copy, Debug)]
enum CellEntry {
    /// attr/fg/bg limited to 8 bits, single byte of character data.
    Simple {
        attr: u8,
        fg: u8,
        bg: u8,
        ch: u8,
        flags: u8,
    },
    /// Index into `Line::extended`.
    Extended { offset: u32 },
}

impl CellEntry {
    fn cleared(bg: u32) -> Self {
        // C grid_cleared_entry, with the low-byte bg applied when not
        // default (256-palette bg sets the BG256 flag).
        let mut f = flags::CLEARED;
        let mut b = 8u8;
        if bg != 8 {
            if bg & (colour::FLAG_256 as u32) != 0 {
                f |= flags::BG256;
            }
            b = (bg & 0xff) as u8;
        }
        CellEntry::Simple {
            attr: 0,
            fg: 8,
            bg: b,
            ch: b' ',
            flags: f,
        }
    }
}

/// One extended cell (mirrors C `grid_extd_entry`).
#[derive(Clone, Copy, Debug)]
struct ExtdEntry {
    data: Utf8Char,
    attr: u16,
    flags: u8,
    fg: i32,
    bg: i32,
    us: i32,
    link: u32,
}

/// One grid line. Owns its cells; dropping the line frees them exactly once.
#[derive(Clone, Debug, Default)]
struct Line {
    cells: Vec<CellEntry>,
    used: u32,
    extended: Vec<ExtdEntry>,
    flags: i32,
    time: i64,
}

/// True when a cell cannot be stored in the compact entry
/// (C `grid_need_extended_cell`).
fn need_extended(entry: &CellEntry, gc: &GridCell) -> bool {
    if matches!(entry, CellEntry::Extended { .. }) {
        return true;
    }
    if gc.attr > 0xff {
        return true;
    }
    if gc.data.size > 1 || gc.data.width > 1 {
        return true;
    }
    if (gc.fg & (colour::FLAG_RGB | colour::FLAG_THEME)) != 0
        || (gc.bg & (colour::FLAG_RGB | colour::FLAG_THEME)) != 0
    {
        return true;
    }
    if gc.us != 8 {
        return true;
    }
    if gc.link != 0 {
        return true;
    }
    if gc.flags & flags::TAB != 0 {
        return true;
    }
    false
}

/// The grid of cells: rows `0..hsize` are history, rows `hsize..hsize+sy`
/// are the viewport. All coordinates here are absolute (like `grid.c`).
pub struct Grid {
    sx: u32,
    sy: u32,
    hscrolled: u32,
    hsize: u32,
    hlimit: u32,
    scroll_added: u32,
    scroll_collected: u32,
    scroll_generation: u32,
    lines: Vec<Line>,
    codec: PackedCodec,
}

impl Grid {
    /// Create a grid of `sx` x `sy` cells with a history limit of `hlimit`
    /// lines (0 disables history).
    pub fn new(sx: u32, sy: u32, hlimit: u32) -> Self {
        Self {
            sx,
            sy,
            hscrolled: 0,
            hsize: 0,
            hlimit,
            scroll_added: 0,
            scroll_collected: 0,
            scroll_generation: 0,
            lines: (0..sy).map(|_| Line::default()).collect(),
            codec: PackedCodec::new(),
        }
    }

    // --- Geometry and counters (used by callers such as window-copy). ---

    pub fn sx(&self) -> u32 {
        self.sx
    }

    pub fn sy(&self) -> u32 {
        self.sy
    }

    pub fn hsize(&self) -> u32 {
        self.hsize
    }

    pub fn hscrolled(&self) -> u32 {
        self.hscrolled
    }

    pub fn scroll_added(&self) -> u32 {
        self.scroll_added
    }

    pub fn scroll_collected(&self) -> u32 {
        self.scroll_collected
    }

    pub fn scroll_generation(&self) -> u32 {
        self.scroll_generation
    }

    /// Total rows (history + viewport).
    fn rows(&self) -> u32 {
        self.hsize + self.sy
    }

    /// C `grid_check_y`: absolute row bound check.
    fn check_y(&self, py: u32) -> bool {
        py < self.rows()
    }

    // --- Cell access. ---

    /// Read the cell at (`px`, `py`). Out-of-range coordinates and cells that
    /// were never written read as the default cell (C `grid_get_cell`).
    pub fn get_cell(&self, px: u32, py: u32) -> GridCell {
        if !self.check_y(py) {
            return GridCell::default_cell();
        }
        let line = &self.lines[py as usize];
        let Some(entry) = line.cells.get(px as usize) else {
            return GridCell::default_cell();
        };
        match *entry {
            CellEntry::Simple {
                attr,
                fg,
                bg,
                ch,
                flags: f,
            } => {
                let mut gc = GridCell::default_cell();
                gc.flags = f & !(flags::FG256 | flags::BG256);
                gc.attr = attr as u16;
                gc.fg = fg as i32;
                if f & flags::FG256 != 0 {
                    gc.fg |= colour::FLAG_256;
                }
                gc.bg = bg as i32;
                if f & flags::BG256 != 0 {
                    gc.bg |= colour::FLAG_256;
                }
                gc.us = 8;
                gc.link = 0;
                gc.data = crate::cell::Utf8Data::from_str(" ", 1);
                gc.data.data[0] = ch;
                gc
            }
            CellEntry::Extended { offset } => {
                let Some(gee) = line.extended.get(offset as usize) else {
                    // C: stale offset reads as default.
                    return GridCell::default_cell();
                };
                let mut gc = GridCell::default_cell();
                gc.flags = gee.flags;
                gc.attr = gee.attr;
                gc.fg = gee.fg;
                gc.bg = gee.bg;
                gc.us = gee.us;
                gc.link = gee.link;
                if gee.flags & flags::TAB != 0 {
                    gc.set_tab(gee.data);
                } else {
                    gc.data = self.codec.decode(gee.data);
                }
                gc
            }
        }
    }

    /// Store a cell into an entry, extending the line as needed
    /// (C `grid_store_cell` / `grid_extended_cell`).
    fn store(&mut self, px: u32, py: u32, gc: &GridCell, ch: u8) {
        let entry = self.lines[py as usize].cells[px as usize];
        if need_extended(&entry, gc) {
            let uc = if gc.flags & flags::TAB != 0 {
                gc.data.width as Utf8Char
            } else {
                self.codec.encode(&gc.data)
            };
            let line = &mut self.lines[py as usize];
            let gee = ExtdEntry {
                data: uc,
                attr: gc.attr,
                flags: gc.flags & !flags::CLEARED,
                fg: gc.fg,
                bg: gc.bg,
                us: gc.us,
                link: gc.link,
            };
            let offset = match entry {
                CellEntry::Extended { offset } if (offset as usize) < line.extended.len() => {
                    line.extended[offset as usize] = gee;
                    offset
                }
                _ => {
                    line.extended.push(gee);
                    (line.extended.len() - 1) as u32
                }
            };
            line.cells[px as usize] = CellEntry::Extended { offset };
            line.flags |= line_flags::EXTENDED;
            if gc.link != 0 {
                line.flags |= line_flags::HYPERLINK;
            }
        } else {
            let mut f = gc.flags & !flags::CLEARED;
            if gc.fg & colour::FLAG_256 != 0 {
                f |= flags::FG256;
            }
            if gc.bg & colour::FLAG_256 != 0 {
                f |= flags::BG256;
            }
            self.lines[py as usize].cells[px as usize] = CellEntry::Simple {
                attr: gc.attr as u8,
                fg: (gc.fg & 0xff) as u8,
                bg: (gc.bg & 0xff) as u8,
                ch,
                flags: f,
            };
        }
    }

    /// Grow a line so `sx` cells exist, clearing new cells to `bg`
    /// (C `grid_expand_line`; growth policy simplified, observably equal).
    fn expand_line(&mut self, py: u32, sx: u32, bg: u32) {
        let cur = self.lines[py as usize].cells.len() as u32;
        if sx <= cur {
            return;
        }
        let mut target = sx;
        if target < self.sx / 4 {
            target = self.sx / 4;
        } else if target < self.sx / 2 {
            target = self.sx / 2;
        } else if target < self.sx {
            target = self.sx;
        }
        let rgb = bg & ((colour::FLAG_RGB | colour::FLAG_THEME) as u32) != 0;
        self.lines[py as usize]
            .cells
            .resize(target as usize, CellEntry::cleared(bg));
        if rgb {
            // C fills grown cells via grid_clear_cell, which for RGB bg
            // creates extended entries.
            for xx in cur..target {
                self.clear_cell(xx, py, bg);
            }
        }
    }

    /// Write a cell (C `grid_set_cell`).
    pub fn set_cell(&mut self, px: u32, py: u32, gc: &GridCell) {
        if !self.check_y(py) {
            return;
        }
        self.expand_line(py, px + 1, 8);
        let line = &mut self.lines[py as usize];
        if px + 1 > line.used {
            line.used = px + 1;
        }
        self.store(px, py, gc, gc.data.data[0]);
    }

    /// Write the padding cell stored after a wide character.
    pub fn set_padding(&mut self, px: u32, py: u32) {
        self.set_cell(px, py, &GridCell::padding_cell());
    }

    /// Write a run of single-byte characters with one style
    /// (C `grid_set_cells`).
    pub fn set_cells(&mut self, px: u32, py: u32, gc: &GridCell, s: &str) {
        if !self.check_y(py) {
            return;
        }
        let slen = s.len() as u32;
        self.expand_line(py, px + slen, 8);
        let line = &mut self.lines[py as usize];
        if px + slen > line.used {
            line.used = px + slen;
        }
        for (i, ch) in s.bytes().enumerate() {
            let mut one = *gc;
            one.data = crate::cell::Utf8Data::from_str(" ", 1);
            one.data.data[0] = ch;
            self.store(px + i as u32, py, &one, ch);
        }
    }

    // --- Line metadata. ---

    /// Line flags (C reads `gl->flags`); 0 for out-of-range.
    pub fn line_flags(&self, py: u32) -> i32 {
        if !self.check_y(py) {
            return 0;
        }
        self.lines[py as usize].flags
    }

    /// Set/clear a line flag (C writes `gl->flags`).
    pub fn set_line_flag(&mut self, py: u32, flag: i32, on: bool) {
        if !self.check_y(py) {
            return;
        }
        let line = &mut self.lines[py as usize];
        if on {
            line.flags |= flag;
        } else {
            line.flags &= !flag;
        }
    }

    /// Number of used cells in a line (C `gl->cellused`); 0 out of range.
    pub fn line_cellused(&self, py: u32) -> u32 {
        if !self.check_y(py) {
            return 0;
        }
        self.lines[py as usize].used
    }

    /// Line timestamp (C `gl->time`); 0 out of range.
    pub fn line_time(&self, py: u32) -> i64 {
        if !self.check_y(py) {
            return 0;
        }
        self.lines[py as usize].time
    }

    /// Number of cells allocated in a line (C `gl->cellsize`).
    pub fn line_cellsize(&self, py: u32) -> u32 {
        if !self.check_y(py) {
            return 0;
        }
        self.lines[py as usize].cells.len() as u32
    }

    // --- Clearing. ---

    /// Reset one line; a non-default `bg` refills it to `sx`
    /// (C `grid_empty_line`).
    pub fn empty_line(&mut self, py: u32, bg: u32) {
        self.lines[py as usize] = Line::default();
        if !colour::is_default(bg as i32) {
            self.expand_line(py, self.sx, bg);
        }
    }

    /// Clear one cell to a background.
    ///
    /// Deliberate divergence from C `grid_clear_cell`: C loses the CLEARED
    /// flag when the cell was previously extended (reads back flags 0) - an
    /// implementation leak, not a design. Here a cleared cell always reads
    /// back CLEARED, whatever its history. The difftest masks this bit.
    fn clear_cell(&mut self, px: u32, py: u32, bg: u32) {
        if px as usize >= self.lines[py as usize].cells.len() {
            return;
        }
        if bg & ((colour::FLAG_RGB | colour::FLAG_THEME) as u32) != 0 {
            let uc = self.codec.encode(&crate::cell::Utf8Data::from_str(" ", 1));
            let line = &mut self.lines[py as usize];
            line.extended.push(ExtdEntry {
                data: uc,
                attr: 0,
                flags: flags::CLEARED,
                fg: 8,
                bg: bg as i32,
                us: 8,
                link: 0,
            });
            line.cells[px as usize] = CellEntry::Extended {
                offset: (line.extended.len() - 1) as u32,
            };
            line.flags |= line_flags::EXTENDED;
        } else {
            self.lines[py as usize].cells[px as usize] = CellEntry::cleared(bg);
        }
    }

    /// Clear an area to a background colour (C `grid_clear`).
    pub fn clear(&mut self, px: u32, py: u32, nx: u32, ny: u32, bg: u32) {
        if nx == 0 || ny == 0 {
            return;
        }
        if px == 0 && nx == self.sx {
            self.clear_lines(py, ny, bg);
            return;
        }
        if !self.check_y(py) || !self.check_y(py + ny - 1) {
            return;
        }
        for yy in py..py + ny {
            let cellsize = self.lines[yy as usize].cells.len() as u32;
            let sx = self.sx.min(cellsize);
            let mut ox = nx;
            if colour::is_default(bg as i32) {
                if px > sx {
                    continue;
                }
                if px + nx > sx {
                    ox = sx - px;
                }
            }
            self.expand_line(yy, px + ox, 8);
            for xx in px..px + ox {
                self.clear_cell(xx, yy, bg);
            }
        }
    }

    /// Free and reset whole lines (C `grid_clear_lines`).
    pub fn clear_lines(&mut self, py: u32, ny: u32, bg: u32) {
        if ny == 0 {
            return;
        }
        if !self.check_y(py) || !self.check_y(py + ny - 1) {
            return;
        }
        for yy in py..py + ny {
            self.empty_line(yy, bg);
        }
        if py != 0 {
            self.lines[py as usize - 1].flags &= !line_flags::WRAPPED;
        }
    }

    // --- Moving. ---

    /// Move `ny` lines from `py` to `dy`; vacated lines become empty
    /// (C `grid_move_lines`).
    pub fn move_lines(&mut self, dy: u32, py: u32, ny: u32, bg: u32) {
        if ny == 0 || py == dy {
            return;
        }
        if !self.check_y(py)
            || !self.check_y(py + ny - 1)
            || !self.check_y(dy)
            || !self.check_y(dy + ny - 1)
        {
            return;
        }
        // Take the source lines out; their slots become default (the C
        // "wipe without freeing" step, expressed as ownership transfer).
        let moved: Vec<Line> = (py..py + ny)
            .map(|yy| std::mem::take(&mut self.lines[yy as usize]))
            .collect();
        if dy != 0 {
            self.lines[dy as usize - 1].flags &= !line_flags::WRAPPED;
        }
        for (i, line) in moved.into_iter().enumerate() {
            self.lines[dy as usize + i] = line;
        }
        // Vacated source slots outside the destination range get the bg fill.
        for yy in py..py + ny {
            if yy < dy || yy >= dy + ny {
                self.empty_line(yy, bg);
            }
        }
        if py != 0 && (py < dy || py >= dy + ny) {
            self.lines[py as usize - 1].flags &= !line_flags::WRAPPED;
        }
    }

    /// Move cells within a line (C `grid_move_cells`).
    pub fn move_cells(&mut self, dx: u32, px: u32, py: u32, nx: u32, bg: u32) {
        if !self.check_y(py) {
            return;
        }
        self.expand_line(py, px + nx, 8);
        self.expand_line(py, dx + nx, 8);
        let line = &mut self.lines[py as usize];
        line.cells
            .copy_within(px as usize..(px + nx) as usize, dx as usize);
        if dx + nx > line.used {
            line.used = dx + nx;
        }
        for xx in px..px + nx {
            if xx >= dx && xx < dx + nx {
                continue;
            }
            self.clear_cell(xx, py, bg);
        }
    }

    // --- History. ---

    /// Scroll the viewport up one line into the history (C
    /// `grid_scroll_history`). Increments `scroll_added`.
    pub fn scroll_history(&mut self, bg: u32) {
        self.lines.push(Line::default());
        let last = self.rows(); // rows() still uses the old hsize
        self.empty_line(last, bg);
        self.hscrolled += 1;
        self.lines[self.hsize as usize].time = 0;
        self.hsize += 1;
        self.scroll_added += 1;
    }

    /// Free the oldest lines when at the history limit (C
    /// `grid_collect_history`): 10% of `hlimit` normally, everything above
    /// the limit if `all`. Increments `scroll_collected`.
    pub fn collect_history(&mut self, all: bool) {
        if self.hsize == 0 || self.hsize < self.hlimit {
            return;
        }
        let mut ny = if all {
            self.hsize - self.hlimit
        } else {
            self.hlimit / 10
        };
        if ny < 1 {
            ny = 1;
        }
        if ny > self.hsize {
            ny = self.hsize;
        }
        self.lines.drain(0..ny as usize);
        self.hsize -= ny;
        self.scroll_collected += ny;
        if self.hscrolled > self.hsize {
            self.hscrolled = self.hsize;
        }
    }

    /// Remove lines from the bottom of the history (C `grid_remove_history`).
    pub fn remove_history(&mut self, ny: u32) {
        if ny > self.hsize {
            return;
        }
        let keep = (self.rows() - ny) as usize;
        self.lines.truncate(keep);
        self.hsize -= ny;
    }

    /// Drop the entire history (C `grid_clear_history`). Bumps
    /// `scroll_generation`.
    pub fn clear_history(&mut self) {
        self.lines.drain(0..self.hsize as usize);
        self.hscrolled = 0;
        self.hsize = 0;
        self.scroll_generation += 1;
    }

    /// Scroll a region up, moving its top line into the history (C
    /// `grid_scroll_history_region`). `upper`/`lower` are absolute rows.
    pub fn scroll_history_region(&mut self, upper: u32, lower: u32, bg: u32) {
        // Insert an empty slot at the top of the viewport: the whole
        // viewport shifts down one, making room for the history line.
        self.lines.insert(self.hsize as usize, Line::default());
        let upper = upper + 1;
        let lower = lower + 1;
        // Move the region's top line into the freed history slot.
        self.lines[self.hsize as usize] = std::mem::take(&mut self.lines[upper as usize]);
        self.lines[self.hsize as usize].time = 0;
        // Shift the region up over the vacated line and clear the bottom.
        for yy in upper..lower {
            self.lines.swap(yy as usize, yy as usize + 1);
        }
        self.hscrolled += 1;
        self.hsize += 1;
        self.empty_line(lower, bg);
        self.scroll_added += 1;
    }

    // --- Copying. ---

    /// Deep-copy `ny` lines from `src` starting at `sy` into `dst` at `dy`
    /// (C `grid_duplicate_lines`). Counts are clamped to both grids.
    pub fn duplicate_lines(dst: &mut Grid, dy: u32, src: &Grid, sy: u32, ny: u32) {
        let mut ny = ny;
        if dy + ny > dst.rows() {
            ny = dst.rows().saturating_sub(dy);
        }
        if sy + ny > src.rows() {
            ny = src.rows().saturating_sub(sy);
        }
        for i in 0..ny {
            // Clone is a deep copy: Vec ownership makes aliasing impossible.
            dst.lines[(dy + i) as usize] = src.lines[(sy + i) as usize].clone();
        }
        // Characters interned in src's codec must decode in dst: re-encode
        // extended entries through dst's codec.
        for i in 0..ny {
            let line = std::mem::take(&mut dst.lines[(dy + i) as usize]);
            let mut rebuilt = line.clone();
            for gee in rebuilt.extended.iter_mut() {
                if gee.flags & flags::TAB == 0 {
                    let ud = src.codec.decode(gee.data);
                    gee.data = dst.codec.encode(&ud);
                }
            }
            dst.lines[(dy + i) as usize] = rebuilt;
        }
    }

    /// Resize the line array (C `grid_adjust_lines`).
    pub fn adjust_lines(&mut self, lines: u32) {
        self.lines.resize(lines as usize, Line::default());
    }

    /// Set the scroll position (C writes `gd->hscrolled` directly).
    pub fn set_hscrolled(&mut self, hscrolled: u32) {
        self.hscrolled = hscrolled;
    }

    /// Incrementally synchronize a copy grid with its scrolled source
    /// (C `grid_sync_history`): the source scrolled `added` lines into
    /// history and collected `collected` from the top; counters already
    /// validated by the caller.
    pub fn sync_history(dst: &mut Grid, src: &Grid, added: u32, collected: u32) {
        let sy = src.sy;
        let kept = dst.hsize - collected;
        let new_hsize = src.hsize;
        if collected > 0 {
            dst.lines.drain(0..collected as usize);
        }
        dst.lines
            .resize((new_hsize + sy) as usize, Line::default());
        dst.hsize = new_hsize;
        if added > 0 {
            Grid::duplicate_lines(dst, kept, src, kept, added);
        }
        Grid::duplicate_lines(dst, new_hsize, src, new_hsize, sy);
    }

    /// Storage totals (C `grid_storage`): line/cell/extended counts and
    /// their byte sizes in this engine.
    pub fn storage(&self) -> (u32, u32, u32, usize, usize, usize) {
        let lines = self.rows();
        let mut cells = 0u32;
        let mut extd = 0u32;
        for l in &self.lines {
            cells += l.cells.len() as u32;
            extd += l.extended.len() as u32;
        }
        (
            lines,
            cells,
            extd,
            lines as usize * std::mem::size_of::<Line>(),
            cells as usize * std::mem::size_of::<CellEntry>(),
            extd as usize * std::mem::size_of::<ExtdEntry>(),
        )
    }

    /// Compare the viewports of two grids cell-by-cell (C `grid_compare`,
    /// by value: expansion policy differences are invisible).
    pub fn compare(&self, other: &Grid) -> bool {
        if self.sx != other.sx || self.sy != other.sy {
            return false;
        }
        for yy in 0..self.sy {
            for xx in 0..self.sx {
                let a = self.get_cell(xx, yy);
                let b = other.get_cell(xx, yy);
                if !a.cells_equal(&b) {
                    return false;
                }
            }
        }
        true
    }

    // --- Text extraction. ---

    /// Convert a run of cells to text (C `grid_string_cells`, plain form:
    /// no SGR sequences). Padding cells are skipped, tabs read as `\t`.
    pub fn string_cells(&self, px: u32, py: u32, nx: u32, opts: StringOpts) -> String {
        let mut out = String::new();
        if !self.check_y(py) {
            return out;
        }
        let line = &self.lines[py as usize];
        let end = if opts.empty_cells {
            line.cells.len() as u32
        } else {
            line.used
        };
        for xx in px..px.saturating_add(nx) {
            if xx >= end {
                break;
            }
            let gc = self.get_cell(xx, py);
            if gc.flags & flags::PADDING != 0 {
                continue;
            }
            if gc.flags & flags::TAB != 0 {
                out.push('\t');
            } else {
                out.push_str(&String::from_utf8_lossy(gc.data.bytes()));
            }
        }
        if opts.trim_spaces {
            let trimmed = out.trim_end_matches(' ').len();
            out.truncate(trimmed);
        }
        out
    }

    /// Visible length of a line: cells up to the last non-space
    /// (C `grid_line_length`).
    pub fn line_length(&self, py: u32) -> u32 {
        let mut px = self.line_cellsize(py).min(self.sx);
        while px > 0 {
            let gc = self.get_cell(px - 1, py);
            if gc.flags & flags::PADDING != 0 || gc.data.size != 1 || gc.data.bytes() != b" " {
                break;
            }
            px -= 1;
        }
        px
    }

    // --- Reflow (resize to a new width). ---

    /// Number of used cells in a line (C `gl->cellused`).
    fn line_used(&self, py: u32) -> u32 {
        self.lines[py as usize].used
    }

    /// Collect the non-padding cells of a line (C reads via grid_get_cell1
    /// and re-sets; padding is regenerated on write).
    fn take_line_cells(&self, py: u32) -> Vec<GridCell> {
        let used = self.line_used(py);
        (0..used)
            .map(|i| self.get_cell(i, py))
            .filter(|gc| gc.flags & flags::PADDING == 0)
            .collect()
    }

    /// Append a cell (plus padding for wide cells) to a target row.
    fn push_cell(&mut self, at: &mut u32, py: u32, gc: &GridCell) {
        self.set_cell(*at, py, gc);
        *at += 1;
        for _ in 1..gc.data.width.max(1) {
            self.set_padding(*at, py);
            *at += 1;
        }
    }

    /// Reflow the whole grid to width `sx` (C `grid_reflow`): too-long lines
    /// split (gaining WRAPPED), previously wrapped lines join when they fit.
    /// `sy` is unchanged; `hsize` absorbs the difference. Bumps
    /// `scroll_generation`.
    pub fn reflow(&mut self, sx: u32) {
        let sy = self.sy;
        let old_rows = self.rows();

        // Gather logical lines: runs of WRAPPED rows plus their terminator.
        // Cells are values, so this stage cannot alias storage (the C
        // implementation moves celldata pointers between grids here).
        let mut logical: Vec<(Vec<GridCell>, bool)> = Vec::new();
        let mut current: Vec<GridCell> = Vec::new();
        for py in 0..old_rows {
            let mut cells = self.take_line_cells(py);
            let wrapped = self.lines[py as usize].flags & line_flags::WRAPPED != 0;
            current.append(&mut cells);
            if !wrapped {
                let trailing_wrap = false;
                logical.push((std::mem::take(&mut current), trailing_wrap));
            }
        }
        if !current.is_empty() {
            logical.push((current, true));
        }

        // Re-lay each logical line at the new width.
        let mut target: Vec<(Vec<GridCell>, bool)> = Vec::new(); // (cells,wrapped)
        for (cells, trailing_wrap) in logical {
            if cells.is_empty() {
                target.push((Vec::new(), false));
                continue;
            }
            let mut row: Vec<GridCell> = Vec::new();
            let mut width = 0u32;
            for gc in cells {
                let w = gc.data.width.max(1) as u32;
                if width + w > sx && !row.is_empty() {
                    target.push((std::mem::take(&mut row), true));
                    width = 0;
                }
                width += w;
                row.push(gc);
            }
            target.push((row, trailing_wrap));
        }

        // Rebuild the grid: new rows, sy preserved, history absorbs extra.
        let new_rows = (target.len() as u32).max(sy);
        self.sx = sx;
        self.lines = (0..new_rows).map(|_| Line::default()).collect();
        self.hsize = new_rows - sy;
        for (py, (cells, wrapped)) in target.into_iter().enumerate() {
            let mut at = 0u32;
            for gc in &cells {
                self.push_cell(&mut at, py as u32, gc);
            }
            if wrapped {
                self.lines[py].flags |= line_flags::WRAPPED;
            }
        }
        if self.hscrolled > self.hsize {
            self.hscrolled = self.hsize;
        }
        self.scroll_generation += 1;
    }

    /// Convert an absolute position to a wrapped (logical-line) position
    /// (C `grid_wrap_position`). `u32::MAX` for x means past end of line.
    pub fn wrap_position(&self, px: u32, py: u32) -> (u32, u32) {
        let mut ax = 0u32;
        let mut ay = 0u32;
        for yy in 0..py {
            if self.lines[yy as usize].flags & line_flags::WRAPPED != 0 {
                ax += self.line_used(yy);
            } else {
                ax = 0;
                ay += 1;
            }
        }
        if px >= self.line_used(py) {
            ax = u32::MAX;
        } else {
            ax += px;
        }
        (ax, ay)
    }

    /// Convert a wrapped (logical-line) position back to absolute
    /// (C `grid_unwrap_position`).
    pub fn unwrap_position(&self, wx: u32, wy: u32) -> (u32, u32) {
        let mut yy = 0u32;
        let mut ay = 0u32;
        while yy < self.rows() - 1 {
            if ay == wy {
                break;
            }
            if self.lines[yy as usize].flags & line_flags::WRAPPED == 0 {
                ay += 1;
            }
            yy += 1;
        }
        let mut wx = wx;
        if wx == u32::MAX {
            while self.lines[yy as usize].flags & line_flags::WRAPPED != 0 {
                yy += 1;
            }
            wx = self.line_used(yy);
        } else {
            while self.lines[yy as usize].flags & line_flags::WRAPPED != 0 {
                if wx < self.line_used(yy) {
                    break;
                }
                wx -= self.line_used(yy);
                yy += 1;
            }
        }
        (wx, yy)
    }
}

/// Options for [`Grid::string_cells`] (subset of C GRID_STRING_* flags; SGR
/// sequence emission needs terminal theme state and lives outside the core).
#[derive(Clone, Copy, Default)]
pub struct StringOpts {
    /// Include allocated-but-unwritten cells (C GRID_STRING_EMPTY_CELLS).
    pub empty_cells: bool,
    /// Drop trailing spaces (C GRID_STRING_TRIM_SPACES).
    pub trim_spaces: bool,
}
