---
title: Git-Hash Version Stamping
author: tizee <tizee@github.com>
status: draft
created: 2026-07-04
url: docs/ (fork-local design docs)
---

# Git-Hash Version Stamping

Make local builds of this fork report a rich, git-aware version instead of the
bare `next-3.7`, so a build can be traced to the exact commit it was built from.
`tmux -V` gains a `zhu`-style rich line; `#{version}` / `TERM_PROGRAM_VERSION`
gain the short `+g<hash>` suffix.

## Objective

`tmux -V` should print the source commit and its release time, e.g.
`tmux next-3.7+g94c8b83 (released 2026-07-03T10:53:22Z, 6 hours ago)`, while
release tarballs (no `.git`) keep printing the plain upstream version.

## Background

Today the version is a configure-time constant: `AC_INIT([tmux], next-3.7)`
expands to `@VERSION@`, injected as `-DTMUX_VERSION='"next-3.7"'`
(`Makefile.am`), and `getversion()` in `tmux.c` returns it verbatim. Every
local build of the fork therefore reports the same string, so a running
`tmux -V` cannot be tied to a commit. When debugging crash-log reports or
capture-mode behaviour across rebases, "which build is this?" is currently
unanswerable from the binary alone.

The author's own `zhu` agent solves the same problem: `build.rs` injects
`BUILD_VERSION` / `BUILD_COMMIT` / `BUILD_DATETIME` and a runtime formatter
prints `zhu 2026.07.03+g94c8b83 (released 2026-07-03T10:53:22Z, 6 hours ago)`.
This design ports that shape to C.

## Goals

- A user running `tmux -V` on a local build sees the exact source commit and how
  long ago it was committed, with no extra steps.
- Automation reading `#{version}` (or `TERM_PROGRAM_VERSION`) can distinguish two
  builds from different commits.
- A release tarball build (no git checkout) behaves byte-for-byte like upstream:
  plain `next-3.7`, no rich clause.

## Non-goals

- **No working-tree "dirty" marker.** Local development edits files constantly;
  a `-dirty` suffix would be noise. Explicitly out of scope.
- **No build-time timestamp.** "released" is the *commit* time (reproducible),
  not wall-clock build time — a rebuild of the same commit yields the same
  string.
- **Not gated behind a build flag.** This is a strict enhancement with a safe
  no-git fallback, not a fork-only semantic like capture-mode; no
  `--enable-*` toggle.
- No change to the version *number* itself (`next-3.7` stays); only a suffix and
  a rich `-V` line are added.

## Scenarios

1. **Developer on a clean checkout.** Builds the fork, runs `tmux -V`, sees
   `tmux next-3.7+g<hash> (released <iso>, N hours ago)`. Copies the hash into a
   bug report; the maintainer checks out that commit exactly.
2. **CI packaging a tarball.** `make dist` product has no `.git`; the generated
   header carries an empty hash; `tmux -V` prints plain `tmux next-3.7`, matching
   upstream so downstream expectations do not break.
3. **Format consumer.** A status line with `#{version}` shows `next-3.7+g<hash>`;
   two panes from different builds are visibly distinguishable.

## Diagrams

### Build-time flow (who produces the git facts)

```
configure.ac AC_INIT ── @VERSION@ ──► -DTMUX_VERSION (Makefile.am)
                                            │  "next-3.7"  (base, no git)
make (BUILT_SOURCES, every build)           │
   │                                        │
   ▼                                        │
etc/git-version.sh                          │
   │  git rev-parse --short HEAD            │
   │  git log -1 --format=%ct  (commit epoch)
   ▼                                        │
version-git.h  (rewritten only if changed)  │
   #define TMUX_GIT_HASH  "94c8b83"         │
   #define TMUX_GIT_EPOCH 1751537602L       │
        │                                   │
        └──────────────┬────────────────────┘
                       ▼
                    tmux.c  (compiles with both)
```

Fallback: outside a git checkout (or git absent) the script emits
`TMUX_GIT_HASH ""` and `TMUX_GIT_EPOCH 0L`.

### Runtime composition (what each caller gets)

```
                 TMUX_VERSION ("next-3.7")
                 TMUX_GIT_HASH ("94c8b83" | "")
                 TMUX_GIT_EPOCH (commit epoch | 0)
                         │
        ┌────────────────┴───────────────────┐
        ▼                                     ▼
 getversion()                          tmux -V branch
 version_format_short()                version_format_full()
        │                                     │
        ▼                                     ▼
 "next-3.7+g94c8b83"          "next-3.7+g94c8b83 (released
 (empty hash → "next-3.7")     2026-07-03T10:53:22Z, 6 hours ago)"
        │                             (empty hash → "next-3.7")
        ▼
 #{version}, TERM_PROGRAM_VERSION,
 DCS response, proc log
```

## Interfaces

New pure module `version-format.c` / `version-format.h` (libc only, no tmux
runtime — unit-testable standalone like `capture-pattern`):

```c
/* "base" or "base+ghash" (hash empty -> "base"). */
void version_format_short(char *buf, size_t len, const char *base,
    const char *git_hash);

/* Relative bucket for [build, now]; see contracts for buckets. */
void version_format_relative(char *buf, size_t len, time_t build, time_t now);

/* Rich -V line. hash empty OR build<=0 -> short version only. Else:
 * "base+ghash (released <iso-utc>, <relative>)". iso derived from build. */
void version_format_full(char *buf, size_t len, const char *base,
    const char *git_hash, time_t build, time_t now);
```

Generated `version-git.h` (build artifact, git-ignored):

```c
#define TMUX_GIT_HASH  "94c8b83"      /* "" outside a git checkout */
#define TMUX_GIT_EPOCH 1751537602L    /* commit unix time, 0 if unknown */
```

`tmux.c` changes: `getversion()` returns a cached `version_format_short(...)`;
the `-V` branch prints `version_format_full(...)` against `time(NULL)`.

## Constraints

- **OpenBSD KNF**, clean under the strict warning set, both with and without
  `--enable-capture-mode`.
- The pure module must link in a standalone test with only libc
  (`cc -o t test/test_version.c version-format.c`) — so it must not call
  `xmalloc`/`fatal`; callers pass fixed buffers.
- The generate step must be idempotent-per-commit: rewrite `version-git.h` only
  when its content changes, or every `make` needlessly recompiles `tmux.c`.
- ISO time is UTC (`gmtime` + `%Y-%m-%dT%H:%M:%SZ`), matching zhu's `...Z` form.

## Dependencies / Infrastructure

- `git` at build time (optional; absent → fallback). `sh` for the generator.
- `gmtime`, `strftime`, `snprintf` from libc; already used across tmux.

## Behavioral Contracts

Relative-time buckets (ported 1:1 from `zhu` `format_relative_time`, so parity
is a *requirement*, not a coincidence):

- `delta < 60s` → `"just now"` (also covers negative delta / clock skew).
- `delta < 1h` → `"N min ago"`; exactly `"1 min ago"` when `N == 1`.
- `delta < 24h` → `"N hours ago"`; `"1 hour ago"` when `N == 1`.
- `delta < 7d` → `"yesterday"` when `N == 1` day, else `"N days ago"`.
- otherwise → commit date as `"YYYY-MM-DD"` (UTC).

Version composition:

- `git_hash == ""` ⇒ short and full both equal the bare base; **no `+g`, no
  released clause** (upstream-identical).
- `git_hash != ""` ⇒ short is `base+g<hash>`; full appends
  ` (released <iso>, <relative>)`.
- `getversion()` value is stable within a process (compute once, cache).
- Output never overflows the caller's buffer (bounded `snprintf`).

## BDD Scenarios

| # | Scenario | Given | When | Then |
|---|----------|-------|------|------|
| 1 | Short with hash | base `next-3.7`, hash `94c8b83` | `version_format_short` | `next-3.7+g94c8b83` |
| 2 | Short no hash | base `next-3.7`, hash `""` | `version_format_short` | `next-3.7` |
| 3 | Relative just now | build = now-30s | `version_format_relative` | `just now` |
| 4 | Relative min plural | build = now-5m | `version_format_relative` | `5 min ago` |
| 5 | Relative min singular | build = now-90s | `version_format_relative` | `1 min ago` |
| 6 | Relative hours plural | build = now-3h | `version_format_relative` | `3 hours ago` |
| 7 | Relative hours singular | build = now-75m | `version_format_relative` | `1 hour ago` |
| 8 | Relative yesterday | build = now-25h | `version_format_relative` | `yesterday` |
| 9 | Relative days | build = now-3d | `version_format_relative` | `3 days ago` |
| 10 | Relative far past | build = now-30d | `version_format_relative` | `YYYY-MM-DD` (UTC) |
| 11 | Relative future skew | build = now+30s | `version_format_relative` | `just now` |
| 12 | Full with hash | base+hash+epoch, far-past date | `version_format_full` | `next-3.7+g94c8b83 (released <iso>, <date>)` |
| 13 | Full no hash | hash `""` | `version_format_full` | `next-3.7` (no released clause) |
| 14 | Full zero epoch | hash set, epoch `0` | `version_format_full` | short version only |

Each row maps to a `test_*` function in `test/test_version.c`.

## Alternatives Considered

- **Configure-time capture** (put the hash in `configure.ac`): rejected — it only
  refreshes on `./configure`, so a plain `make` after new commits reports a stale
  hash. Build-time (`make`) capture is the correct granularity.
- **Build wall-clock time for "released"**: rejected per non-goals — not
  reproducible; commit time answers "what source is this" better.
- **Emitting the ISO string from the shell script**: rejected — passing only the
  epoch and formatting with `gmtime` in C keeps the generator trivial and avoids
  shell/`date` portability differences (BSD vs GNU `date`).

## Open Issues

- Out-of-tree builds (`configure` in a separate dir): the `#include
  "version-git.h"` resolves via the build dir where the rule writes it; confirm
  once VPATH builds are exercised. In-tree build (the fork's `build.sh`) is the
  primary path and is unaffected.

## Resolved Issues

- **Dirty marker?** No — confirmed with maintainer; local edits would make it
  perpetually dirty (2026-07-04).
- **Rich clause scope?** `-V` only; `#{version}` stays the compact `+g<hash>`
  form (2026-07-04).
