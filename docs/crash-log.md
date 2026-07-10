---
updated: 2026-07-10
source: tizee/tmux fork @ next-3.7 (build-gated by --enable-crash-log, default on)
---

# Ring-Buffer Crash Log

The crash log makes a daily-driver tmux build self-diagnosing: when the server
dies from a fatal signal it writes `<socket-dir>/tmux-crash-<pid>.log` — the
signal, a symbolized backtrace, and the tail of the most recent internal log
lines — then re-raises the signal so the OS crash reporter still runs. Nothing
is written during normal operation. It is compiled in by default for this fork
(`--disable-crash-log` to omit) so an unexpected crash leaves a locatable trail
with zero prior setup.

## Source Files

| File | Role |
|------|------|
| `crash-log.c` / `.h` | The ring buffer, the async-signal-safe fatal-signal handler, init/pruning |
| `log.c` | Feeds each formatted log line into the ring (`log_vwrite`); does not short-circuit `log_debug` when file logging is off |
| `server.c` | Arms the handler in `server_start` with the socket directory |
| `configure.ac` / `Makefile.am` | `--enable-crash-log` (default on), `execinfo`/`backtrace` probe, conditional source |

## Design Rationale

**Problem.** When a self-maintained tmux crashes, the client only prints
`[server exited unexpectedly]`. macOS keeps a symbolized `.ips` crash report,
but (a) it is macOS-only and (b) it records registers and a backtrace, not
*what tmux was doing* just before — and for heap-corruption crashes the
backtrace points at the innocent free site, not the culprit (see
[`DEBUGGING.md`](DEBUGGING.md) §1.1). Reproducing under `-v` after the fact only
helps if you had `-v` on *before* the crash.

**Solution.** Keep a bounded, always-on, in-memory tail of the internal log
(`log_debug`) — a flight recorder — and dump it, with a backtrace, only when a
fatal signal fires. The recent-lines trail names the active pane / mode /
command right before death, which is the real lead when the backtrace is a
heap-corruption victim.

**Key insight.** This must cost almost nothing in normal operation and must be
safe to run from a signal handler. So the ring is fed with a bounded,
malloc-free `vsnprintf`, and the handler uses only async-signal-safe primitives
with everything it needs precomputed at init.

## Architecture

Two phases: a cheap always-on *record* path in the log funnel, and a *dump*
path that runs only inside the signal handler.

### Record path (every log line, always)

```
log_debug(...) / fatal*(...)         [ENABLE_CRASH_LOG: log_debug no longer
        |                             short-circuits when log_file == NULL]
        v
log_vwrite(msg, ap, prefix)
        |
        |  bounded, malloc-free:
        |    snprintf(prefix) + vsnprintf(msg, va_copy(ap))  -> rbuf
        v
crash_log_record(rbuf)
        |
        |  prefix denylist (crash_noise[]): per-glyph render spam is dropped
        |  and each run folded into "(ring: dropped N render-noise lines)"
        v
crash_ring[head++ % CRASH_RING_LINES] = rbuf     (fixed 2-D char array)
        |
        v
(then, only if log_file != NULL) the normal vasprintf/stravis file write
```

### Dump path (fatal signal only)

```
SIGSEGV|SIGBUS|SIGABRT|SIGILL|SIGFPE
        |  (delivered on the sigaltstack)
        v
crash_handler(sig)
        |
        +-- re-entry guard: if already handling, _exit(128+sig)
        |
        +-- open(crash_path)          [path precomputed at init]
        +-- write: signal name+number, pid
        +-- backtrace() + backtrace_symbols_fd(fd)
        +-- dump crash_ring tail (oldest -> newest)
        +-- flush pending dropped-noise count, if any (signal-safe)
        +-- close
        |
        v
signal(sig, SIG_DFL); raise(sig)   -> OS default action -> core / .ips
```

### Init (server start)

```
server_start()
   |
   v
crash_log_init(dirname(socket_path))
   |
   +-- mkdir(dir); precompute crash_path = <dir>/tmux-crash-<pid>.log
   +-- crash_prune(dir): keep newest CRASH_KEEP_FILES, unlink the rest
   +-- sigaltstack(alt) so a stack-overflow SIGSEGV can still be handled
   +-- sigaction(each fatal sig, SA_ONSTACK | SA_RESETHAND)
```

## Behavioral Contracts

- **Recorded even without `-v`.** Under `ENABLE_CRASH_LOG`, `log_debug` does not
  return early when file logging is off; every line still reaches the ring. The
  ring feed is bounded and allocation-free, so the logging-off hot path stays
  cheap. (Without `-v`, the file write is still skipped — only the ring is fed.)
- **Per-glyph render noise never reaches the ring.** Lines matching the
  `crash_noise[]` prefix table (`utf8_to_data:`, `utf8_from_data:`, `UTF-8 `,
  `utf8proc_wcwidth(`, `input_top_bit_set`, `screen_write_combine:`) are
  dropped and counted; the next kept line is preceded by a single
  `(ring: dropped N render-noise lines)` summary. A busy TUI pane emits these
  five-plus times per drawn character, which otherwise evicts the
  command-level trail the ring exists to preserve. A count still pending when
  a fatal signal fires is flushed by the handler using only signal-safe
  primitives. The full render detail is still available via `-vv` file
  logging, which is unaffected by the filter.
- **The handler is async-signal-safe.** It calls only `open`, `write`,
  `backtrace`/`backtrace_symbols_fd`, `close`, `signal`, `raise`, plus
  hand-rolled integer formatting. No `malloc`, no `printf`, no `strsignal`. The
  destination path and the alternate stack are established at init, not in the
  handler.
- **Both artifacts are produced.** `SA_RESETHAND` restores the default
  disposition and the handler `raise`s the signal again, so the OS crash
  reporter (macOS `.ips`, or a core file) runs *in addition to* the ring file.
  The two are complementary: the `.ips` has registers and the image list; the
  ring file has the internal-event trail.
- **A fault inside the handler does not loop.** A re-entry guard makes a second
  fault `_exit(128+sig)` immediately.
- **Stack-overflow crashes are still captured.** The handler runs on a dedicated
  `sigaltstack`, so a `SIGSEGV` from exhausting the normal stack can still be
  handled.
- **Bounded footprint, bounded files.** The ring is a fixed `CRASH_RING_LINES ×
  CRASH_RING_LINE` array (no growth); on the disk side, init prunes to the newest
  `CRASH_KEEP_FILES` crash files.
- **Crash files live with the socket.** The directory is derived from
  `socket_path` (a writable per-user tmp directory); it falls back to `/tmp` if
  the socket path is unavailable.
- **Off cleanly when disabled or unsupported.** Without `--disable-crash-log` the
  feature is on; with it, none of the code compiles in and the build behaves like
  upstream. On a platform lacking `execinfo.h`/`backtrace`, configure disables it
  automatically (unless explicitly `--enable-crash-log`, which then errors).
- **Do not combine with ASan.** The handler installs `sigaction` for
  SIGSEGV/SIGABRT after ASan installs its own; leaving both on shadows ASan's
  reporting. ASan builds must configure `--disable-crash-log`.

## Key Mechanisms

### Why the ring feed lives in `log_vwrite`

`log_vwrite` is the single funnel every `log_debug`/`fatal`/`fatalx` passes
through. Recording there captures the whole internal trace with one hook. The
recorded line reuses the caller's format + args via `va_copy` (leaving the
original `va_list` intact for the subsequent file write) and is truncated into a
fixed `CRASH_RING_LINE` slot — the same content the `-v` file would show, minus
the vis-encoding, at a fraction of the cost.

### Relationship to `-v` / `-vv`

Same source stream, opposite tools: `-v`/`-vv` write *everything*
*continuously* to `tmux-server-<pid>.log` (heavy, opt-in per run); the crash log
keeps only the recent tail *in memory* and writes it *only on a crash*
(always-on, near-zero cost). With `-vv` active you get both files after a crash.
See [`DEBUGGING.md`](DEBUGGING.md) §5.2 for the comparison table.

### Reading a crash file

    ls -t "$(dirname "$(tmux display -p '#{socket_path}')")"/tmux-crash-*.log | head
    # header: signal + pid ; symbolized backtrace ; recent log ring (oldest first)

## BDD Scenarios

| # | Scenario | Given | When | Then |
|---|----------|-------|------|------|
| 1 | Crash writes a file | default build, server running | server hits a fatal signal | `tmux-crash-<pid>.log` appears with signal, pid, backtrace, log ring |
| 2 | Trail without `-v` | server started without `-v` | it crashes | the log ring in the crash file is non-empty |
| 3 | Both artifacts | macOS, default build | server crashes | a `.ips` in DiagnosticReports *and* the crash file both exist |
| 4 | Stack overflow | recursion exhausts the stack | `SIGSEGV` raised | the handler still runs (alt stack) and writes the file |
| 5 | Handler re-fault | handler itself faults | second signal | process `_exit`s without looping |
| 6 | File pruning | more than the keep-limit of crash files | server starts | only the newest `CRASH_KEEP_FILES` remain |
| 7 | Disabled build | configured `--disable-crash-log` | server crashes | no crash file; `crash_log_*` not linked; upstream behavior |
| 8 | Unsupported platform | no `execinfo.h` | `./configure` | crash logging auto-disabled with a warning, build succeeds |
| 9 | Normal operation | default build, no crash | server runs and exits cleanly | no crash file is ever written |
| 10 | Render noise folded | a TUI pane logs a burst of `utf8_to_data`/`screen_write_combine` lines | a normal line is recorded after the burst | the ring holds one `(ring: dropped N render-noise lines)` summary, then the normal line; no noise lines |
| 11 | Pending noise at crash | noise lines were dropped after the last kept line | server crashes | the crash file ends with the dropped-count summary before `=== end ===` |

## Key Files

| File | Purpose |
|------|---------|
| `crash-log.c` | `crash_log_record` (noise filter + ring put), `crash_handler`, `crash_log_init`, `crash_prune`, `crash_utoa`, `crash_noise[]` |
| `crash-log.h` | Public API; `CRASH_RING_LINES`, `CRASH_RING_LINE`; test accessors `crash_log_ring_head`/`crash_log_ring_line` |
| `test/test_crash_ring.c` | Standalone unit test for the noise filter (drop, fold, prefix-only match, truncation) |
| `log.c` | `log_vwrite` ring feed; `log_debug` guard under `ENABLE_CRASH_LOG` |
| `server.c` | `crash_log_init` call in `server_start` |
| `configure.ac` / `Makefile.am` | `--enable-crash-log` default-on wiring, `execinfo` probe |

## Open Questions

- Portability of `backtrace`/`backtrace_symbols_fd` beyond macOS/glibc is not
  pursued here; configure disables the feature where they are absent.
- The recorded line is truncated to `CRASH_RING_LINE` and is not vis-encoded, so
  very long log lines are clipped in the ring (the full form is only in the `-v`
  file when logging is on).
- A ring slot half-written when a signal lands is dumped as-is; the ring is a
  best-effort trail, not a transactional log.
- The noise denylist is static and prefix-based. If a future debugging session
  needs the per-glyph render trail in the ring itself (not just via `-vv`),
  the list would need a runtime toggle; so far the `-vv` file has been the
  right tool for that case.

## Related Documentation

| Doc | Relevance |
|-----|-----------|
| [`DEBUGGING.md`](DEBUGGING.md) | §1.1 reading `.ips`; §2 ASan; §5.2 crash log vs `-v`/`-vv`, daily builds |
| `README` (Crash Logging) | User-facing overview and enable/disable |
| tmux `-v` logging | The `log_debug` stream this ring samples |
