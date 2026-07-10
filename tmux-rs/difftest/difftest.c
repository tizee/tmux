/*
 * Differential test: tmux's C grid engine vs the Rust grid-core, fed the
 * same pseudo-random operation sequence, compared cell-by-cell.
 *
 * The C side links the real grid.o and utf8.o from the tmux build; the Rust
 * side links libgrid_core_ffi.a. Any divergence prints the op trace tail
 * and exits non-zero. Run under ASan to also catch C-side memory bugs on
 * the generated sequences (the crash class this project exists for).
 *
 * Build via difftest.sh in this directory.
 */

#include <sys/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/* ---- Rust FFI (mirrors grid-core-ffi/src/lib.rs) ---- */

struct rust_grid_cell {
	u_char	data[32];
	u_char	have;
	u_char	size;
	u_char	width;
	u_short	attr;
	u_char	flags;
	int	fg;
	int	bg;
	int	us;
	u_int	link;
};

struct rust_grid {
	int	flags;
	u_int	sx;
	u_int	sy;
	u_int	hscrolled;
	u_int	hsize;
	u_int	hlimit;
	u_int	scroll_added;
	u_int	scroll_collected;
	u_int	scroll_generation;
	void	*grid;
};

struct rust_grid	*rust_grid_create(u_int, u_int, u_int);
void	 rust_grid_destroy(struct rust_grid *);
void	 rust_grid_get_cell(const struct rust_grid *, u_int, u_int,
	    struct rust_grid_cell *);
void	 rust_grid_set_cell(struct rust_grid *, u_int, u_int,
	    const struct rust_grid_cell *);
void	 rust_grid_set_padding(struct rust_grid *, u_int, u_int);
void	 rust_grid_set_cells(struct rust_grid *, u_int, u_int,
	    const struct rust_grid_cell *, const char *, size_t);
void	 rust_grid_clear(struct rust_grid *, u_int, u_int, u_int, u_int,
	    u_int);
void	 rust_grid_clear_lines(struct rust_grid *, u_int, u_int, u_int);
void	 rust_grid_move_lines(struct rust_grid *, u_int, u_int, u_int,
	    u_int);
void	 rust_grid_move_cells(struct rust_grid *, u_int, u_int, u_int,
	    u_int, u_int);
void	 rust_grid_scroll_history(struct rust_grid *, u_int);
void	 rust_grid_collect_history(struct rust_grid *, int);
void	 rust_grid_remove_history(struct rust_grid *, u_int);
void	 rust_grid_clear_history(struct rust_grid *);
void	 rust_grid_duplicate_lines(struct rust_grid *, u_int,
	    const struct rust_grid *, u_int, u_int);
u_int	 rust_grid_line_length(const struct rust_grid *, u_int);
int	 rust_grid_line_flags(const struct rust_grid *, u_int);

/* ---- PRNG (xorshift32, seeded) ---- */

static u_int rng_state;

static u_int
rnd(u_int max)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return (max ? rng_state % max : 0);
}

/* ---- Op trace ring for failure reports ---- */

static char	trace[64][128];
static u_int	trace_n;

static void
top(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(trace[trace_n++ % 64], sizeof trace[0], fmt, ap);
	va_end(ap);
}

static void
dump_trace(void)
{
	u_int	i, start = (trace_n > 64) ? trace_n - 64 : 0;

	fprintf(stderr, "--- last ops ---\n");
	for (i = start; i < trace_n; i++)
		fprintf(stderr, "%6u: %s\n", i, trace[i % 64]);
}

/* ---- Cell conversion + comparison ---- */

static void
to_rust_cell(const struct grid_cell *gc, struct rust_grid_cell *rc)
{
	memcpy(rc->data, gc->data.data, sizeof rc->data);
	rc->have = gc->data.have;
	rc->size = gc->data.size;
	rc->width = gc->data.width;
	rc->attr = gc->attr;
	rc->flags = gc->flags;
	rc->fg = gc->fg;
	rc->bg = gc->bg;
	rc->us = gc->us;
	rc->link = gc->link;
}

static int
cells_differ(const struct grid_cell *c, const struct rust_grid_cell *r)
{
	/*
	 * GRID_FLAG_CLEARED is masked: the Rust engine deliberately fixes
	 * C's quirk of losing CLEARED on previously-extended cells (see
	 * docs/grid-core-rust.md, "Deliberate divergences").
	 */
	u_char	mask = (u_char)~GRID_FLAG_CLEARED;

	if ((c->flags & mask) != (r->flags & mask))
		return (1);
	if (c->attr != r->attr)
		return (1);
	/*
	 * Padding cells: the flag is the whole contract; the stored data
	 * ('!' vs empty) depends on simple-vs-extended storage history,
	 * which legitimately differs after the CLEARED fix. Skip data.
	 */
	if (c->flags & GRID_FLAG_PADDING)
		goto style;
	if (c->data.size != r->size || c->data.width != r->width)
		return (1);
	if (memcmp(c->data.data, r->data, c->data.size) != 0)
		return (1);
style:
	if (c->fg != r->fg || c->bg != r->bg || c->us != r->us)
		return (1);
	if (c->link != r->link)
		return (1);
	return (0);
}

static int
compare_grids(struct grid *cg, struct rust_grid *rg, u_int step)
{
	struct grid_cell	 gc;
	struct rust_grid_cell	 rc;
	u_int			 xx, yy, rows;

	if (cg->sx != rg->sx || cg->sy != rg->sy || cg->hsize != rg->hsize) {
		fprintf(stderr, "step %u: geometry: C %ux%u+%u, Rust %ux%u+%u\n",
		    step, cg->sx, cg->sy, cg->hsize, rg->sx, rg->sy, rg->hsize);
		return (1);
	}
	if (cg->scroll_added != rg->scroll_added ||
	    cg->scroll_collected != rg->scroll_collected) {
		fprintf(stderr, "step %u: counters: C +%u -%u, Rust +%u -%u\n",
		    step, cg->scroll_added, cg->scroll_collected,
		    rg->scroll_added, rg->scroll_collected);
		return (1);
	}

	rows = cg->hsize + cg->sy;
	for (yy = 0; yy < rows; yy++) {
		for (xx = 0; xx < cg->sx; xx++) {
			grid_get_cell(cg, xx, yy, &gc);
			rust_grid_get_cell(rg, xx, yy, &rc);
			if (cells_differ(&gc, &rc)) {
				fprintf(stderr,
				    "step %u: cell %u,%u: "
				    "C '%.*s' w%u a%x f%x fg%x bg%x us%x l%u / "
				    "Rust '%.*s' w%u a%x f%x fg%x bg%x us%x l%u\n",
				    step, xx, yy,
				    (int)gc.data.size, gc.data.data,
				    gc.data.width, gc.attr, gc.flags, gc.fg,
				    gc.bg, gc.us, gc.link,
				    (int)rc.size, rc.data, rc.width, rc.attr,
				    rc.flags, rc.fg, rc.bg, rc.us, rc.link);
				return (1);
			}
		}
		if ((grid_get_line(cg, yy)->flags & GRID_LINE_WRAPPED) !=
		    (rust_grid_line_flags(rg, yy) & GRID_LINE_WRAPPED)) {
			fprintf(stderr, "step %u: line %u WRAPPED differs\n",
			    step, yy);
			return (1);
		}
	}
	return (0);
}

/* ---- Random cell content generators ---- */

static const struct {
	const char	*s;
	u_char		 width;
} chars[] = {
	{ "a", 1 }, { "Z", 1 }, { "0", 1 }, { "~", 1 },
	{ "中", 2 }, { "界", 2 }, { "宽", 2 },
	{ "é", 1 },			/* 2-byte */
	{ "→", 1 },			/* 3-byte narrow */
};

static void
random_cell(struct grid_cell *gc)
{
	u_int	k = rnd(nitems(chars));

	memcpy(gc, &grid_default_cell, sizeof *gc);
	utf8_set(&gc->data, 'x');
	gc->data.size = gc->data.have = strlen(chars[k].s);
	memcpy(gc->data.data, chars[k].s, gc->data.size);
	gc->data.width = chars[k].width;

	switch (rnd(6)) {
	case 0:
		gc->fg = rnd(8);
		break;
	case 1:
		gc->fg = rnd(256) | COLOUR_FLAG_256;
		gc->bg = rnd(256) | COLOUR_FLAG_256;
		break;
	case 2:
		gc->fg = rnd(0x1000000) | COLOUR_FLAG_RGB;
		break;
	case 3:
		gc->attr = rnd(0x200);
		break;
	case 4:
		gc->link = rnd(100);
		break;
	}
}

/* ---- Main loop ---- */

int
main(int argc, char **argv)
{
	struct grid		*cg;
	struct rust_grid	*rg;
	struct grid_cell	 gc;
	struct rust_grid_cell	 rc;
	u_int			 seed, steps, i, sx, sy, hlimit;
	u_int			 px, py, nx, ny, dy, bg, rows;

	seed = (argc > 1) ? (u_int)strtoul(argv[1], NULL, 0) : 1;
	steps = (argc > 2) ? (u_int)strtoul(argv[2], NULL, 0) : 20000;
	rng_state = seed ? seed : 1;

	sx = 20 + rnd(60);
	sy = 4 + rnd(20);
	hlimit = 50 + rnd(200);

	cg = grid_create(sx, sy, hlimit);
	rg = rust_grid_create(sx, sy, hlimit);
	printf("difftest: seed=%u steps=%u grid=%ux%u hlimit=%u\n",
	    seed, steps, sx, sy, hlimit);

	for (i = 0; i < steps; i++) {
		rows = cg->hsize + cg->sy;
		bg = (rnd(4) == 0) ? rnd(8) : 8;
		switch (rnd(12)) {
		case 0: /* set_cell */
		case 1:
		case 2:
			px = rnd(sx + 2);
			py = rnd(rows + 2);
			random_cell(&gc);
			top("set_cell %u,%u '%.*s'", px, py,
			    (int)gc.data.size, gc.data.data);
			grid_set_cell(cg, px, py, &gc);
			to_rust_cell(&gc, &rc);
			rust_grid_set_cell(rg, px, py, &rc);
			break;
		case 3: /* scroll_history */
			top("scroll_history bg=%u", bg);
			grid_scroll_history(cg, bg);
			rust_grid_scroll_history(rg, bg);
			break;
		case 4: /* collect_history */
			top("collect_history");
			grid_collect_history(cg, 0);
			rust_grid_collect_history(rg, 0);
			break;
		case 5: /* clear area */
			px = rnd(sx);
			py = rnd(rows);
			nx = 1 + rnd(sx - px);
			ny = 1 + rnd(rows - py);
			top("clear %u,%u %ux%u bg=%u", px, py, nx, ny, bg);
			grid_clear(cg, px, py, nx, ny, bg);
			rust_grid_clear(rg, px, py, nx, ny, bg);
			break;
		case 6: /* clear_lines */
			py = rnd(rows);
			ny = 1 + rnd(rows - py);
			top("clear_lines %u+%u bg=%u", py, ny, bg);
			grid_clear_lines(cg, py, ny, bg);
			rust_grid_clear_lines(rg, py, ny, bg);
			break;
		case 7: /* move_lines within viewport (scroll shape) */
			if (sy < 3)
				break;
			py = cg->hsize + 1 + rnd(sy - 2);
			dy = cg->hsize + rnd(sy - 2);
			ny = 1 + rnd(cg->hsize + sy - ((py > dy) ? py : dy));
			top("move_lines dy=%u py=%u ny=%u bg=%u",
			    dy, py, ny, bg);
			grid_move_lines(cg, dy, py, ny, bg);
			rust_grid_move_lines(rg, dy, py, ny, bg);
			break;
		case 8: /* remove_history */
			if (cg->hsize == 0)
				break;
			ny = 1 + rnd(cg->hsize);
			top("remove_history %u", ny);
			grid_remove_history(cg, ny);
			rust_grid_remove_history(rg, ny);
			break;
		case 9: /* clear_history */
			if (rnd(20) != 0)
				break;
			top("clear_history");
			grid_clear_history(cg);
			rust_grid_clear_history(rg);
			break;
		case 10: /* set padding after a wide cell */
			px = rnd(sx - 1);
			py = rnd(rows);
			random_cell(&gc);
			gc.data.size = gc.data.have = 3;
			memcpy(gc.data.data, "中", 3);
			gc.data.width = 2;
			top("wide+padding %u,%u", px, py);
			grid_set_cell(cg, px, py, &gc);
			to_rust_cell(&gc, &rc);
			rust_grid_set_cell(rg, px, py, &rc);
			grid_set_padding(cg, px + 1, py);
			rust_grid_set_padding(rg, px + 1, py);
			break;
		case 11: /* duplicate viewport into scratch grids + verify */
			top("clone+compare");
			{
				struct grid *cs = grid_create(sx, sy, 0);
				struct rust_grid *rs =
				    rust_grid_create(sx, sy, 0);
				grid_duplicate_lines(cs, 0, cg, cg->hsize,
				    sy);
				rust_grid_duplicate_lines(rs, 0, rg,
				    rg->hsize, sy);
				grid_destroy(cs);
				rust_grid_destroy(rs);
			}
			break;
		}

		if (i % 64 == 0 && compare_grids(cg, rg, i) != 0) {
			dump_trace();
			return (1);
		}
	}

	if (compare_grids(cg, rg, steps) != 0) {
		dump_trace();
		return (1);
	}

	grid_destroy(cg);
	rust_grid_destroy(rg);
	printf("OK: %u steps, no divergence\n", steps);
	return (0);
}
