#!/bin/sh
# Build and run the C-vs-Rust grid differential test.
# Usage: sh difftest.sh [seed] [steps]
# Requires: tmux built in the repo root (grid.o etc.), cargo.
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIR="$ROOT/tmux-rs/difftest"
DEFS="-DHAVE_STRLCPY=1 -DHAVE_STRLCAT=1 -DHAVE_CLOCK_GETTIME=1 -DHAVE_FLOCK=1 \
 -DHAVE_DIRFD=1 -DHAVE_GETPROGNAME=1 -DHAVE_SETENV=1 -DHAVE_STRSEP=1 \
 -DHAVE_STRNDUP=1 -DHAVE_MEMMEM=1 -DHAVE_GETLINE=1 -DHAVE_ASPRINTF=1 \
 -DHAVE_FGETLN=1 -DHAVE_STRCASESTR=1 -DHAVE_STRTONUM=1 -DHAVE_DAEMON=1 \
 -DHAVE_GETDTABLESIZE=1 -DHAVE_CFMAKERAW=1 -DHAVE_FORKPTY=1 -DHAVE_GETPEEREID=1"

# 1. Rust staticlib.
(cd "$ROOT/tmux-rs" && cargo build --release -p grid-core-ffi --quiet)

# 2. C objects: reuse the tmux build's grid.o/utf8.o/log.o/xmalloc.o.
for o in grid.o utf8.o log.o xmalloc.o compat/unvis.o compat/vis.o; do
	[ -f "$ROOT/$o" ] || { echo "missing $ROOT/$o - run make first" >&2; exit 1; }
done

# 3. Link. crash_log_record comes from crash-log.o (log.o references it).
cc -Wall $DEFS -I"$ROOT" -I/opt/homebrew/opt/libevent/include \
	-o /tmp/grid_difftest \
	"$DIR/difftest.c" "$DIR/stubs.c" \
	"$ROOT/grid.o" "$ROOT/utf8.o" "$ROOT/log.o" "$ROOT/xmalloc.o" \
	"$ROOT/crash-log.o" "$ROOT/compat/unvis.o" "$ROOT/compat/vis.o" \
	"$ROOT/compat/reallocarray.o" "$ROOT/compat/recallocarray.o" \
	"$ROOT/compat/explicit_bzero.o" \
	"$ROOT/tmux-rs/target/release/libgrid_core_ffi.a" \
	-L/opt/homebrew/opt/libevent/lib -levent_core -lm

# 4. Run.
/tmp/grid_difftest "${1:-1}" "${2:-20000}"
