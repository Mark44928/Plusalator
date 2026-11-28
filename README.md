# Plusalator v1.6.0 - Power Tools Edition

<p align="center"><img src="assets/logo.svg" width="760" alt="Plusalator logo"></p>

Fancy interactive C++17 calculator REPL. Standard library only, single binary.
Engine semantics faithfully modeled on Qalculate (qalc).

[![CI](https://github.com/Mark44928/Plusalator/actions/workflows/ci.yml/badge.svg)](https://github.com/Mark44928/Plusalator/actions/workflows/ci.yml)

![Plusalator — Qalculate-flavored C++17 REPL](docs/screenshot.png)

<img src="docs/screenshot-cli.png" width="640" alt="Plusalator CLI screenshot">

More terminal captures:

| Multi-function plot | Stats | Table |
|---|---|---|
| ![Plusalator plot](docs/screenshot-plot.png) | ![Plusalator stats](docs/screenshot-repl.png) | ![Plusalator table](docs/screenshot-table.png) |

```
⟨ plusa ⟩ ➤ 2+3*4
= 14
⟨ plusa ⟩ ➤ 200+10%
= 220
⟨ plusa ⟩ ➤ f(x)=x^2+1;f(3)
= 10
```

## Build & Install

```
make            # build plusalator
make check      # build + run tests/test.sh + syntax check (gate)
make banner     # regenerate src/banner.h from figlet
make clean
make install    # PREFIX-aware: $PREFIX on Termux, /usr/local on Linux by default
./install.sh    # or: fzf menu, backups, manifest, --uninstall
```

The installer defaults to `$PREFIX` on Termux and `~/.local` elsewhere; use
`--prefix DIR` to override, `--no-menu` for scripts, and `--uninstall` to
remove the recorded binary.

**Requirements:**
- C++17 compiler (`clang++` or `g++`)
- `figlet` (only for `make banner`)
  - Termux: `pkg install figlet`
  - Linux: your distro's figlet package

**Paths:**
- Install prefix: honors `$PREFIX` (Termux sets it automatically), otherwise `/usr/local` on Linux
- Temp files: honor `$TMPDIR` (Termux sets it), otherwise `/tmp` on Linux
- History: always saved to `$HOME/.plusalator_history` (capped at 200 entries)
- Config: theme persistence at `$HOME/.config/plusalator/config`

## Engine (Qalculate semantics)

- `%` is postfix **percent** (`50% = 0.5`, `200+10% = 220`). Modulo is `mod(x,y)`.
- `!` postfix factorial (`5! = 120`). `^` right-associative, binds tighter than unary
  (`-2^2 = -4`, `2^3^2 = 512`).
- Implicit multiplication: `2x`, `2(3+4)`, `(2+3)(4+5)`, `2sin(pi/2)`, `3pi`, `x(3)`, `-2x`.
  Never between two numbers (`"2 3"` is a syntax error with a caret).
- Number literals: `0x` hex, `0b` binary, `0o` octal.
- Built-ins (radians by default; `deg`/`rad`/`grad` switch the unit for trig and inverse trig): `sqrt`, `cbrt`, `sin`, `cos`, `tan`, `cot`, `csc`, `sec`, `asin`, `acos`, `atan`,
  `asinh`, `acosh`, `atanh`, `log`, `log2`, `ln`, `exp`, `abs`, `floor`, `ceil`, `round`, `sign`,
  `isprime(x)` (1 if prime), `nextprime(n)`, `prevprime(n)` (the primes strictly after/before n),
  `min`, `max`, `clamp(x, lo, hi)`, `gcd`, `lcm`, `pow(b,e)`, `mod(x,y)`, `hypot(a,b)`,
  `ncr(n,r)` (combinations), `npr(n,r)` (permutations),
  `fact(n)` (factorial as a function form of `!`).
- Constants, grouped and listed by `constants`: geometry (`pi`, `tau`, `phi`), mechanics (`grav`, `G`), thermodynamics (`kB`, `R`, `NA`, `c`, `h`, `hbar`, `sigma`), electromagnetism (`eps0`, `mu0`), atom/masses (`u`, `m_e`, `m_p`, `e`), and `ans` = last result.
- Assignment `x=5`; user functions `f(x)=x^2+1` (body cloned once at definition, depth cap 64,
  graceful "recursion too deep", never a stack crash).
- Errors print a caret under the offending token, exit code 0, never crash/hang (fuzz-gated).

On startup, a dim tip reminds you to type `help` for commands. Type any expression to
calculate immediately. The REPL supports raw-mode line editing on TTYs with full
keyboard navigation, history, and ghost autosuggest.

## Quick commands (no math)

```bash
primes [n]           # print every prime <= n (default 100, capped at 1000)
factorize 360        # or `factors` — prints 360 = 2 × 2 × 2 × 3 × 3 × 5
```

## REPL

- **Raw-mode line editor** on TTYs with full keyboard support; `getline` fallback when stdin is not a TTY
- **Keys:**
  - Navigation: Left/Right/Home/End, Ctrl+A (beginning), Ctrl+E (end)
  - Editing: Backspace/Delete, Ctrl+U (delete to start), Ctrl+K (delete to end)
  - Control: Ctrl+C (clears buffer, never exits), Ctrl+D (quits on empty buffer), Ctrl+L (clear screen + redraw the prompt)
  - Autocomplete: Ctrl+F (accept ghost suggestion)
- **Prefix history:** Up/Down cycle history entries starting with the current buffer prefix (empty buffer = full history)
- **Ghost autosuggest** (fish-style): dim history suffix after the cursor; accept with Right (at end) or Ctrl+F
- **Bracketed paste:** inserted literally, never auto-executes
- **Transient prompt:** full ornament `⟨ plusa ⟩ ➤` on the active line only; after Enter the line redraws
  dim `⟨ ·· ⟩ ➤ <input>`; Ctrl+C clear shows a dim transient prompt with a short dim rule above

## Prompt & Visuals

**NEW:** Completely revamped visual experience with 13 additional modern themes inspired by popular color schemes (Nord, Catppuccin, Tokyo Night, Dracula, Gruvbox, Solarized, Cyberpunk, Matrix, Sunset, Aurora, Neon, Retro, Yellen) — 20 in total including Greenit, Bluney and the classics. The banner now features a sophisticated multi-gradient color transition, and the status line shows both variable and function counts for better context.

- **Line 1:** full-width `─` gradient rule (hint→accent→hint) with right-side dim status
  showing `theme · N vars · M funcs`, printed each cycle
- **Active prompt:** `⟨ plusa ⟩ ➤` — U+27E8/27E9 brackets in accent color, `plusa` in hint
  italic, U+27A9 arrow in accent+bold; flashes rose once on error
- **Result:** `= <value>`, bold number, 12 significant figures, trailing zeros trimmed, integers bare
- **Error:** caret line under the offending token; next prompt glyph flashes rose once
- **Banner:** embedded figlet output, rows colored with a top→bottom gradient at runtime
- **Themes:** 19 built-in truecolor themes (6 hex colors each: prompt/result/error/hint/rule/accent):
  - **greenit** (default) — vibrant green on dark background
  - **mono** — clean monochrome grayscale
  - **ocean** — bright blue/cyan palette
  - **ember** — warm orange/red tones
  - **sakura** — soft pink/purple aesthetic
  - **midnight** — deep blue/purple night theme
  - **nord** — Arctic Ice Studio frost palette, low saturation, high readability
  - **catppuccin** — pastel colors, cozy aesthetic
  - **tokyonight** — dark theme with vibrant blue/purple accents
  - **dracula** — popular dark theme with purple accent
  - **gruvbox** — retro groove with warm earth tones
  - **solarized** — scientific, colorblind-friendly palette
  - **cyberpunk** — neon aesthetic with cyan/blue glow
  - **matrix** — classic green-on-black hacker look
  - **sunset** — warm orange/pink gradient
  - **aurora** — purple/blue/pink palette
  - **neon** — bright glowing colors
  - **retro** — CRT monitor aesthetic (amber on dark)
  - **bluney** — calm blue family theme
- **Theme commands:** `theme` lists themes with color swatches; `settings` provides interactive
  menu to switch theme and saves to `$HOME/.config/plusalator/config` for persistence
- **Color control:** `NO_COLOR` environment variable or `TERM=dumb` strips all ANSI formatting

## Commands

| Command | Description |
|---------|-------------|
| `<expr>` | Evaluate an expression |
| `help` | Show help box (boxed two-column layout) |
| `vars` | List all variables and their values |
| `funcs` | List user-defined functions with signatures |
| `history` | Show command history |
| `export vars\|history [path]` | **new** — write variables or history to CSV |
| `plot <f(x)>[,<g(x)>] [xmin xmax]` | **new** — ASCII plot; comma overlays up to 3 curves |
| `table <f(x)> [xmin xmax [step]]` | **new** — evaluate f(x) over a range, tabulated |
| `bench <expr> [n]` | **new** — time n evaluations, µs/eval + total |
| `stats <n...>` | count/min/max/sum/mean/median/stddev |
| `units <v> <from> <to>` | **new** — convert length, mass, speed, volume, area, data, time, energy, pressure, angle and temperature |
| `clear` | Clear screen and re-banner |
| `theme` | List available themes with color swatches |
| `settings` | Interactive settings menu (switch theme) |
| `ans` | Show last answer |
| `ms <expr>` | **new** — store an expression into memory |
| `mr` | **new** — recall memory (feeds `ans` so it chains) |
| `m+ <expr>` / `m- <expr>` | **new** — add / subtract from memory |
| `mc` | **new** — clear memory |
| `deg` / `rad` / `grad` | **new** — angle unit for trig and inverse trig |
| `mode <dec\|hex\|bin\|sci>` | **new** — output radix (hex/bin apply to integers) |
| `constants` | List built-in constants (pi, tau, e, phi) |
| `reset` | Clear user variables and functions |
| `del <name>` | Delete a variable or function |
| `config` | Show config path and active theme |
| `version` / `about` | Show version and build info |
| `quit` / `exit` / `q` | Exit the REPL |

All commands are case-sensitive. Assigning over a built-in constant (`pi=5`)
is rejected — built-in definitions win over user assignment.

### v1.2.0 notes

- Memory, angle unit and output radix persist in `~/.config/plusalator/config`
  and are restored on the next launch.
- `mode hex` / `mode bin` format integers as `0xff` / `0b101`; non-integers
  always stay decimal so no precision is lost. `mode sci` switches every result
  to scientific notation.
- The status line shows the active angle unit (`· deg`), radix (`· hex`) and
  memory presence (`· M`) next to the theme name.
- A no-argument command followed by more text is evaluated as an expression:
  `ans * 2` multiplies the last answer instead of being swallowed by `ans`.
- Bare-number display law enforced: results never leak exponent notation for
  readable magnitudes (`0.000001`, not `1e-06`), integers print in full
  (`100000000000000000000`, not `1e+20`), and `-0` prints as `0`.

### v1.6.0 notes

- `units 5 km mi` converts between common units, with temperature handled separately; `units 1 km kg` refuses to mix categories instead of returning a nonsense number.

### v1.5.0 notes

- Stats distributions and small-matrix helpers are built in: `normpdf`/`normcdf`, `binompdf`/`binomcdf`, `poissonpdf`/`poissoncdf`, `erf`, `logistic`, and `det2`/`det3`.
- Number theory and integer utilities grew with `divcount`, `totient`, `modpow`, `floordiv`, and `ceildiv`.

### v1.4.0 notes

- `--json --eval` produces a stable scripting object: `{"expression":"…","result":<number>}`.
- Evaluation errors in JSON mode return `{"expression":"…","error":"…"}` instead of a caret block, with JSON string escaping for quotes and control characters.
- The JSON result is always decimal, independent of the persisted `mode hex/bin/sci` setting.

## Command-line Usage

### `--eval` mode

Headless evaluation for scripting:
```bash
plusalator --eval "2+3*4"        # prints: 14
plusalator --eval "sqrt(16)"     # prints: 4
plusalator --eval "1/0"          # prints error with caret, exit code 0
plusalator --json --eval "2+3*4" # prints: {"expression":"2+3*4","result":14}
plusalator --eval "1/0" --json   # prints: {"expression":"1/0","error":"division by zero"}
```

**Flags:**
- `--eval <expr>`: evaluate expression and print bare result (no prompt/banner/color)
- `--json`: with `--eval`, emit a machine-readable JSON object; errors keep the same shape with an `error` field
- `--version`: show version
- `--help`: show usage

**Environment variables:**
- `FORCE_COLOR=1`: force ANSI color output in `--eval` mode
- `NO_COLOR`: disable all ANSI color output
- `TERM=dumb`: disable all ANSI formatting
- `PREFIX`: installation prefix (Termux sets automatically)
- `TMPDIR`: temporary directory (Termux sets automatically)

All modes exit with code 0, even on calculation errors.

## File layout

- `src/calc.h` / `src/calc.cpp` — tokenizer, AST, parser, evaluator
- `src/lineedit.h` / `src/lineedit.cpp` — raw-mode line editor
- `src/banner.h` — embedded figlet output (dev-generated via `make banner`)
- `src/main.cpp` — REPL loop, commands, prompt, themes
- `Makefile`, `tests/test.sh`, `README.md`, `LICENSE`

## TTY limits (honest)

The line editor, ghost autosuggest, transient prompt, and bracketed paste require a
TTY and are not testable via `--eval`. When stdin is not a TTY, `getline` is used
instead, so history/editing features are unavailable. UTF-8 cursor movement is by byte
(adequate for ASCII calc input); very long lines do not wrap. Screen size is detected
via `TIOCGWINSZ` (falls back to 80 columns).


## 🛠️ More from Mark44928

Tiny, dependency-free tools from the same family:

- [Pythonator](https://github.com/Mark44928/Pythonator) — the Python rewrite of Plusalator: same calculator semantics, pure standard-library
- [Solvamath](https://github.com/Mark44928/Solvamath) — terminal math puzzle game — seven puzzle types, daily seed, achievements
- [Bluney](https://github.com/Mark44928/Bluney) — blue-injected theme family — dark + light, single palette.json
- [Fluffless Prompt](https://github.com/Mark44928/FlufflessPrompt) — quiet Oh My Zsh theme — Git state, exit codes, clock
- [Weatui](https://github.com/Mark44928/Weatui) — friendly Open-Meteo weather TUI — saved cities, forecasts, no API key
- [Yellen](https://github.com/Mark44928/Yellen) — golden-yellow theme family — dark + light, single palette.json
- [Mark44928](https://github.com/Mark44928) — profile and project index
- [plusafetch](https://github.com/Mark44928/plusafetch) — C++23 fetch for Termux — ~23 ms, zero subprocesses on the default path
- [GrubBlastersHTML](https://github.com/Mark44928/GrubBlastersHTML) — Turn-based artillery in one HTML file — 73 weapons, fully destructible terrain
- [SnakTUI](https://github.com/Mark44928/SnakTUI) — Snake game for the terminal — Textual + Python
- [Easy-Termux-Package-Manager](https://github.com/Mark44928/Easy-Termux-Package-Manager) — fzf-driven package manager for Termux — snapshots, audit, 194 tests
- [NoNameOS](https://github.com/Mark44928/NoNameOS) — OS simulation in a single C++17 file — virtual FS, real shell
- [Greenit](https://github.com/Mark44928/Greenit) — green-injected theme family — dark + light, single palette.json
- [CalculaHTML](https://github.com/Mark44928/CalculaHTML) — static HTML calculator — 16 themes + System mode, zero deps
- [android12-fastfetch](https://github.com/Mark44928/android12-fastfetch) — fastfetch config for Android 12 + native X11 Termux desktops
- [debloat-android-tv](https://github.com/Mark44928/debloat-android-tv) — ADB debloat for rooted Android 9+ TV sticks & boxes
- [Android9-audios](https://github.com/Mark44928/Android9-audios) — 217 stock Android 9 system sounds + per-file SHA-256

## License

MIT. See `LICENSE`.

---

<div align="center">
<img width="100%" src="https://api.star-history.com/svg?repos=Mark44928/Plusalator&amp;type=Date" alt="Star history" />
</div>

---

**More projects:** [Mark44928's hub](https://mark44928.github.io/Mark44928/) — a family of dependency-free tools for Termux, Linux, and the terminal.
