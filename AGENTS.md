# Plusalator — Agent Instructions
Fancy interactive C++17 calculator REPL, standard library only, single binary.
Engine semantics modeled on Qalculate (qcalc/qalc). Parses/evaluates in src/calc.h/calc.cpp,
raw-mode line editor in src/lineedit.{h,cpp}, REPL/theme/prompt in src/main.cpp,
embedded figlet banner in src/banner.h.

# Commands
- Build/run/gate: `make` (build plusalator), `make run` (run with TMPDIR), `make check`
  (`tests/test.sh` ≈88 engine/fuzz assertions + `clang++ -fsyntax-only src/calc.cpp src/main.cpp`),
  `make clean`, `make install` (honors $PREFIX; Termux $PREFIX auto-set), `make banner`
  (regenerate src/banner.h via figlet).
- Single expression headlessly: `./plusalator --eval "<expr>"` (bare output, exit 0 even on
  error; no prompt/banner/color unless FORCE_COLOR=1).
- Commit-per-milestone: every behavior change lands with its test and `make check` green
  before committing.

# Engine laws
- `%` = postfix percent (50% = 0.5); modulo = mod(x,y) — NEVER C-style remainder.
- `^` right-assoc, binds tighter than unary (-2^2=-4, 2^3^2=512).
- Implicit multiplication inserted by parser (2x, 2(3+4), (2+3)(4+5), 2sin(pi/2),
  x(3), -2x = -(2x)); number-number ban ("2 3" = syntax error with caret).
- User functions: cloned AST per call (Expr::clone), callDepth cap 64, graceful
  "recursion too deep", never a stack crash.
- NEVER crash/hang — fuzz-gate (`tests/test.sh` fuzz loop) before any milestone claim.
- `--eval` output = machine-readable bare lines only (number or caret+error).

# REPL laws
- Ctrl+C NEVER exits (clears buffer); Ctrl+D exits only on empty buffer.
- Ghost autosuggest: dim history suffix after cursor; accept with Right or Ctrl+F.
- Bracketed paste inserts literally, never auto-executes.
- Transient prompt: full ornament ⟨ plusa ⟩ ➤ on active line only; executed lines redraw
  dim `⟨ ·· ⟩ ➤ <input>`; Ctrl+C clear shows dim transient prompt with short dim rule above.
- Commands: `help` `vars` `funcs` `history` `clear` `theme [name]` `ans` (last answer)
  `constants` (built-ins) `reset` (clear user vars/funcs) `del <name>` (remove var/func)
  `quit`/`exit`/`q`.

# Visual laws
- NO hand-drawn ASCII art — banner is embedded figlet output (src/banner.h), runtime
  only colors it top→bottom gradient; regenerate only via `make banner`.
- NO_COLOR / TERM=dumb strip all formatting (isatty required for color).
- Prompt ornament glyphs are standard BMP (U+27E8/27E9 ⟨⟩, U+27A9 ➤, U+00B7 ·),
  never nerd-font PUA codepoints.
- Right-side dim status on line 1: theme · N vars; themes: greenit (default), mono,
  ocean, ember, sakura; `theme <name>` switches theme and persists it to
  `$HOME/.config/plusalator/config` across sessions.

# Examples
- ✅ Correct: changing a builtin's behavior → edit src/calc.cpp → add/update assertion in
  tests/test.sh → `make check` → commit.
- ❌ Wrong: hand-editing banner.h letters (regenerate via figlet), skipping `make check`,
  hand-drawing ASCII art.

# Completion report (required end of every task)
Files changed + diff hunks · `make check` transcript · what could not be verified and why
