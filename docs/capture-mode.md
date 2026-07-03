---
updated: 2026-07-04
source: tizee/tmux fork @ next-3.7 (build-gated by --enable-capture-mode)
---

# Capture Mode

Capture mode is a keyboard-hint text picker layered on top of copy mode. It
scans the visible pane for interesting substrings (URLs, paths, git SHAs,
emails, custom regexes), draws a short prefix-free hint label next to each
distinct match, and copies the one whose label you type into the paste buffer /
clipboard. It replaces the external `tmux-capture` plugin with an in-tree,
dependency-free implementation, and is compiled in only when configured with
`--enable-capture-mode` (otherwise the build is upstream-equivalent).

## Source Files

| File | Role |
|------|------|
| `cmd-capture-mode.c` | The `capture-mode` command: entry point, client/session guard, ensures copy mode then arms the overlay |
| `capture-pattern.c` / `.h` | Regex match over one line, trailing-punctuation trimming, overlap resolution |
| `capture-hint.c` / `.h` | Prefix-free, minimum-length hint-label generation over a key alphabet |
| `window-copy.c` | Overlay lifecycle: scan, hint assignment, per-pane key table, draw, key handling, copy, teardown (all `window_copy_capture_*`) |
| `key-bindings.c` | Default root binding `Y` → `capture-mode` |
| `options-table.c` | `@capture-*` user options |

## Design Rationale

**Problem.** Selecting a URL or path from terminal output means entering copy
mode and hand-driving the cursor to both ends of the token — slow and
error-prone. The external `tmux-capture` plugin solved this but needed an
out-of-tree dependency and shelled out.

**Solution.** Do it inside tmux, on top of the copy-mode machinery that already
knows the grid, scrollback, and rendering. Copy mode gives a frozen, scrollable
snapshot of the pane; capture mode scans that snapshot, labels the matches, and
turns a multi-keystroke cursor dance into "look at the label, type it." Because
it is an overlay on copy mode it works on whatever is visible, including
scrollback you have scrolled into view.

**Key insight.** The hard parts are *disambiguation* and *alignment*: labels
must be prefix-free so typing is unambiguous, and match offsets (byte positions
in a joined line string) must map back to display columns so highlights land on
the right cells under multibyte/double-width content. Those two concerns are
isolated in `capture-hint.c` and `window_copy_capture_byte_to_col`.

## Architecture

Three pure modules (pattern matching, hint generation) sit under one stateful
integration layer in `window-copy.c`. The command layer is a thin guard.

```
        Y  (root key)                capture-mode (command)
             \                          /
              v                        v
        cmd_capture_mode_exec  (client + session guard)
              |
              |  ensure copy mode, then
              v
        window_copy_capture_pane -> window_copy_capture_start
              |
    +---------+-----------------------------------------+
    |                    scan phase                     |
    |  per visible row:                                 |
    |    grid_string_cells ---> capture_pattern_match_line
    |                              (regex + trim)       |
    |    byte offsets  ---> window_copy_capture_byte_to_col
    +---------------------------------------------------+
              |
              v
   resolve overlaps -> dedupe identical texts (capture_match_hint)
              |
              v
   capture_hint_generate  (prefix-free labels for N distinct texts)
              |
              v
   build per-pane key table "capture-mode-%<id>", bind hint keys +
   cancel keys, switch client into it, capture_active = 1
```

### Selection loop (overlay armed)

```
key press
  |
  v
window_copy_key_table  --(capture_active)-->  "capture-mode-%<id>"
  |                                              |
  |  hint key                                    |  q / Esc / C-c
  v                                              v
send -X capture-key <k>                    send -X capture-cancel
  |                                              |
  v                                              v
window_copy_capture_key                    window_copy_capture_stop
  |
  +-- append k to capture_input (bounded)
  +-- keep hints whose label has this prefix (memcmp)
  |
  +-- exact label match (h->len == input_len)?
        |yes                         |no, still prefixes
        v                            v
   copy match text  ->          redraw, wait for next key
   window_copy_copy_buffer
   (paste buffer + clipboard),
   then capture stop
        |no prefix matches
        v
   reset input, redraw
```

## Behavioral Contracts

- **Overlay needs an interactive client with a session.** `cmd_capture_mode_exec`
  returns a no-op when the client or its session is NULL; the overlay draws hint
  labels and posts status messages that dereference `c->session`. (This guard is
  the fix for a server crash when `capture-mode` was run without an attached
  client.)
- **Capture is an overlay on copy mode, not a replacement.** If the pane is not
  already in copy mode, the command enters it first; an already-active copy-mode
  view (including scrolled-in scrollback) is captured in place without losing
  position.
- **Hint labels are prefix-free and minimum length.** `capture_hint_generate`
  produces labels over the key alphabet such that no label is a prefix of
  another, using the shortest maximum length for the required count; typing a
  full label is therefore unambiguous. (Verified by an AddressSanitizer fuzz
  harness across alphabets and counts around the `nkeys^L` boundaries — see
  `docs/DEBUGGING.md` §4.)
- **Identical match texts share one hint.** Matches are grouped by text content
  (`capture_match_hint[i]` maps match *i* to a distinct-text index); one label
  serves all occurrences.
- **Byte offsets are mapped to display columns.** Regex offsets index the joined
  line string; `window_copy_capture_byte_to_col` walks grid cells (honoring
  padding and tab cells) so highlights and labels align under multibyte and
  double-width content.
- **Matches are cleaned and de-overlapped.** `capture_pattern_trim_end` strips
  trailing sentence punctuation and *unbalanced* closing brackets (balanced ones
  inside a URL survive); `capture_pattern_resolve_overlaps` keeps the longer of
  two overlapping matches (ties: the earlier one).
- **Input is bounded.** `capture_input` holds at most `CAPTURE_HINT_MAX_LEN`
  keys; comparison is by `memcmp` over the current length (labels are not NUL
  terminated in the buffer), and the buffer resets when it fills or when no hint
  matches the typed prefix.
- **Build-gated, upstream-clean when off.** All capture state and code paths are
  under `#ifdef ENABLE_CAPTURE_MODE`; without the flag there is no `capture-mode`
  command, no `Y` binding, and no manual section.

## Key Mechanisms

### Pattern set resolution

The active regex is a single POSIX ERE (`regcomp(REG_EXTENDED)`) built from
options: `@capture-patterns-override` *replaces* the built-in set entirely;
otherwise the built-in pattern (URLs, emails, git SHAs, `sha256:…`, file paths,
…; see `capture-pattern.h`) is used, with `@capture-patterns` appended as extra
alternations. A compile failure disarms the overlay rather than erroring.

### Per-pane key table

Arming builds a throwaway key table named `capture-mode-%<pane-id>` and binds
every hint key to `send -X capture-key -- <k>` and `q`/`Esc`/`C-c` to
`send -X capture-cancel`. While `capture_active`, `window_copy_key_table`
returns this table, so *all* key input is routed through the capture bindings
until a selection or cancel. Teardown removes the table.

### Overlay styling

Three grid cells drive the draw: matched-text style
(`@capture-match-style`), hint-label style (`@capture-hint-style`), and the
typed-prefix style (`@capture-hint-typed-style`), each with gruvbox-ish
defaults, resolved once at arm time and applied in `window_copy_capture_draw`.

## BDD Scenarios

| # | Scenario | Given | When | Then |
|---|----------|-------|------|------|
| 1 | Enter and copy | pane shows a URL, client attached | press `Y`, type the URL's hint label | URL is in the paste buffer/clipboard; overlay closes |
| 2 | Duplicate text, one hint | same string appears twice | arm capture | both occurrences share one label; typing it copies that text |
| 3 | Prefix-free typing | >alphabet-size matches | arm capture | multi-char labels appear; a partial prefix narrows, a full label selects |
| 4 | Cancel | overlay armed | press `q`/`Esc`/`C-c` | overlay closes, nothing copied, copy mode preserved |
| 5 | No matches | pane has nothing matchable | run `capture-mode` | status "no matches"; no key table armed |
| 6 | Too many matches | matches exceed label capacity | arm capture | status "too many matches"; overlay disarmed |
| 7 | Headless invocation | no attached client/session | run `capture-mode` command | no-op, server does not crash |
| 8 | Multibyte alignment | matches after CJK/double-width text | arm capture | highlights/labels land on the correct columns |
| 9 | Trailing bracket trim | `(https://x/y)` in prose | arm capture | match excludes the unbalanced `)` |
| 10 | Build off | configured without `--enable-capture-mode` | run `capture-mode` | unknown command; behaves like upstream |

## Key Files

| File | Purpose |
|------|---------|
| `cmd-capture-mode.c` | `cmd_capture_mode_exec` — guard + arm |
| `window-copy.c` | `window_copy_capture_start/pane/key/draw/stop/clear`, `window_copy_capture_byte_to_col`, key-table routing in `window_copy_key_table` |
| `capture-pattern.c` | `capture_pattern_compile/match_line/trim_end/resolve_overlaps` |
| `capture-hint.c` | `capture_hint_generate/decode/free` |
| `capture-pattern.h` | built-in default pattern; `CAPTURE_HINT_MAX_LEN`, key defaults |

## Open Questions

- The overlay scans only the *visible* rows of the copy-mode screen, not the
  whole scrollback; scanning a large history is intentionally out of scope.
- Hint-key alphabet quality (home-row, low-collision) is left to the user via
  `@capture-hint-keys`; there is no locale/keyboard-aware default beyond the
  built-in set.
- `capture-key`/`capture-cancel` are copy-mode `-X` sub-commands; they are only
  meaningful while an overlay is armed (guarded by `capture_active`).

## Related Documentation

| Doc | Relevance |
|-----|-----------|
| [`DEBUGGING.md`](DEBUGGING.md) | Building with `--enable-capture-mode`; fuzzing `capture-pattern`/`capture-hint` under ASan |
| `README` (Capture Mode) | User-facing quick start, configuration, built-in patterns |
| tmux `copy-mode` manual | The mode this overlay is built on |
