#!/bin/sh
# Regression test: renumbering windows must not hang the server when a
# window is linked at two indexes and both winlinks are on the last-window
# stack.
#
# Bug: session_renumber_windows() rebuilt s->lastw without checking
# WINLINK_VISITED. Two old lastw entries pointing at the same window both
# resolved (via winlink_find_by_window) to the same new winlink, which was
# then TAILQ_INSERT_TAIL'd twice. The double insert truncates the list and
# leaves the node with a stale tqe_prev; the next winlink_stack_push of
# that node then REMOVEs through the stale pointer and INSERT_HEADs it
# onto itself (A->next == A). Any later lastw traversal (e.g. the next
# renumber, triggered by a pane exiting with renumber-windows on) spins
# forever, wedging the whole server at 100% CPU.
#
# Behavior under test: after link-window + window switches + renumbering,
# the server must still answer commands.
#
# Usage: sh test/test_renumber_hang.sh [path-to-tmux]
# Exit 0 = pass, 1 = fail (server hung), 2 = setup error.

TMUX_BIN=${1:-./tmux}
T="$TMUX_BIN -Lrenumber-hang-test -f/dev/null"

fail() {
	echo "FAIL: $*"
	# The server may be wedged spinning; kill it hard.
	pkill -9 -f "Lrenumber-hang-test" 2>/dev/null
	exit 1
}

run() {
	# Run a tmux command with a 5s deadline; a hung server blocks forever.
	timeout 5 $T "$@" >/dev/null 2>&1
}

[ -x "$TMUX_BIN" ] || { echo "no tmux binary: $TMUX_BIN"; exit 2; }

$T kill-server 2>/dev/null
sleep 0.2

# Session with three windows (0, 1, 2); window 0 also linked at index 9.
run new-session -d -s test -x 80 -y 24 "sleep 60" || exit 2
run new-window -d -t test: "sleep 60" || exit 2
run new-window -d -t test: "sleep 60" || exit 2
run link-window -d -s test:0 -t test:9 || exit 2

# Build a last-window stack holding both winlinks of the shared window,
# ending on an unrelated window so neither gets popped before renumber:
# visiting 0, 1, 9 then 2 leaves lastw=[wl9, wl1, wl0] with curw=wl2.
run select-window -t test:1 || exit 2	# push wl0, lastw=[0]
run select-window -t test:9 || exit 2	# push wl1, lastw=[1,0]
run select-window -t test:2 || exit 2	# push wl9, lastw=[9,1,0]

# Renumber: both wl9 and wl0 resolve to the same new index-0 winlink;
# with the bug it is TAILQ_INSERT_TAIL'd twice, truncating the rebuilt
# lastw and leaving the node with a stale tqe_prev.
run move-window -r -t test || fail "server hung during first renumber"

# Re-push the corrupted winlink: stack_remove writes through the stale
# tqe_prev, then INSERT_HEAD links the node to itself (A->next == A).
run select-window -t test:0 || fail "server hung on select-window 0"
run select-window -t test:1 || fail "server hung on select-window 1"

# Any traversal of the now-cyclic stack spins forever.
run move-window -r -t test || fail "server hung during second renumber"

# The real assertion: the server must still be responsive.
timeout 5 $T list-windows -t test >/dev/null 2>&1 || \
	fail "server unresponsive after renumber (lastw cycle)"

$T kill-server 2>/dev/null
echo "PASS: no hang renumbering with duplicate window links in lastw"
exit 0
