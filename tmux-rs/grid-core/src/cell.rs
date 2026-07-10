//! Cell value types, mirroring the observable fields of tmux's
//! `struct grid_cell` / `struct utf8_data`.

/// Maximum bytes of one (possibly combined) UTF-8 character (tmux UTF8_SIZE).
pub const UTF8_SIZE: usize = 32;

/// An expanded UTF-8 character (tmux `struct utf8_data`).
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct Utf8Data {
    pub data: [u8; UTF8_SIZE],
    pub have: u8,
    pub size: u8,
    /// Display width in columns; 0xff if invalid.
    pub width: u8,
}

impl Utf8Data {
    /// Build from a single complete UTF-8 character string and its width.
    ///
    /// # Panics
    /// Panics if `s` is longer than `UTF8_SIZE` bytes (caller bug; matches
    /// the C side where oversized input is rejected before storage).
    pub fn from_str(s: &str, width: u8) -> Self {
        let bytes = s.as_bytes();
        assert!(bytes.len() <= UTF8_SIZE, "utf8 data too long");
        let mut data = [0u8; UTF8_SIZE];
        data[..bytes.len()].copy_from_slice(bytes);
        Self {
            data,
            have: bytes.len() as u8,
            size: bytes.len() as u8,
            width,
        }
    }

    /// The stored bytes as a slice.
    pub fn bytes(&self) -> &[u8] {
        &self.data[..self.size as usize]
    }
}

impl std::fmt::Debug for Utf8Data {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "Utf8Data({:?}, size={}, width={})",
            String::from_utf8_lossy(self.bytes()),
            self.size,
            self.width
        )
    }
}

/// A grid cell as seen by callers (tmux `struct grid_cell`).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct GridCell {
    pub data: Utf8Data,
    pub attr: u16,
    pub flags: u8,
    pub fg: i32,
    pub bg: i32,
    pub us: i32,
    pub link: u32,
}

impl GridCell {
    /// The default cell: a plain space, default colours (tmux
    /// `grid_default_cell`).
    pub fn default_cell() -> Self {
        Self {
            data: Utf8Data::from_str(" ", 1),
            attr: 0,
            flags: 0,
            fg: 8,
            bg: 8,
            us: 8,
            link: 0,
        }
    }

    /// The padding cell stored after a wide character (tmux
    /// `grid_padding_cell`): zero width, PADDING flag.
    pub fn padding_cell() -> Self {
        let mut data = Utf8Data::from_str("!", 0);
        data.have = 0;
        data.size = 0;
        Self {
            data,
            attr: 0,
            flags: crate::flags::PADDING,
            fg: 8,
            bg: 8,
            us: 8,
            link: 0,
        }
    }

    /// Turn this cell into a tab of `width` columns (tmux `grid_set_tab`):
    /// TAB flag set, data becomes `width` spaces, clamped to the buffer.
    pub fn set_tab(&mut self, width: u32) {
        let w = (width as usize).min(UTF8_SIZE);
        self.flags |= crate::flags::TAB;
        self.flags &= !crate::flags::PADDING;
        self.data.data = [0u8; UTF8_SIZE];
        self.data.data[..w].fill(b' ');
        self.data.have = w as u8;
        self.data.size = w as u8;
        self.data.width = w as u8;
    }

    /// Visible style equality (tmux `grid_cells_look_equal`): everything but
    /// the character data, ignoring CLEARED.
    pub fn looks_equal(&self, other: &Self) -> bool {
        self.fg == other.fg
            && self.bg == other.bg
            && self.attr == other.attr
            && (self.flags & !crate::flags::CLEARED) == (other.flags & !crate::flags::CLEARED)
            && self.link == other.link
    }

    /// Full equality including character data (tmux `grid_cells_equal`).
    pub fn cells_equal(&self, other: &Self) -> bool {
        self.looks_equal(other)
            && self.data.width == other.data.width
            && self.data.bytes() == other.data.bytes()
    }
}
