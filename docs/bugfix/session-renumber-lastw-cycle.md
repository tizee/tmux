---
updated: 2026-07-10
source: tizee/tmux fork @ next-3.7 (patch to upstream session.c, not build-gated)
---

# Infinite Loop in `session_renumber_windows` (lastw TAILQ Cycle)

Server wedges at 100% CPU when renumbering windows in a session where one
window is linked at two indexes and both winlinks sit on the last-window
stack. Fixed by a one-line `WINLINK_VISITED` guard in `session.c`. Upstream
carries the same bug as of 2026-07-10.

## Symptom

With a window linked at two indexes in one session (`link-window`, grouped
sessions) and `renumber-windows on`, exiting a pane (or any renumber) wedges
the **server** in an infinite loop at 100% CPU. Every subsequent `tmux ls` /
`attach` hangs: the clients block forever on a socket the server never
services again. Pane processes keep running but the session is unreachable.

Observed in production on 2026-07-10: server pid spinning in
`window_pane_error_callback → server_kill_window → session_renumber_windows`,
confirmed by `sample(1)` and lldb. If the loop also allocates, the process can
balloon until the kernel OOM killer SIGKILLs it (ours died at ~187 GB
compressed: `memorystatus: killing largest compressed process tmux`), which
bypasses the crash logger — SIGKILL is uncatchable.

## Root cause

`session_renumber_windows()` rebuilds the last-window stack `s->lastw` by
mapping each old entry to its new winlink **by window**:

```c
/* session.c, "Fix the stack of last windows now" */
TAILQ_FOREACH(wl, &old_lastw, sentry) {
        wl->flags &= ~WINLINK_VISITED;
        wl_new = winlink_find_by_window(&s->windows, wl->window);
        if (wl_new != NULL) {                /* BUG: VISITED never checked */
                TAILQ_INSERT_TAIL(&s->lastw, wl_new, sentry);
                wl_new->flags |= WINLINK_VISITED;
        }
}
```

When a window is linked at two indexes, two old lastw entries resolve to the
**same** `wl_new` (`winlink_find_by_window` returns the lowest-index winlink),
so the same node is `TAILQ_INSERT_TAIL`'d twice. The flag is *set* but never
*read* — a guard that guards nothing.

The corruption detonates in two stages (why naive repros pass):

```
Stage 1: double INSERT_TAIL(A) ... INSERT_TAIL(B) ... INSERT_TAIL(A)
         A->next = NULL again; list traversal terminates but is
         truncated to [A]; B orphaned; A->tqe_prev stale (&B->next)

Stage 2: next winlink_stack_push(A):
         TAILQ_REMOVE writes through stale tqe_prev (B->next = NULL)
         but head.first still == A; INSERT_HEAD then sets
         A->next = head.first = A          <-- self-cycle

Boom:    any TAILQ_FOREACH over lastw (e.g. the next renumber,
         triggered by a pane exiting with renumber-windows on)
         spins forever; server event loop never returns
```

Evidence from the live hang: lldb memory walk of the in-flight `old_lastw`
showed `wl(idx=3) → wl(idx=2) → wl(idx=1) → wl(idx=3)` and two winlinks
(`idx=3`, `idx=6`) pointing at the same `struct window`.

## Fix

One line — make the existing flag do its job (mirrors upstream's own fix
`c5542637` for the same pattern in `session_group_synchronize1`):

```c
-               if (wl_new != NULL) {
+               if (wl_new != NULL && (~wl_new->flags & WINLINK_VISITED)) {
                        TAILQ_INSERT_TAIL(&s->lastw, wl_new, sentry);
                        wl_new->flags |= WINLINK_VISITED;
```

The analogous loop in `session_group_synchronize1` resolves by **index**
(`winlink_find_by_index`), which is unique per session, so it cannot produce
duplicates and needs no change.

Semantics note: a duplicated window keeps a single lastw entry (its
lowest-index winlink), matching how `winlink_find_by_window` already collapses
duplicates everywhere else. `last-window` behavior verified unchanged.

## Regression test

```sh
sh test/test_renumber_hang.sh ./tmux    # PASS = fixed, FAIL = hung server
```

The script builds the exact trigger: three windows + `link-window` duplicate,
a select sequence that leaves **both** winlinks of the shared window on the
stack (the sequence must end on a third window — ending on the duplicate pops
it and hides the bug), then two `move-window -r` renumbers with a
`winlink_stack_push` between them. Each command runs under `timeout 5`; a
wedged server fails loudly. Verified red on the unfixed binary
(`FAIL: server hung during second renumber`, server at 100% CPU) and green on
the fixed one.

Run it after every upstream rebase: if upstream fixes the bug and the local
patch drops, the test still proving green is the confirmation.

## Upstream references

| Ref | Relevance |
|-----|-----------|
| [`tmux/tmux#3676`](https://github.com/tmux/tmux/issues/3676) | Same corruption family: hang in `winlink_stack_remove` after swap-window + kill-session in grouped sessions |
| [`tmux/tmux#3645`](https://github.com/tmux/tmux/issues/3645) | Same symptom: 100% CPU server hang on rapid window create/kill |
| Commit `b13c2307` | "Correct visited flag when the last window list is rebuilt by renumbering" — added the VISITED clear/set to this very loop but not the check |
| Commit `c5542637` | "Set visited flag on last windows when linking session" — the equivalent guard added to `session_group_synchronize1` |
| Commit `8b3e2eab` | Introduced `WINLINK_VISITED` (GitHub #3588) |

If someone upstreams this: the one-line diff above plus the repro steps from
`test/test_renumber_hang.sh` are self-contained; cite `b13c2307` as the
precedent fix for the same flag in the same function.

## Behavior scenarios

| Scenario | Given | When | Then |
|----------|-------|------|------|
| Renumber with duplicate links | Window linked at indexes 0 and 9, both winlinks on lastw | `move-window -r` | Renumber completes; server responsive |
| Repeat renumber after switches | Renumbered session with duplicate links | select-window twice, `move-window -r` again | No hang; lastw traversals terminate |
| Pane exit with renumber-windows on | `renumber-windows on`, duplicate links in session | `exit` in a pane | Window killed, renumber runs, server responsive |
| last-window with duplicates | Duplicate window on lastw (collapsed to one entry) | `last-window` twice | Switches to previous window and back; no corruption |
| Upstream rebase check | Fork rebased onto new upstream | `sh test/test_renumber_hang.sh ./tmux` | PASS, whether the fix is local or upstreamed |

## Related Documentation

| Doc | Link |
|-----|------|
| Upstream-fix ledger (policy + index) | [`../upstream-fixes.md`](../upstream-fixes.md) |
| Debugging field manual (sampling, lldb, `.ips`) | [`../DEBUGGING.md`](../DEBUGGING.md) |
| Crash logger (why SIGKILL/OOM leaves no crash log) | [`../crash-log.md`](../crash-log.md) |
