---
updated: 2026-07-10
source: tizee/tmux fork @ next-3.7, tmux-rs/ + grid-rust.c shim (--enable-rust-grid)
---

# Rust Grid Core (PoC)

A safe-Rust reimplementation of tmux's `grid.c` — the cell/line/history
memory management where the recurring double-free / heap-corruption crashes
live (upstream #4777 / #4888 / #4962 / #5267; this fork's 2026-07-08 and
2026-07-10 crashes are byte-identical stacks). Ownership is `Vec`-based, so
the entire crash class (line-slot aliasing, double-free, stale extended-data
offsets) is unrepresentable or handled, by construction.

Status: **integrated and default.** The Rust engine is now built by default for
this fork (`--disable-rust-grid` to opt out): `grid.c` is replaced by the
`grid-rust.c` shim (delegating wrappers + C copies of the pure SGR/equality
functions) linked against `libgrid_core_ffi.a`. Requires cargo; a plain
`./configure` falls back to the C engine (with a warning) only when cargo is
absent. Runtime-verified 2026-07-10 (capture, history, reflow-resize,
copy-mode enter/exit).

## Build and Engine Selection

The grid engine is selected at configure time, not at runtime. The Rust engine
is the default; the resulting binary links `grid-rust.c` plus
`libgrid_core_ffi.a` instead of compiling the normal `grid.c` implementation.
Pass `--disable-rust-grid` to build the C engine instead.

Build the Rust-backed tmux binary (the default):

```sh
sh autogen.sh    # only needed after editing configure.ac or Makefile.am
./configure --enable-capture-mode --enable-sixel --disable-utf8proc
make clean
make 2>&1 | tail -n 40
```

Switch back to the C grid engine with `--disable-rust-grid`:

```sh
make clean
./configure --enable-capture-mode --enable-sixel --disable-utf8proc --disable-rust-grid
make 2>&1 | tail -n 40
```

Run the Rust grid's standalone validation before daily-driving the replacement:

```sh
cd tmux-rs
cargo test
cargo clippy --all-targets
cargo fmt --check
```

Run the C-vs-Rust differential harness after a successful top-level build,
because it links against the freshly built C objects:

```sh
sh tmux-rs/difftest/difftest.sh 1 20000
```

When toggling the grid engine (adding or removing `--disable-rust-grid`),
`make clean` is required. The two
variants compile different grid translation units and stale object files can
otherwise produce misleading link or runtime results.

## Source Files

| Path | Role |
|------|------|
| `tmux-rs/grid-core/src/grid.rs` | The engine: `Grid` = `Vec<Line>`, `Line` owns `Vec<CellEntry>` + `Vec<ExtdEntry>` |
| `tmux-rs/grid-core/src/cell.rs` | Value types crossing the API: `GridCell`, `Utf8Data` |
| `tmux-rs/grid-core/src/codec.rs` | `utf8_char` packing (<=3 bytes inline, longer interned) — pure Rust replica of `utf8_from_data`/`utf8_to_data` |
| `tmux-rs/grid-core-ffi/src/lib.rs` | The only unsafe layer: 26 `rust_grid_*` C ABI functions |
| `tmux-rs/grid-core/tests/{behavior,reflow}.rs` | 46 behavior contracts transcribed from grid.c |
| `tmux-rs/grid-core-ffi/tests/abi.rs` | ABI contracts for layout, C-visible mirror fields, and public-field writeback including width |
| `tmux-rs/difftest/` | C-vs-Rust differential harness (links the real `grid.o`) |

## Design Rationale

**Why grid.c, not all of tmux.** The tmux-rs project (richardscollin, 2025)
demonstrated that a full port yields ~81k lines of unsafe Rust after six
months — the memory-unsafety survives translation. The grid subsystem is
2,332 lines, owns all the heap the crashes corrupt, and has a narrow-enough
API (74 functions, ~40 used externally) to wrap. Oxidize the organ, not the
patient.

**Why behavior tests, not line-by-line translation.** Every test names a
caller-observable contract ("duplicate_lines is a deep copy", "clearing a
line unwraps the previous line"). This let the Rust reflow be a semantic
reimplementation (gather logical lines → re-lay at new width) instead of a
transcription of C's splice dance — the dance exists to avoid peak-memory
copies and is precisely where C juggles raw pointers between grids.

**Why a differential harness anyway.** Two Rust-side semantic drifts were
caught only by comparing against the real `grid.o`: C's `grid_clear_cell`
asymmetry (a previously-extended cell reads back flags==0 after clear — the
CLEARED flag is lost; a simple cell keeps it) and `grid_expand_line`'s
RGB-background fill creating extended entries. Faithful quirk replication is
deliberate: first be a drop-in equivalent, clean up quirks only once the
Rust engine is the only implementation.

## Architecture

The Rust grid is an owned storage engine behind a C-shaped ABI. tmux still sees
and sometimes edits the leading `struct grid` fields directly, so the boundary is
bidirectional: C-written public geometry is imported into Rust before
geometry-sensitive mutations; Rust-owned state is synchronized back afterward.

### C/Rust Geometry Flow

```
C caller writes public grid fields
(screen_resize, copy-mode setup, etc.)
        |
        v
rust_grid FFI wrapper
(geometry-sensitive mutation)
        |
        +-- import_public_geometry()
        |      sx/sy/hsize/hscrolled/hlimit -> Grid
        |
        v
Rust Grid mutation
(set_cell, clear_lines, adjust_lines, ...)
        |
        v
sync()
Grid counters/geometry -> C-visible mirror
```

The important design constraint is that `rust_grid` cannot treat its leading C
fields as a write-only cache. Width (`sx`) is part of the same public geometry as
height and history fields: `screen_resize()` writes it directly before the next
grid call, and status rendering depends on that write surviving.

## Validation Evidence (2026-07-10)

| Check | Scale | Result |
|-------|-------|--------|
| Rust unit/behavior suites | 57 tests (31 core + 15 reflow + 11 ABI) | green |
| Differential vs real `grid.o` | 20 seeds x 20,000 random ops, 11 op kinds, compare every 64 steps | zero divergence |
| Differential under ASan+UBSan (C side instrumented from source) | 20,000 ops | zero divergence, zero reports |
| clippy --all-targets / cargo fmt | — | clean |
| Crash-profile soak (in-tree) | 1,480 scrolls + 40 full-grid clones over limited history | invariants hold |

Reproduce: `sh tmux-rs/difftest/difftest.sh [seed] [steps]` (requires a
built tree for `grid.o`); `cd tmux-rs && cargo test`.

## Behavioral Contracts

- **Deep copy or no copy.** `duplicate_lines` clones cell storage; mutating
  or destroying either grid never affects the other (the #4777 crash class,
  asserted through both the Rust API and the C ABI).
- **Counter arithmetic balances.** `scroll_added - scroll_collected ==
  hsize` under any op sequence — window-copy's incremental sync (upstream
  8cb4aabb) depends on this identity.
- **Deliberate divergences (fixes over C, maintainer-approved 2026-07-10).**
  (1) A cleared cell always reads back `GRID_FLAG_CLEARED`; C loses the flag
  when the cell was previously extended (implementation leak). (2) Padding
  cells' stored data is unspecified — the PADDING flag is the whole
  contract (C leaks '!' or empty depending on storage history). (3) Reflow
  `hscrolled` adjustment follows the Rust semantic reimplementation, not
  C's per-splice arithmetic. The difftest masks (1) and (2) explicitly.
- **Remaining C-quirk fidelity.** Expand-line chunking (sx/4, sx/2, sx)
  matches; extended-entry reuse and counter arithmetic match bit-for-bit
  (13 seeds x 20,000 ops, zero divergence with only the documented masks).
- **Mirror header stays fresh in both directions.** The `rust_grid` handle's
  leading fields mirror C `struct grid` (verified by `offset_of` tests). Rust
  mutations re-sync the mirror so C-side reads keep working. Before
  geometry-sensitive Rust mutations — including calls that follow `screen_resize()`
  and copy-mode setup — FFI paths import C-written geometry fields: width (`sx`),
  viewport height (`sy`), history size (`hsize`), scroll offset (`hscrolled`), and
  history limit (`hlimit`). This prevents direct writes by C callers from being
  overwritten by a stale Rust cache.
- **Known divergence: reflow.** Content and WRAPPED flags are
  contract-tested; `hscrolled` adjustment and per-line time/metadata
  preservation may differ from C (semantic reimplementation). Reflow is
  excluded from the differential op set; decide align-vs-accept before the
  engine swap.

## Lessons from the 2026-07-10 ABI Crash

- **Memory safety does not imply ABI safety.** Moving line ownership into Rust
  makes the old C aliasing/double-free class unrepresentable, but the C/Rust
  boundary can still corrupt the Rust engine's model if it misses a public C
  contract.
- **The compatibility target is real tmux behavior, not the clean API we wish
  existed.** `struct grid` is not opaque in upstream tmux: callers both read and
  write public geometry such as `gd->sx`, `gd->sy`, `gd->hsize`, and
  `gd->hscrolled`. The Rust handle must treat those fields as bidirectional ABI,
  not a Rust-owned cache.
- **BDD tests must model callers, not wrappers.** The original ABI suite proved
  that Rust mutations refreshed C-visible fields, but did not simulate
  `screen_resize` or `window_copy_clone_screen` writing those fields before the
  next FFI call. The regression tests now encode those caller-visible scenarios,
  including stale-width status rendering after a terminal resize.
- **Do not patch symptoms at the panic site.** Clamping `clear_history` would
  have hidden the abort while leaving Rust and C geometry out of sync. The fix
  belongs at the boundary where C-written geometry enters the Rust-owned grid.
- **Keep the organ transplant boundary small.** The design goal remains to
  replace the crash-prone grid storage with Rust, not to refactor tmux around an
  ideal opaque grid API. Local FFI import keeps the fork rebaseable while still
  preserving Rust's ownership benefits.

## Remaining Work: the Engine Swap (audited 2026-07-10)

The wall between PoC and `--enable-rust-grid` is `grid_get_line` /
`grid_peek_line` leaking `struct grid_line*` — 44 call sites in 8 files:

| Reads | Sites | Difficulty |
|-------|-------|-----------|
| Metadata: `gl->flags` (15), `gl->cellsize` (11), `gl->cellused` (2), `gl->time` (1) | 29 | trivial — accessor functions (FFI already exports most) |
| Data: `gl->celldata` (4), `gl->extddata` (3), `gl->extdsize` (2) | 7 (format.c stats, screen.c, screen-write.c, window-copy.c) | replace with `grid_get_cell` / new stats accessor |

Plan (Phase 1 + 4 of `.plans/grid-core-rust-poc-260710/`):
1. C-side: add `grid_line_*` accessors to grid.c, convert the 44 sites —
   upstream-rebasable, engine-agnostic prep.
2. Shim `grid-engine.h`: `#ifdef ENABLE_RUST_GRID` maps `grid_*` to
   `rust_grid_*`; grid-view/grid-reader stay C, calling through the shim.
3. configure `--disable-rust-grid` (default on per maintainer decision),
   cargo build wired into Makefile.am, three-variant no-warning builds.
4. Daily-drive with the crash logger armed; the fork's own crash ring then
   monitors the Rust engine in production.

## BDD Scenarios

| # | Scenario | Given | When | Then |
|---|----------|-------|------|------|
| 1 | Deep clone | grid with extended cells | duplicate_lines, then mutate src | dst unchanged; destroying either is safe |
| 2 | Crash-profile soak | limited history, busy writes | 1,480 scrolls + 40 clones | every read consistent, counters balance |
| 3 | Engine equivalence | same seed | 20,000 random ops into both engines | cell-by-cell identical every 64 steps |
| 4 | Cleared-cell divergence | extended cell | clear with default bg | Rust keeps `CLEARED`; the C differential masks this documented storage-history quirk |
| 5 | ABI layout | — | offset_of assertions | `rust_grid_cell` offsets 32/33/34/36/38/40/44/48/52, size 56 |
| 6 | Reflow contracts | wrapped/wide/styled content | shrink then grow | text, styles, wide-char integrity preserved |
| 7 | C public-field writeback | tmux C writes `gd->sx`, `gd->sy`, `gd->hsize`, or `gd->hscrolled` during resize/copy-mode setup | next Rust FFI call runs | Rust-owned grid imports the geometry; `history + viewport == storage lines`; clear-history remains safe |
| 8 | Resized status width | `screen_resize` has written a wider `gd->sx` directly | a Rust grid mutation writes or clears cells at the new right edge | C-visible width survives sync and the far-edge cell uses the requested background |

Test mapping:

| Scenario | Concrete coverage |
|----------|-------------------|
| 1 | `duplicate_lines_is_a_deep_copy`; `duplicate_lines_between_handles_is_deep` |
| 2 | `soak_scroll_collect_clone_cycle_stays_consistent` |
| 3 | `tmux-rs/difftest/difftest.sh` |
| 4 | `cleared_extended_cell_still_reads_cleared_flag`; difftest clear-cell mask |
| 5 | `abi_struct_layout_matches_c_expectations` |
| 6 | `reflow_roundtrip_preserves_text_and_styles`; `reflow_split_respects_wide_char_boundaries` |
| 7 | `c_resize_geometry_writes_survive_adjust_lines`; `c_copy_clone_geometry_writes_survive_set_hscrolled` |
| 8 | `c_width_write_survives_next_grid_mutation` |

## Key Files

| File | Purpose |
|------|---------|
| `tmux-rs/grid-core/src/grid.rs` | Safe Rust grid engine; `import_public_geometry` and `adjust_lines_from_public_geometry` preserve C-written geometry before storage changes. |
| `tmux-rs/grid-core-ffi/src/lib.rs` | C ABI boundary; `rust_grid` mirrors C-visible fields, imports them before mutations, and syncs Rust-owned state back afterward. |
| `tmux-rs/grid-core-ffi/tests/abi.rs` | Caller-visible ABI regression tests, including resize-width import and copy-mode geometry writeback. |
| `screen.c` | C-side screen resize path that writes `s->grid->sx` directly before later grid calls. |
| `status.c` / `screen-redraw.c` / `tty-draw.c` | Status redraw path whose full-width rendering depends on the current grid width. |

## Related Documentation

| Doc | Relevance |
|-----|-----------|
| [`DEBUGGING.md`](DEBUGGING.md) | §4 linker-stub harness technique reused by difftest; crash-class analysis |
| [`crash-log.md`](crash-log.md) | The flight recorder that will monitor the Rust engine in daily use |
| `.plans/grid-core-rust-poc-260710/` | Full task plan, findings (upstream-bug lineage), progress log |
| `.plans/rust-grid-status-width-260710/` | Root-cause notes and TDD progress for the stale-width status regression |
