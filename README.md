# Plusalator

Fancy interactive C++17 calculator REPL for Termux. Standard library only,
single binary. Engine semantics faithfully modeled on Qalculate (qalc).

```
⟨ plusa ⟩ ➤ 2+3*4
= 14
⟨ plusa ⟩ ➤ 200+10%
= 220
⟨ plusa ⟩ ➤ f(x)=x^2+1;f(3)
= 10
```

## Build

```
make            # build plusalator
make check      # build + run tests/test.sh + syntax check (gate)
make banner     # regenerate src/banner.h from figlet
make clean
make install    # PREFIX-aware: $PREFIX on Termux, /usr/local on Linux by default
```

Requires: a C++17 compiler (`clang++` or `g++`); `figlet` only for `make banner`
( Termux: `pkg install figlet` · Linux: your distro's figlet package ).

Install prefix: honors `$PREFIX` when set (Termux sets it automatically); otherwise
defaults to `/usr/local` on Linux. Temp files honor `$TMPDIR` (Termux sets it);
otherwise `/tmp` on Linux. History is always `$HOME/.plusalator_history`.

## Engine (Qalculate semantics)

- `%` is postfix **percent** (`50% = 0.5`, `200+10% = 220`). Modulo is `mod(x,y)`.
- `!` postfix factorial (`5! = 120`). `^` right-associative, binds tighter than unary
  (`-2^2 = -4`, `2^3^2 = 512`).
- Implicit multiplication inserted by the parser: `2x`, `2(3+4)`, `(2+3)(4+5)`,
  `2sin(pi/2)`, `3pi`, `x(3)` (var × arg), `-2x = -(2x)`. Never between two numbers
  (`"2 3"` is a syntax error with a caret).
- Number literals: `0x` hex, `0b` binary, `0o` octal.
- Built-ins (radians): `sqrt sin cos tan asin acos atan log ln exp abs floor ceil
  round min(...) max(...) hypot(...) pow(b,e) mod(x,y)`.
- Constants: `pi`, `tau`, `e`, `phi`. `ans` = last result.
- Assignment `x=5`; user functions `f(x)=x^2+1` (cloned AST per call, depth cap 64,
  graceful "recursion too deep", never a stack crash).
- Errors print a caret under the offending token, exit code 0, never crash/hang
  (fuzz-gated).

On startup, a dim hint reminds you to type `help` for commands. Type any
expression to calculate immediately.

## REPL

- Raw-mode line editor on TTYs; `getline` fallback when stdin is not a TTY.
- Keys: insert at cursor, Left/Right/Home/End, Backspace/Delete, Ctrl+A/E, Ctrl+U/K,
  Ctrl+C (clears buffer, never exits), Ctrl+D (quits on empty buffer), Ctrl+L (clear +
  re-banner).
- Prefix history: Up/Down cycle history entries starting with the buffer (empty buffer
  = full history). Persisted to `~/.plusalator_history` (cap 200).
- Ghost autosuggest (fish-style): dim history suffix after the cursor; accept with
  Right (at end) or Ctrl+F.
- Bracketed paste inserted literally, never auto-executes.
- Transient prompt: full ornament `⟨ plusa ⟩ ➤` on the active line only; after Enter
  the line redraws dim `⟨ ·· ⟩ ➤ <input>`; Ctrl+C clear shows a dim transient prompt
  with a short dim rule above.

## Prompt & visuals

- LINE 1: full-width `─` gradient rule (hint→accent→hint) with right-side dim status
  `theme · N vars`, printed each cycle.
- Active prompt: `⟨ plusa ⟩ ➤` — U+27E8/27E9 brackets in accent, `plusa` in hint
  italic, U+27A9 arrow in accent+bold; flashes rose once on error.
- Result: `= <value>`, bold number, 10 sig figs, zeros trimmed, integers bare.
- Error: caret line under the offending token; next prompt glyph flashes rose once.
- Banner: embedded figlet output, rows colored with a top→bottom gradient at runtime.
- Themes (6 truecolor hexes each: prompt/result/error/hint/rule/accent):
  **greenit** (default), mono, ocean, ember, sakura.
- `theme` lists themes with swatches; `settings` switches theme and saves it to
  `$HOME/.config/plusalator/config` so it persists across sessions.
- `NO_COLOR` or `TERM=dumb` strips all formatting.

## Commands

`help` (boxed two-column) · `vars` · `funcs` (signatures) · `history` · `clear` ·
`settings` (interactive menu to switch theme) · `theme` (list themes) · `ans` (last answer)
· `constants` · `reset` (clear user state) · `del <name>` · `config` · `version` / `about` ·
`q`/`quit`/`exit`.

## --eval

`plusalator --eval "<expr>"` prints a bare number or a bare error line (no
prompt/banner/color unless `FORCE_COLOR=1`), exit code 0 even on error.

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

## License

MIT. See `LICENSE`.
