# Debugging this tmux fork

A field manual for maintaining this fork: how to find out *why* the server
crashed, how to locate the offending C code, and how to make daily builds
record enough to diagnose a crash after the fact.

Written for the environment this fork is developed on (macOS arm64, Homebrew
toolchain), but the ASan and ring-buffer sections apply to Linux too.

---

## 0. Mental model: client vs server

tmux is two processes:

- a **server** (one per socket) that owns every session, window, pane, and the
  grid memory;
- one or more **clients** that attach to it.

When you see the client print

    [server exited unexpectedly]

the **server process died** (crash or abort). The terminal emulator and the
client are innocent — do not chase them. Everything below is about finding out
why the server process died.

A crash usually kills every pane on that server at once. Keep a session-restore
plugin (resurrect/continuum) so a crash is recoverable while you debug.

---

## 1. After a crash: read the logs

### 1.1 macOS crash reports (`.ips`) — the first place to look

macOS already keeps a ring of crash reports. Every SIGSEGV/SIGABRT/SIGBUS in the
server is written here automatically:

    ~/Library/Logs/DiagnosticReports/tmux-YYYY-MM-DD-HHMMSS.ips

List the most recent tmux crashes:

    ls -lat ~/Library/Logs/DiagnosticReports/tmux-*.ips | head

An `.ips` is two lines: a JSON header line, then a JSON body. Extract the parts
that matter:

    F=~/Library/Logs/DiagnosticReports/tmux-2026-07-03-201236.ips
    # signal / termination
    tail -n +2 "$F" | python3 -c 'import sys,json; d=json.load(sys.stdin);
    print("exception:", d.get("exception"));
    print("termination:", d.get("termination"));
    print("asi:", d.get("asi"))'
    # faulting-thread backtrace, symbol per frame
    tail -n +2 "$F" | python3 -c '
    import sys, json
    d = json.load(sys.stdin)
    imgs = d["usedImages"]
    ft = d.get("faultingThread", 0)
    for f in d["threads"][ft]["frames"]:
        i = f.get("imageIndex", -1)
        name = imgs[i].get("name", "?") if 0 <= i < len(imgs) else "?"
        sym = f.get("symbol", hex(f.get("imageOffset", 0)))
        print("%-16s %s" % (name, sym))
    '

What each field tells you:

| Field | Meaning |
|-------|---------|
| `exception.signal` | `SIGABRT` = an assertion/`abort()`/libmalloc caught something. `SIGSEGV` = bad pointer dereference. `SIGBUS` = misaligned/unmapped access. |
| `termination.indicator` | e.g. `Abort trap: 6`. |
| `asi` (Additional Signal Info) | libsystem hints. `libsystem_malloc: BUG IN CLIENT ... POINTER BEING FREED WAS NOT ALLOCATED` = **heap corruption**. |
| `faultingThread` frames | The call stack at death. Read bottom-up: `main → ... → the function that died`. |

**The single most important debugging lesson from this fork's history:** a
`SIGABRT` inside `free`/`grid_free_line` with the malloc message
`POINTER BEING FREED WAS NOT ALLOCATED` almost never means the free site is
buggy. It means an **earlier out-of-bounds write corrupted the heap metadata**,
and libmalloc only noticed at the next `free`. The crash frame is the *victim*,
not the culprit. Do not "fix" the free — find the earlier bad write (see §2).
Corollary: a garbled screen at crash time is the *residue* of a half-drawn
frame, not evidence about the cause.

If the frames are only hex offsets (a stripped release build), symbolize them:

    # needs the matching dSYM (see §5.1)
    atos -o tmux.dSYM/Contents/Resources/DWARF/tmux -arch arm64 -l <loadAddr> <frameAddr>

### 1.2 tmux's own verbose log

For non-crash misbehavior (wrong rendering, stuck state), turn on tmux logging.
`log_open()` writes `tmux-server-<pid>.log` and `tmux-client-<pid>.log` in the
process's cwd:

    tmux kill-server                       # start clean
    tmux -vv new-session -d -s dbg         # -v per level; -vv is verbose
    # ... reproduce ...
    ls tmux-server-*.log ; tail -f tmux-server-*.log

`log_debug()` calls throughout the code land here. This is your `printf`-trace
channel; it is also the source the ring buffer in §5.2 snapshots on crash.

---

## 2. ASan: catch the bug at the write, not the death

### 2.1 What ASan is

AddressSanitizer (ASan) is a compiler instrumentation (`-fsanitize=address`).
At compile time it surrounds every allocation and stack variable with poisoned
"redzones" and keeps a shadow map of which bytes are legal to touch. At run time
every load/store is checked against the shadow. The instant code reads or writes
one byte out of bounds, or frees a pointer twice, or uses freed memory, ASan
stops **at that instruction** and prints the exact file:line plus the
allocation's origin.

That is precisely what turns a "victim at free" crash (§1.1) into a real
diagnosis: ASan reports the *bad write*, not the later innocent free.

Cost: ~2x slower, ~3x memory. Fine for debugging, not for daily use — see §5 for
the daily-build story.

### 2.2 Build this fork with ASan

The custom capture-mode feature is behind a configure switch. **You must pass
`--enable-capture-mode`, or you will be testing a binary that does not contain
the code you are debugging.** Also do a clean rebuild — a plain `make` will not
recompile objects just because `CFLAGS` changed, which silently mixes
instrumented and non-instrumented objects (and breaks the link on
feature-gated symbols).

    PKG="/opt/homebrew/opt/libevent/lib/pkgconfig:/opt/homebrew/opt/ncurses/lib/pkgconfig:/opt/homebrew/opt/utf8proc/lib/pkgconfig"
    PKG_CONFIG_PATH="$PKG" ./configure \
      CC=clang --enable-utf8proc --enable-capture-mode --disable-crash-log \
      CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1" \
      LDFLAGS="-fsanitize=address -L/opt/homebrew/opt/libevent/lib -L/opt/homebrew/opt/ncurses/lib -L/opt/homebrew/opt/utf8proc/lib" \
      CPPFLAGS="-I/opt/homebrew/opt/libevent/include -I/opt/homebrew/opt/ncurses/include -I/opt/homebrew/opt/utf8proc/include"
    make clean && make -j8
    strings tmux | grep -x capture-mode   # sanity: feature really linked

### 2.3 Run the server under ASan

The server daemonizes, so its stderr goes nowhere. Route ASan output to files
with `ASAN_OPTIONS=log_path` — the report lands in `/tmp/tmux_asan.<pid>`:

    SOCK=/tmp/dbg.sock
    export ASAN_OPTIONS="log_path=/tmp/tmux_asan:abort_on_error=1:detect_leaks=0"
    ./tmux -S "$SOCK" -f /dev/null new -d -s w -x 120 -y 40 "bash --norc"
    # ... drive it (send-keys / resize-window / copy-mode / capture-mode) ...
    ls /tmp/tmux_asan.*        # any file here == a real violation, with backtrace

Useful `ASAN_OPTIONS`:

| Option | Effect |
|--------|--------|
| `log_path=/tmp/tmux_asan` | write reports to files (daemon-safe) |
| `abort_on_error=1` | `abort()` on first error so it also lands in DiagnosticReports |
| `detect_leaks=0` | tmux leaks a lot at exit; silence LSan while hunting a specific bug |
| `halt_on_error=0` | keep running after the first error to collect more |
| `strict_string_checks=1` | flag `strlen`/`strcpy` overruns too |

Drive the server headlessly with the same commands a script or the tmux
remote-control skill would use: `send-keys`, `resize-window` (forces reflow),
`copy-mode`, `capture-mode`, `capture-pane`.

---

## 3. Layered elimination: the method that actually works

Guessing which of 400 changed lines is buggy is slow. Eliminate whole regions
with evidence instead:

1. **Reproduce under instrumentation.** Get ASan (or the crash report) to fire
   once. Even a flaky repro is enough to start.
2. **Read the backtrace and name the suspects.** The faulting stack points at a
   *subsystem* (e.g. copy-mode grid clone). List the fork's diffs in that
   subsystem: `git log --oneline` and `git diff <upstream-merge-base> -- <files>`.
3. **Clear the pure logic first, in isolation.** Any module that is just data
   transformation (parsers, string/label generators, math) can be linked into a
   standalone ASan harness and hammered without a tmux server (see §4). If the
   harness stays clean under heavy input, that module is *ruled out* — a
   positive result you can trust, not a hunch.
4. **Narrow to the integration layer.** What remains is the glue that touches
   real tmux state (grids, screens, the draw path, client/session pointers).
   That is where the bug is. Re-run the end-to-end ASan build focused on those
   operations.
5. **Fix at the boundary, add a regression test, verify red→green.**

This fork's copy-mode crash was found exactly this way: fuzzing cleared
`capture-pattern.c` and `capture-hint.c` outright, which pointed at the
grid/draw glue, where two real bugs lived:

- `grid_set_tab()` did a variable-length `memset` into the fixed 32-byte
  `utf8_data.data` with no internal bound (only one of its two callers guarded
  the width) — a heap/stack overflow. Fixed by clamping inside the function.
- `cmd_capture_mode_exec()` passed a client with a `NULL` session into the
  overlay, which dereferenced `c->session` in `status_message_set()` — a
  NULL-deref that crashed the server when `capture-mode` ran without an attached
  client. Fixed by a boundary guard, matching `cmd_copy_mode_exec`.

---

## 4. Writing an ASan fuzz harness for a module

Goal: exercise one `.c` file's real functions, in isolation, under ASan, with
adversarial input — no tmux server needed.

**Linking trick.** The module's object references other tmux symbols it never
calls on the paths you test. Stub exactly those. Get the list with `nm`:

    nm -u capture-pattern.o | sed 's/^ *//' \
      | grep -vE 'dyld_stub|___asan|_mem(set|cpy|move|cmp)|_str(len|chr|cmp|dup|ndup)'

Then write a `stubs.c` that defines each remaining symbol as an empty body (they
are never executed on the tested paths — they only satisfy the linker). Note:
`-Wl,-undefined,dynamic_lookup` is *not* a shortcut here — objects with eager
data references (e.g. a global `current_time`) fail to load at dyld time. Stub
explicitly.

**Force ASan to validate outputs.** Touch every returned byte (`strlen` each
produced string, read each array element), or ASan may not inspect an allocation
you never dereference.

**Cover the boundaries.** For counting/label math, stress values *around* the
powers where the algorithm changes regime (e.g. `nkeys^L` for hint generation);
for string parsers, feed empty, all-punctuation, all-brackets, very long, and
multibyte/CJK lines.

Skeleton:

    /* fuzz.c */
    #include <stdio.h>
    #include <string.h>
    #include "capture-pattern.h"
    int main(void) {
        struct capture_pattern cp;
        capture_pattern_compile(&cp, "(https?://[^ ]+)|(/[^ ]+)|(#[0-9A-Fa-f]+)");
        const char *lines[] = { "", "####", "))))", "see (http://a/b). [x], {y}!",
                                "/p/中文/路径)) #ff0000 http://中文.example/x" };
        for (size_t i = 0; i < sizeof lines/sizeof *lines; i++)
            for (int r = 0; r < 200; r++) {
                int n = 0;
                struct capture_match *m =
                    capture_pattern_match_line(&cp, lines[i], (unsigned)i, &n);
                if (m) { for (int k=0;k<n;k++) (void)strlen(m[k].text);
                         capture_pattern_resolve_overlaps(m,&n);
                         capture_match_free(m,n); }
            }
        capture_pattern_free(&cp);
        puts("OK"); return 0;
    }

Build & run:

    clang -fsanitize=address -g -O1 -I. fuzz.c stubs.c capture-pattern.o -o /tmp/fuzz -lm
    ASAN_OPTIONS=abort_on_error=1 /tmp/fuzz

**Testing a single non-static function** (e.g. `grid_set_tab`, declared in
`tmux.h`): mirror only the ABI-compatible structs it touches in the test file
(avoids pulling all of `tmux.h`), declare the function `extern`, and link the
real object plus a stub file. A red test calls it with an oversized argument and
expects an ASan abort; after the fix the same test passes and normal inputs are
unchanged. This is the red→green loop for a C memory bug.

---

## 5. Daily builds that record their own crashes

You run your own build all day. Make it so that *any* crash leaves enough behind
to locate the code, without paying ASan's cost. Two independent layers; use
both.

### 5.1 Keep the release build symbolizable (do this regardless)

This fork already compiles with `-g3 -ggdb` (see `Makefile.am`), which is why
the crash report in §1.1 symbolized to function names. Preserve that:

- **Do not `strip` the installed binary.** If you package it, keep a companion
  `tmux.dSYM`:

      dsymutil tmux -o tmux.dSYM      # after building

- With `-g` (or the dSYM) kept, macOS DiagnosticReports **is already a crash
  ring buffer**: it retains the last N reports per program, symbolized, for
  free. For a personal daily driver this alone is often enough — every crash
  gives you a real backtrace in `~/Library/Logs/DiagnosticReports/`.
- Symbolize a stripped report against the dSYM with `atos` (§1.1).

Recommended daily configure (fast, symbolizable, feature-complete):

    ./configure --enable-utf8proc --enable-capture-mode \
      CFLAGS="-O2 -g -fno-omit-frame-pointer"

`-fno-omit-frame-pointer` keeps backtraces reliable at `-O2`. This build already
includes the ring-buffer crash logger (§5.2), which is on by default.

### 5.2 Built-in ring-buffer crash log (on by default; `--disable-crash-log` to omit)

DiagnosticReports is macOS-only and does not include *what tmux was doing*
before the crash. This fork ships a small crash handler that, on a fatal signal,
writes `<socket-dir>/tmux-crash-<pid>.log` containing the backtrace **and the
last few hundred `log_debug` lines**, then re-raises the signal so the OS crash
reporter still runs. That "ring buffer" — a bounded in-memory tail of internal
events, flushed only when the process dies — is exactly what tells you which
pane/mode/command was active right before death.

**It is compiled in by default** (this is your daily driver), so a normal build
already has it:

    ./configure --enable-utf8proc --enable-capture-mode \
      CFLAGS="-O2 -g -fno-omit-frame-pointer"
    make -j8
    # add --disable-crash-log to omit it; it also self-disables on platforms
    # without execinfo.h / backtrace.

After a crash:

    ls -t "$(dirname "$(tmux display -p '#{socket_path}')")"/tmux-crash-*.log | head
    # header: signal + pid ; then a symbolized backtrace ; then the log ring

**How it relates to `-v` / `-vv` (§1.2).** Both draw from the same `log_debug`
stream, but they are opposite tools:

| | `-v` / `-vv` | crash log |
|--|--------------|-----------|
| When it writes | continuously, the whole run | only on a fatal signal |
| Where | `tmux-server-<pid>.log` on disk | `tmux-crash-<pid>.log` (ring dump) |
| How much | everything | the last few hundred lines |
| Cost | heavy | near zero |
| Setup | must pass `-v` *before* the bug | always on, no setup |
| Use for | actively debugging a reproducible issue | post-mortem of an unexpected crash |

`-vv` is a firehose you turn on before reproducing; the crash log is a flight
recorder that always runs and dumps only on a crash. With `-vv` active you get
*both* the full log file and the crash file.

Implementation lives in **`crash-log.c`** / **`crash-log.h`**, hooked from
`log.c` (ring feed) and `server.c` (handler install); the build wiring is in
`configure.ac` / `Makefile.am`. Key properties, and why they matter:

- **The ring is fed even without `-v`.** `log_debug()` normally short-circuits
  when file logging is off; under `ENABLE_CRASH_LOG` it instead records each line
  into the ring via a bounded, malloc-free `vsnprintf` (see `log_vwrite`). So a
  daily build with no logging flags still captures the trail. Cost is one small
  `vsnprintf` per `log_debug` — acceptable for a personal driver, and gated off
  by default.
- **The handler is async-signal-safe.** No `malloc`, no `printf`: only
  `open`/`write`/`backtrace_symbols_fd` and hand-rolled integer formatting; the
  crash file path is precomputed at init. A re-entry guard prevents a fault in
  the handler from looping.
- **`sigaltstack`** is installed so a stack-overflow `SIGSEGV` can still be
  handled.
- **`SA_RESETHAND` + `raise(sig)`** means you get **both** the ring-buffer file
  **and** the macOS `.ips` — they are complementary (the `.ips` has registers
  and image list; the ring file has the internal-event trail).
- **Heap-corruption caveat still applies (§1.1):** the backtrace points at the
  victim frame; the ring's last lines are the real lead. When the ring implicates
  a subsystem, reach for ASan (§2) to pin the exact bad write.
- **Turn it off for ASan builds.** The handler installs `sigaction` for
  SIGSEGV/SIGABRT after ASan has installed its own; leaving both on lets our
  handler shadow ASan's crash reporting. Configure ASan builds with
  `--disable-crash-log` (see the recipes in §6).

For reference, the shape of the two halves (the real code is in `crash-log.c`):

    /* ring feed, in log.c log_vwrite(), before the log_file==NULL check */
    char rbuf[CRASH_RING_LINE]; va_list ap2; va_copy(ap2, ap);
    /* prefix + vsnprintf(msg, ap2) into rbuf, bounded, no malloc */
    crash_log_record(rbuf);

### 5.3 Cheaper-than-ASan heap checking for a spell of daily use

If a bug is rare and you want extra scrutiny without an ASan rebuild, run the
release server under macOS's guard allocators for a day:

    # scribble freed memory + guard page after each allocation
    MallocScribble=1 MallocGuardEdges=1 ./tmux -S /tmp/dbg.sock new -d ...
    # or the heavier libgmalloc (catches the offending access immediately)
    DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib ./tmux -S /tmp/dbg.sock new -d ...

These turn "silent corruption now, crash later" into "crash at the bad access",
at a fraction of ASan's setup cost — useful for narrowing before committing to a
full ASan repro.

---

## 6. Quick reference

| Situation | Do this |
|-----------|---------|
| Server just died | `ls -lat ~/Library/Logs/DiagnosticReports/tmux-*.ips`; read signal + faulting stack (§1.1) |
| `SIGABRT` + `POINTER BEING FREED WAS NOT ALLOCATED` | heap corruption; the free is the victim — hunt an earlier OOB write with ASan (§1.1, §2) |
| Wrong rendering / stuck state | `tmux -vv`; read `tmux-server-*.log` (§1.2) |
| Need the exact bad write | ASan build (`--enable-capture-mode`, clean rebuild), `ASAN_OPTIONS=log_path=...` (§2) |
| Which of my diffs is guilty | fuzz-clear the pure modules, narrow to the glue (§3, §4) |
| Fixing a memory bug | write a red ASan test on the real function, fix, confirm green (§4) |
| Make daily builds diagnosable | keep `-g`/dSYM; crash logger is on by default (§5) |
| Rare heap bug, no ASan rebuild | `MallocScribble=1 MallocGuardEdges=1` or `libgmalloc` (§5.3) |

Build recipes:

    # Debug / hunting a bug
    # (--disable-crash-log so our fatal-signal handler does not shadow ASan's)
    ./configure --enable-utf8proc --enable-capture-mode --disable-crash-log \
      CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1" \
      LDFLAGS="-fsanitize=address"
    make clean && make -j8

    # Daily driver (fast, symbolizable, crash logger on by default via §5)
    ./configure --enable-utf8proc --enable-capture-mode \
      CFLAGS="-O2 -g -fno-omit-frame-pointer"
    make -j8 && dsymutil tmux -o tmux.dSYM
