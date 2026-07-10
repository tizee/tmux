---
updated: 2026-07-10
source: tizee/tmux fork @ next-3.7 (patches to upstream code, not build-gated)
---

# Upstream Bug Fixes Carried in This Fork

This fork occasionally fixes bugs that live in **upstream** tmux code, not in
fork-only code. Sending each fix upstream is the right thing in principle, but
upstream PR throughput and the social cost of AI-assisted patches make that
slow and uncertain; we keep the fixes here and document them thoroughly enough
that (a) a future rebase knows what to drop once upstream fixes it, and (b)
anyone can lift the patch + repro into an upstream issue verbatim.

This file is the **index**; each bug gets its own doc under
[`bugfix/`](bugfix/) with the full root-cause analysis, evidence, patch, and
regression test.

## Rules for entries

- The patch must be **minimal and upstream-shaped** (KNF style, no fork
  `#ifdef`s) so it either merges cleanly upstream or drops cleanly on rebase.
- Every entry ships a **regression test** runnable against a plain binary, so
  "did upstream fix this?" is one command after each rebase.
- Root cause is documented with **evidence** (stack samples, memory dumps,
  repro), not speculation.
- One doc per bug in `bugfix/`, named `<subsystem>-<short-slug>.md`, following
  the docs conventions in [`README.md`](README.md).

## Index

| # | Doc | Area | Symptom | Patch | Test | Upstream status |
|---|-----|------|---------|-------|------|-----------------|
| 1 | [`bugfix/session-renumber-lastw-cycle.md`](bugfix/session-renumber-lastw-cycle.md) | `session.c` last-window stack | Server wedges at 100% CPU on window kill/renumber; every client hangs | `session_renumber_windows`, one line | `test/test_renumber_hang.sh` | Unfixed as of 2026-07-10 (`tmux/tmux` master); siblings fixed in `b13c2307`, `c5542637` |

## After every upstream rebase

Run each entry's regression test against the rebased binary:

```sh
sh test/test_renumber_hang.sh ./tmux    # entry 1
```

- **PASS with local patch dropped by the rebase** — upstream fixed it; delete
  the patch note from the entry and mark it `Fixed upstream in <commit>`.
- **PASS with local patch intact** — still our divergence; keep carrying it.
- **FAIL** — the rebase reintroduced or mutated the bug; re-apply the patch
  from the entry's doc.

## Related Documentation

| Doc | Link |
|-----|------|
| Docs index and conventions | [`README.md`](README.md) |
| Debugging field manual (sampling, lldb, `.ips`) | [`DEBUGGING.md`](DEBUGGING.md) |
| Crash logger design | [`crash-log.md`](crash-log.md) |
