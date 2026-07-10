# Documentation

Design and maintenance docs for this tmux fork. Upstream user documentation
lives in the `tmux.1` manual and the top-level `README`; these docs cover the
fork's additions and how to debug the fork itself.

## Index

| Doc | What it covers |
|-----|----------------|
| [`DEBUGGING.md`](DEBUGGING.md) | Field manual: reading crash reports (`.ips`), AddressSanitizer, layered elimination, writing fuzz harnesses, and daily builds that record their own crashes |
| [`grid-core-rust.md`](grid-core-rust.md) | Safe-Rust grid engine PoC (`tmux-rs/`): design, validation evidence, engine-swap roadmap |
| [`capture-mode.md`](capture-mode.md) | Design of capture mode — the hint-based copy-mode text picker (`--enable-capture-mode`) |
| [`crash-log.md`](crash-log.md) | Design of the ring-buffer crash logger (`--enable-crash-log`, on by default) |
| [`upstream-fixes.md`](upstream-fixes.md) | Index of upstream tmux bugs fixed locally (policy, rebase protocol); per-bug docs live under [`bugfix/`](bugfix/) |

## Conventions

- **Design docs** are grounded ("Mode A"): every structural claim maps to code
  you can read. They lead with the *contract* (what must be true) over
  implementation shape, so they survive refactors.
- Each begins with YAML frontmatter (`updated:`, `source:`), and ends with a
  BDD scenario table and a Related Documentation link table.
- Diagrams are small, single-purpose ASCII/box drawings kept under ~75 columns.

## Build flags referenced here

| Flag | Default | Effect |
|------|---------|--------|
| `--enable-capture-mode` | off | Compile in capture mode (see `capture-mode.md`) |
| `--enable-crash-log` / `--disable-crash-log` | on | Ring-buffer crash logger (see `crash-log.md`); disable for ASan builds |
