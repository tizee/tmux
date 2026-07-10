/* $OpenBSD$ */

/*
 * Copyright (c) 2026 tizee <tizee@github.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * Rust grid engine shim (ENABLE_RUST_GRID): implements the grid_* storage
 * API by delegating to the safe-Rust grid-core via its C ABI (rust_grid_*),
 * and keeps the pure value functions (equality, SGR string conversion) as C
 * copies from grid.c operating only through the public accessors.
 * See docs/grid-core-rust.md.
 */

/* ---- Rust FFI (grid-core-ffi); struct grid is layout-compatible. ---- */

struct rust_grid_cell;

struct grid	*rust_grid_create(u_int, u_int, u_int);
void	 rust_grid_destroy(struct grid *);
void	 rust_grid_get_cell(const struct grid *, u_int, u_int,
	    struct grid_cell *);
void	 rust_grid_set_cell(struct grid *, u_int, u_int,
	    const struct grid_cell *);
void	 rust_grid_set_padding(struct grid *, u_int, u_int);
void	 rust_grid_set_cells(struct grid *, u_int, u_int,
	    const struct grid_cell *, const char *, size_t);
void	 rust_grid_clear(struct grid *, u_int, u_int, u_int, u_int, u_int);
void	 rust_grid_clear_lines(struct grid *, u_int, u_int, u_int);
void	 rust_grid_move_lines(struct grid *, u_int, u_int, u_int, u_int);
void	 rust_grid_move_cells(struct grid *, u_int, u_int, u_int, u_int,
	    u_int);
void	 rust_grid_scroll_history(struct grid *, u_int);
void	 rust_grid_scroll_history_region(struct grid *, u_int, u_int, u_int);
void	 rust_grid_collect_history(struct grid *, int);
void	 rust_grid_remove_history(struct grid *, u_int);
void	 rust_grid_clear_history(struct grid *);
void	 rust_grid_duplicate_lines(struct grid *, u_int,
	    const struct grid *, u_int, u_int);
void	 rust_grid_reflow(struct grid *, u_int);
int	 rust_grid_compare(const struct grid *, const struct grid *);
void	 rust_grid_wrap_position(const struct grid *, u_int, u_int, u_int *,
	    u_int *);
void	 rust_grid_unwrap_position(const struct grid *, u_int *, u_int *,
	    u_int, u_int);
u_int	 rust_grid_line_length(const struct grid *, u_int);
int	 rust_grid_line_flags(const struct grid *, u_int);
void	 rust_grid_set_line_flag(struct grid *, u_int, int, int);
u_int	 rust_grid_line_cellsize(const struct grid *, u_int);
void	 rust_grid_adjust_lines(struct grid *, u_int);
void	 rust_grid_empty_line(struct grid *, u_int, u_int);
void	 rust_grid_set_hscrolled(struct grid *, u_int);
void	 rust_grid_sync_history(struct grid *, const struct grid *, u_int,
	    u_int);
void	 rust_grid_storage(const struct grid *, u_int *, u_int *, u_int *,
	    size_t *, size_t *, size_t *);

/* Not in the Rust FFI yet: cellused/time metadata accessors. */
u_int	 rust_grid_line_cellused(const struct grid *, u_int);
time_t	 rust_grid_line_time(const struct grid *, u_int);

/* Default grid cell data (same value as grid.c). */
const struct grid_cell grid_default_cell = {
	{ { ' ' }, 0, 1, 1 }, 0, 0, 8, 8, 8, 0
};

/*
 * Padding grid cell data (identified by the PADDING flag; the character
 * content is unspecified, see docs/grid-core-rust.md).
 */
static const struct grid_cell grid_padding_cell = {
	{ { '!' }, 0, 0, 0 }, 0, GRID_FLAG_PADDING, 8, 8, 8, 0
};

/* ---- Delegating storage wrappers. ---- */

struct grid *
grid_create(u_int sx, u_int sy, u_int hlimit)
{
	return (rust_grid_create(sx, sy, hlimit));
}

void
grid_destroy(struct grid *gd)
{
	rust_grid_destroy(gd);
}

void
grid_get_cell(struct grid *gd, u_int px, u_int py, struct grid_cell *gc)
{
	rust_grid_get_cell(gd, px, py, gc);
}

void
grid_set_cell(struct grid *gd, u_int px, u_int py,
    const struct grid_cell *gc)
{
	rust_grid_set_cell(gd, px, py, gc);
}

void
grid_set_padding(struct grid *gd, u_int px, u_int py)
{
	grid_set_cell(gd, px, py, &grid_padding_cell);
}

void
grid_set_cells(struct grid *gd, u_int px, u_int py,
    const struct grid_cell *gc, const char *s, size_t slen)
{
	rust_grid_set_cells(gd, px, py, gc, s, slen);
}

void
grid_clear(struct grid *gd, u_int px, u_int py, u_int nx, u_int ny, u_int bg)
{
	rust_grid_clear(gd, px, py, nx, ny, bg);
}

void
grid_clear_lines(struct grid *gd, u_int py, u_int ny, u_int bg)
{
	rust_grid_clear_lines(gd, py, ny, bg);
}

void
grid_move_lines(struct grid *gd, u_int dy, u_int py, u_int ny, u_int bg)
{
	rust_grid_move_lines(gd, dy, py, ny, bg);
}

void
grid_move_cells(struct grid *gd, u_int dx, u_int px, u_int py, u_int nx,
    u_int bg)
{
	rust_grid_move_cells(gd, dx, px, py, nx, bg);
}

void
grid_scroll_history(struct grid *gd, u_int bg)
{
	rust_grid_scroll_history(gd, bg);
}

void
grid_scroll_history_region(struct grid *gd, u_int upper, u_int lower,
    u_int bg)
{
	rust_grid_scroll_history_region(gd, upper, lower, bg);
}

void
grid_collect_history(struct grid *gd, int all)
{
	rust_grid_collect_history(gd, all);
}

void
grid_remove_history(struct grid *gd, u_int ny)
{
	rust_grid_remove_history(gd, ny);
}

void
grid_clear_history(struct grid *gd)
{
	rust_grid_clear_history(gd);
}

void
grid_duplicate_lines(struct grid *dst, u_int dy, struct grid *src, u_int sy,
    u_int ny)
{
	rust_grid_duplicate_lines(dst, dy, src, sy, ny);
}

void
grid_sync_history(struct grid *dg, struct grid *sg, u_int added,
    u_int collected)
{
	rust_grid_sync_history(dg, sg, added, collected);
}

void
grid_reflow(struct grid *gd, u_int sx)
{
	rust_grid_reflow(gd, sx);
}

int
grid_compare(struct grid *ga, struct grid *gb)
{
	return (rust_grid_compare(ga, gb));
}

void
grid_wrap_position(struct grid *gd, u_int px, u_int py, u_int *wx, u_int *wy)
{
	rust_grid_wrap_position(gd, px, py, wx, wy);
}

void
grid_unwrap_position(struct grid *gd, u_int *px, u_int *py, u_int wx,
    u_int wy)
{
	rust_grid_unwrap_position(gd, px, py, wx, wy);
}

u_int
grid_line_length(struct grid *gd, u_int py)
{
	return (rust_grid_line_length(gd, py));
}

int
grid_line_flags(struct grid *gd, u_int py)
{
	return (rust_grid_line_flags(gd, py));
}

void
grid_line_set_flag(struct grid *gd, u_int py, int flag, int on)
{
	rust_grid_set_line_flag(gd, py, flag, on);
}

u_int
grid_line_cellsize(struct grid *gd, u_int py)
{
	return (rust_grid_line_cellsize(gd, py));
}

u_int
grid_line_cellused(struct grid *gd, u_int py)
{
	return (rust_grid_line_cellused(gd, py));
}

time_t
grid_line_time(struct grid *gd, u_int py)
{
	return (rust_grid_line_time(gd, py));
}

void
grid_adjust_lines(struct grid *gd, u_int lines)
{
	rust_grid_adjust_lines(gd, lines);
}

void
grid_empty_line(struct grid *gd, u_int py, u_int bg)
{
	rust_grid_empty_line(gd, py, bg);
}

void
grid_set_hscrolled(struct grid *gd, u_int hscrolled)
{
	rust_grid_set_hscrolled(gd, hscrolled);
}

void
grid_storage(struct grid *gd, u_int *lines, u_int *cells, u_int *extd,
    size_t *linebytes, size_t *cellbytes, size_t *extdbytes)
{
	rust_grid_storage(gd, lines, cells, extd, linebytes, cellbytes,
	    extdbytes);
}

/* ---- Pure value functions, copied from grid.c (accessor-only). ---- */

/* Check if two styles are (visibly) the same. */
int
grid_cells_look_equal(const struct grid_cell *gc1, const struct grid_cell *gc2)
{
	int flags1 = gc1->flags, flags2 = gc2->flags;

	if (gc1->fg != gc2->fg || gc1->bg != gc2->bg)
		return (0);
	if (gc1->attr != gc2->attr)
		return (0);
	if ((flags1 & ~GRID_FLAG_CLEARED) != (flags2 & ~GRID_FLAG_CLEARED))
		return (0);
	if (gc1->link != gc2->link)
		return (0);
	return (1);
}

/* Compare grid cells. Return 1 if equal, 0 if not. */
int
grid_cells_equal(const struct grid_cell *gc1, const struct grid_cell *gc2)
{
	if (!grid_cells_look_equal(gc1, gc2))
		return (0);
	if (gc1->data.width != gc2->data.width)
		return (0);
	if (gc1->data.size != gc2->data.size)
		return (0);
	return (memcmp(gc1->data.data, gc2->data.data, gc1->data.size) == 0);
}

/* Set grid cell to a tab. */
void
grid_set_tab(struct grid_cell *gc, u_int width)
{
	/*
	 * Bound the width to the fixed cell data buffer. A tab never legitimately
	 * spans more columns than a cell can hold, and only one caller (the HT
	 * handler in input.c) guards this; clamp here so no caller — including the
	 * extended-cell read-back path in grid_get_cell — can overflow data.data.
	 */
	if (width > sizeof gc->data.data)
		width = sizeof gc->data.data;

	memset(gc->data.data, 0, sizeof gc->data.data);
	gc->flags |= GRID_FLAG_TAB;
	gc->flags &= ~GRID_FLAG_PADDING;
	gc->data.width = gc->data.size = gc->data.have = width;
	memset(gc->data.data, ' ', gc->data.size);
}

/* Get ANSI foreground sequence. */
static size_t
grid_string_cells_fg(const struct grid_cell *gc, int *values)
{
	size_t	n;
	u_char	r, g, b;
	int	c;

	n = 0;
	if (gc->fg & COLOUR_FLAG_THEME) {
		c = colour_theme_terminal_colour(gc->fg & 0xff);
		if (c == 8)
			values[n++] = 39;
		else
			values[n++] = c + 30;
	} else if (gc->fg & COLOUR_FLAG_256) {
		values[n++] = 38;
		values[n++] = 5;
		values[n++] = gc->fg & 0xff;
	} else if (gc->fg & COLOUR_FLAG_RGB) {
		values[n++] = 38;
		values[n++] = 2;
		colour_split_rgb(gc->fg, &r, &g, &b);
		values[n++] = r;
		values[n++] = g;
		values[n++] = b;
	} else {
		switch (gc->fg) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			values[n++] = gc->fg + 30;
			break;
		case 8:
			values[n++] = 39;
			break;
		case 90:
		case 91:
		case 92:
		case 93:
		case 94:
		case 95:
		case 96:
		case 97:
			values[n++] = gc->fg;
			break;
		}
	}
	return (n);
}

/* Get ANSI background sequence. */
static size_t
grid_string_cells_bg(const struct grid_cell *gc, int *values)
{
	size_t	n;
	u_char	r, g, b;
	int	c;

	n = 0;
	if (gc->bg & COLOUR_FLAG_THEME) {
		c = colour_theme_terminal_colour(gc->bg & 0xff);
		if (c == 8)
			values[n++] = 49;
		else
			values[n++] = c + 40;
	} else if (gc->bg & COLOUR_FLAG_256) {
		values[n++] = 48;
		values[n++] = 5;
		values[n++] = gc->bg & 0xff;
	} else if (gc->bg & COLOUR_FLAG_RGB) {
		values[n++] = 48;
		values[n++] = 2;
		colour_split_rgb(gc->bg, &r, &g, &b);
		values[n++] = r;
		values[n++] = g;
		values[n++] = b;
	} else {
		switch (gc->bg) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			values[n++] = gc->bg + 40;
			break;
		case 8:
			values[n++] = 49;
			break;
		case 90:
		case 91:
		case 92:
		case 93:
		case 94:
		case 95:
		case 96:
		case 97:
			values[n++] = gc->bg + 10;
			break;
		}
	}
	return (n);
}

/* Get underscore colour sequence. */
static size_t
grid_string_cells_us(const struct grid_cell *gc, int *values)
{
	size_t	n;
	u_char	r, g, b;
	int	c;

	n = 0;
	if (gc->us & COLOUR_FLAG_THEME) {
		c = colour_theme_terminal_colour(gc->us & 0xff);
		if (c == 8)
			values[n++] = 59;
		else {
			values[n++] = 58;
			values[n++] = 5;
			values[n++] = c;
		}
	} else if (gc->us & COLOUR_FLAG_256) {
		values[n++] = 58;
		values[n++] = 5;
		values[n++] = gc->us & 0xff;
	} else if (gc->us & COLOUR_FLAG_RGB) {
		values[n++] = 58;
		values[n++] = 2;
		colour_split_rgb(gc->us, &r, &g, &b);
		values[n++] = r;
		values[n++] = g;
		values[n++] = b;
	}
	return (n);
}

/* Add on SGR code. */
static void
grid_string_cells_add_code(char *buf, size_t len, u_int n, int *s, int *newc,
    int *oldc, size_t nnewc, size_t noldc, int flags)
{
	u_int	i;
	char	tmp[64];
	int	reset = (n != 0 && s[0] == 0);

	if (nnewc == 0)
		return; /* no code to add */
	if (!reset &&
	    nnewc == noldc &&
	    memcmp(newc, oldc, nnewc * sizeof newc[0]) == 0)
		return; /* no reset and colour unchanged */
	if (reset && (newc[0] == 49 || newc[0] == 39))
		return; /* reset and colour default */

	if (flags & GRID_STRING_ESCAPE_SEQUENCES)
		strlcat(buf, "\\033[", len);
	else
		strlcat(buf, "\033[", len);
	for (i = 0; i < nnewc; i++) {
		if (i + 1 < nnewc)
			xsnprintf(tmp, sizeof tmp, "%d;", newc[i]);
		else
			xsnprintf(tmp, sizeof tmp, "%d", newc[i]);
		strlcat(buf, tmp, len);
	}
	strlcat(buf, "m", len);
}

static int
grid_string_cells_add_hyperlink(char *buf, size_t len, const char *id,
    const char *uri, int flags)
{
	char	*tmp;

	if (strlen(uri) + strlen(id) + 17 >= len)
		return (0);

	if (flags & GRID_STRING_ESCAPE_SEQUENCES)
		strlcat(buf, "\\033]8;", len);
	else
		strlcat(buf, "\033]8;", len);
	if (*id != '\0') {
		xasprintf(&tmp, "id=%s;", id);
		strlcat(buf, tmp, len);
		free(tmp);
	} else
		strlcat(buf, ";", len);
	strlcat(buf, uri, len);
	if (flags & GRID_STRING_ESCAPE_SEQUENCES)
		strlcat(buf, "\\033\\\\", len);
	else
		strlcat(buf, "\033\\", len);
	return (1);
}

/*
 * Returns ANSI code to set particular attributes (colour, bold and so on)
 * given a current state.
 */
static void
grid_string_cells_code(const struct grid_cell *lastgc,
    const struct grid_cell *gc, char *buf, size_t len, int flags,
    struct screen *sc, int *has_link)
{
	int			 oldc[64], newc[64], s[128];
	size_t			 noldc, nnewc, n, i;
	u_int			 attr = gc->attr, lastattr = lastgc->attr;
	char			 tmp[64];
	const char		*uri, *id;

	static const struct {
		u_int	mask;
		u_int	code;
	} attrs[] = {
		{ GRID_ATTR_BRIGHT, 1 },
		{ GRID_ATTR_DIM, 2 },
		{ GRID_ATTR_ITALICS, 3 },
		{ GRID_ATTR_UNDERSCORE, 4 },
		{ GRID_ATTR_BLINK, 5 },
		{ GRID_ATTR_REVERSE, 7 },
		{ GRID_ATTR_HIDDEN, 8 },
		{ GRID_ATTR_STRIKETHROUGH, 9 },
		{ GRID_ATTR_UNDERSCORE_2, 42 },
		{ GRID_ATTR_UNDERSCORE_3, 43 },
		{ GRID_ATTR_UNDERSCORE_4, 44 },
		{ GRID_ATTR_UNDERSCORE_5, 45 },
		{ GRID_ATTR_OVERLINE, 53 },
	};
	n = 0;

	/* If any attribute is removed, begin with 0. */
	for (i = 0; i < nitems(attrs); i++) {
		if (((~attr & attrs[i].mask) &&
		    (lastattr & attrs[i].mask)) ||
		    (lastgc->us != 8 && gc->us == 8)) {
			s[n++] = 0;
			lastattr &= GRID_ATTR_CHARSET;
			break;
		}
	}
	/* For each attribute that is newly set, add its code. */
	for (i = 0; i < nitems(attrs); i++) {
		if ((attr & attrs[i].mask) && !(lastattr & attrs[i].mask))
			s[n++] = attrs[i].code;
	}

	/* Write the attributes. */
	*buf = '\0';
	if (n > 0) {
		if (flags & GRID_STRING_ESCAPE_SEQUENCES)
			strlcat(buf, "\\033[", len);
		else
			strlcat(buf, "\033[", len);
		for (i = 0; i < n; i++) {
			if (s[i] < 10)
				xsnprintf(tmp, sizeof tmp, "%d", s[i]);
			else {
				xsnprintf(tmp, sizeof tmp, "%d:%d", s[i] / 10,
				    s[i] % 10);
			}
			strlcat(buf, tmp, len);
			if (i + 1 < n)
				strlcat(buf, ";", len);
		}
		strlcat(buf, "m", len);
	}

	/* If the foreground colour changed, write its parameters. */
	nnewc = grid_string_cells_fg(gc, newc);
	noldc = grid_string_cells_fg(lastgc, oldc);
	grid_string_cells_add_code(buf, len, n, s, newc, oldc, nnewc, noldc,
	    flags);

	/* If the background colour changed, append its parameters. */
	nnewc = grid_string_cells_bg(gc, newc);
	noldc = grid_string_cells_bg(lastgc, oldc);
	grid_string_cells_add_code(buf, len, n, s, newc, oldc, nnewc, noldc,
	    flags);

	/* If the underscore colour changed, append its parameters. */
	nnewc = grid_string_cells_us(gc, newc);
	noldc = grid_string_cells_us(lastgc, oldc);
	grid_string_cells_add_code(buf, len, n, s, newc, oldc, nnewc, noldc,
	    flags);

	/* Append shift in/shift out if needed. */
	if ((attr & GRID_ATTR_CHARSET) && !(lastattr & GRID_ATTR_CHARSET)) {
		if (flags & GRID_STRING_ESCAPE_SEQUENCES)
			strlcat(buf, "\\016", len); /* SO */
		else
			strlcat(buf, "\016", len);  /* SO */
	}
	if (!(attr & GRID_ATTR_CHARSET) && (lastattr & GRID_ATTR_CHARSET)) {
		if (flags & GRID_STRING_ESCAPE_SEQUENCES)
			strlcat(buf, "\\017", len); /* SI */
		else
			strlcat(buf, "\017", len);  /* SI */
	}

	/* Add hyperlink if changed. */
	if (sc != NULL && sc->hyperlinks != NULL && lastgc->link != gc->link) {
		if (hyperlinks_get(sc->hyperlinks, gc->link, &uri, &id, NULL)) {
			*has_link = grid_string_cells_add_hyperlink(buf, len,
			    id, uri, flags);
		} else if (*has_link) {
			grid_string_cells_add_hyperlink(buf, len, "", "",
			    flags);
			*has_link = 0;
		}
	}
}

/* Convert cells into a string. */
char *
grid_string_cells(struct grid *gd, u_int px, u_int py, u_int nx,
    struct grid_cell **lastgc, int flags, struct screen *s)
{
	struct grid_cell	 gc;
	static struct grid_cell	 lastgc1;
	const char		*data;
	char			*buf, code[8192];
	size_t			 len, off, size, codelen;
	u_int			 xx, end;
	int			 has_link = 0;

	if (lastgc != NULL && *lastgc == NULL) {
		memcpy(&lastgc1, &grid_default_cell, sizeof lastgc1);
		*lastgc = &lastgc1;
	}

	len = 128;
	buf = xmalloc(len);
	off = 0;

	if (py >= gd->hsize + gd->sy) {
		buf[0] = '\0';
		return (buf);
	}
	if (flags & GRID_STRING_EMPTY_CELLS)
		end = grid_line_cellsize(gd, py);
	else
		end = grid_line_cellused(gd, py);
	for (xx = px; xx < px + nx; xx++) {
		if (xx >= end)
			break;
		grid_get_cell(gd, xx, py, &gc);
		if (gc.flags & GRID_FLAG_PADDING)
			continue;

		if (lastgc != NULL && (flags & GRID_STRING_WITH_SEQUENCES)) {
			grid_string_cells_code(*lastgc, &gc, code, sizeof code,
			    flags, s, &has_link);
			codelen = strlen(code);
			memcpy(*lastgc, &gc, sizeof **lastgc);
		} else
			codelen = 0;

		if (gc.flags & GRID_FLAG_TAB) {
			data = "\t";
			size = 1;
		} else {
			data = gc.data.data;
			size = gc.data.size;
			if ((flags & GRID_STRING_ESCAPE_SEQUENCES) &&
			    size == 1 &&
			    *data == '\\') {
				data = "\\\\";
				size = 2;
			}
		}

		while (len < off + size + codelen + 1) {
			buf = xreallocarray(buf, 2, len);
			len *= 2;
		}

		if (codelen != 0) {
			memcpy(buf + off, code, codelen);
			off += codelen;
		}
		memcpy(buf + off, data, size);
		off += size;
	}

	if (has_link) {
		grid_string_cells_add_hyperlink(code, sizeof code, "", "",
		    flags);
		codelen = strlen(code);
		while (len < off + size + codelen + 1) {
			buf = xreallocarray(buf, 2, len);
			len *= 2;
		}
		memcpy(buf + off, code, codelen);
		off += codelen;
	}

	if (flags & GRID_STRING_TRIM_SPACES) {
		while (off > 0 && buf[off - 1] == ' ')
			off--;
	}
	buf[off] = '\0';

	return (buf);
}

/* Check if character is in set. */
int
grid_in_set(struct grid *gd, u_int px, u_int py, const char *set)
{
	struct grid_cell	gc, tmp_gc;
	u_int			pxx;

	grid_get_cell(gd, px, py, &gc);
	if (strchr(set, '\t')) {
		if (gc.flags & GRID_FLAG_PADDING) {
			pxx = px;
			do
				grid_get_cell(gd, --pxx, py, &tmp_gc);
			while (pxx > 0 && tmp_gc.flags & GRID_FLAG_PADDING);
			if (tmp_gc.flags & GRID_FLAG_TAB)
				return (tmp_gc.data.width - (px - pxx));
		} else if (gc.flags & GRID_FLAG_TAB)
			return (gc.data.width);
	}
	if (gc.flags & GRID_FLAG_PADDING)
		return (0);
	return (utf8_cstrhas(set, &gc.data));
}
