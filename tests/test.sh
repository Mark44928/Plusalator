#!/usr/bin/env bash
# tests/test.sh — Plusalator engine tests via --eval (bare output, no TTY).
# Exit code: 0 if all pass, non-zero otherwise.
set -euo pipefail

TMPDIR=/data/data/com.termux/files/usr/tmp
export TMPDIR
mkdir -p "$TMPDIR"

BIN="./plusalator"
PASS=0
FAIL=0

fail() { echo "FAIL: $*"; FAIL=$((FAIL + 1)); }

# Capture combined stdout/stderr of --eval (exit code ignored here).
eval_out() { "$BIN" --eval "$1" 2>&1 || true; }

assert_eq() { # <expr> <expected>
  local out; out=$(eval_out "$1")
  if [ "$out" = "$2" ]; then PASS=$((PASS + 1));
  else fail "$1 -> got '$out' expected '$2'"; fi
}

assert_approx() { # <expr> <expected>  (tolerance 1e-9)
  local out; out=$(eval_out "$1")
  local ok; ok=$(awk -v a="$out" -v b="$2" 'BEGIN{
    if (a=="nan"||a=="inf"||a=="-inf") {print 0; exit}
    print (a+0==a+0 && b+0==b+0 && (a-b)<1e-9 && (a-b)>-1e-9)?1:0}')
  if [ "$ok" = "1" ]; then PASS=$((PASS + 1));
  else fail "$1 -> got '$out' expected ~$2"; fi
}

assert_err() { # <expr>  (expects exit 0 and a caret line)
  local out rc=0
  out=$("$BIN" --eval "$1" 2>&1) || rc=$?
  if [ "$rc" != "0" ]; then fail "$1 -> exit $rc expected 0"; return; fi
  if ! printf '%s\n' "$out" | grep -qF '^'; then fail "$1 -> no caret in output"; return; fi
  PASS=$((PASS + 1))
}

echo "== engine (numeric) =="
assert_eq "2+3*4" "14"
assert_eq "(2+3)*4" "20"
assert_eq "2^3^2" "512"
assert_eq "-2^2" "-4"
assert_eq "10-4" "6"
assert_approx "sqrt(2)" "1.4142135623730951"
assert_approx "pi*2" "6.283185307179586"
assert_approx "e^2" "7.38905609893"
assert_eq "floor(2.9)" "2"
assert_eq "ceil(2.1)" "3"
assert_eq "round(2.5)" "3"
assert_eq "abs(-5)" "5"
assert_eq "min(3,1,2)" "1"
assert_eq "max(3,1,2)" "3"
assert_eq "hypot(3,4)" "5"
assert_eq "pow(2,10)" "1024"
assert_eq "mod(10,3)" "1"
assert_eq "50%" "0.5"
assert_eq "200+10%" "220"
assert_eq "5!" "120"
assert_eq "0x10" "16"
assert_eq "0b101" "5"
assert_eq "0o17" "15"
assert_eq "x=5;x*2" "10"
assert_eq "x=5;2x" "10"
assert_eq "x=5;x(3)" "15"
assert_eq "x=5;-2x" "-10"
assert_eq "2(3+4)" "14"
assert_eq "(2+3)(4+5)" "45"
assert_eq "2sin(pi/2)" "2"
assert_approx "3pi" "9.42477796077"
assert_eq "f(x)=x^2+1;f(3)" "10"
assert_eq "g(a,b)=a*b+1;g(2,3)" "7"

echo "== errors (exit 0, caret present) =="
assert_err "1/0"
assert_err "1+*2"
assert_err "2 3"
assert_err "zzz+1"
assert_err "f(x)=f(x)+1;f(1)"

echo "== fuzz: 50 random inputs must terminate with exit 0 =="
RANDOM_CHARS="+-*/^%!()0123456789.abcxyzsinco"
for i in $(seq 1 50); do
  soup=""
  n=$(( (RANDOM % 40) + 1 ))
  for j in $(seq 1 $n); do
    soup="${soup}${RANDOM_CHARS:$((RANDOM % ${#RANDOM_CHARS})):1}"
  done
  if timeout 1 "$BIN" --eval "$soup" >/dev/null 2>&1; then
    rc=$?
    if [ "$rc" != "0" ]; then fail "fuzz '$soup' -> exit $rc"; else PASS=$((PASS + 1)); fi
  else
    fail "fuzz '$soup' -> did not terminate or crashed"
  fi
done

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" = "0" ]
