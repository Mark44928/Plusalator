# M0 — Step Zero: Qalculate/qalc Semantics Research

Sources consulted:
- Qalculate! Manual, Chapter 4 (Expressions): https://qalculate.github.io/manual/qalculate-expressions.html
- QALC man page: https://qalculate.github.io/manual/qalc.html
- Qalculate! Manual single page: https://qalculate.github.io/manual/singlepage.html
- Qalculate! Manual, Chapter 5 (Calculator Modes): https://qalculate.github.io/manual/qalculate-mode.html

## Findings

### 1. `%` operator — remainder in Qalculate, POSTFIX PERCENT in Plusalator
Qalculate defines `%` / `rem` as "Remainder ... Returns the remainder after (truncated) division" (e.g. 3%2 = 1), and `%%` / `mod` as floored-modulo. Plusalator deviates deliberately per spec: `%` is a **postfix percent** (50% = 0.5, 200+10% = 220), and modulo is only the function `mod(x,y)`. Plusalator never uses C-style remainder on `%`.

### 2. Number literals
Qalculate accepts `0b` (binary), `0o` (octal), `0x` (hex), `0d` (decimal) prefixes (e.g. `0x3f = 63`). Plusalator implements `0x`, `0b`, `0o` via strtod-style parsing in the tokenizer.

### 3. Implicit multiplication
"The multiplication sign can generally be left out... This is not true for numbers (`5(5) = 25` but `5 5 = 55`)." Plusalator inserts implicit multiplication in the parser for value-before-value adjacency (number×ident, number×`(`, `)`×number/ident, ident×`(`, etc.), bans number-number (`2 3` = syntax error with caret), and applies it after unary too (`-2x = -(2x)`).

### 4. Factorial and exponentiation
`!` is postfix factorial (5! = 120). `^` is right-associative exponentiation (`2^3^2 = 512`) and binds tighter than unary (`-2^2 = -4`).

### 5. `ans` semantics
"The `ans` variable always contains the most recent result." Plusalator maintains `ans` updated after every successful evaluation.

### 6. Trigonometric angle defaults
Default angle unit is radians (Qalculate `angle = 1*` radians). `sin(pi/2) - cos(pi) = 2`. Plusalator built-ins operate in radians.

### Deviations from Qalculate (intentional, per spec)
- `%` = postfix percent (Qalculate: remainder).
- `mod(x,y)` function only for modulo (Qalculate also has `%%` operator).
- No units, no intervals, no complex numbers, no equation solving.
