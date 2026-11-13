#!/usr/bin/env bash
# tests/test.sh — Plusalator engine tests via --eval (bare output, no TTY).
# Exit code: 0 if all pass, non-zero otherwise.
set -euo pipefail

TMPDIR=${TMPDIR:-/data/data/com.termux/files/usr/tmp}
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
  rc=0
  timeout 1 "$BIN" --eval "$soup" >/dev/null 2>&1 || rc=$?
  # 0 = evaluated, 1 = graceful error (exit 0 per the --eval contract);
  # anything else — including timeout's 124 — is a crash or a hang.
  if [ "$rc" != "0" ] && [ "$rc" != "1" ]; then fail "fuzz '$soup' -> exit $rc"; else PASS=$((PASS + 1)); fi
done

echo "== regression: a failed user-function call must not leak its stack frame =="
# Before the fix, a throw inside a user-function body permanently popped a
# frame off `locals`: later assignments vanished from vars and reset could
# not clear them.
repl_out=$(printf 'f(x)=sqrt(x-5)\nf(1)\nzz=5\nvars\nquit\n' | timeout 5 "$BIN" 2>&1 | sed 's/\x1b\[[0-9;]*[A-Za-z]//g')
if ! printf '%s\n' "$repl_out" | grep -q 'sqrt of negative number'; then
  fail "frame-leak: f(1) did not report the expected error"
elif printf '%s\n' "$repl_out" | grep -q '^zz = 5$'; then
  PASS=$((PASS + 1))
else
  fail "frame-leak: assignment after a failed call disappeared from vars"
fi

echo "== FORCE_COLOR opt-in for --eval =="
if FORCE_COLOR=1 "$BIN" --eval "1+1" 2>&1 | grep -q $'\033\[38;2;'; then PASS=$((PASS + 1)); else fail "FORCE_COLOR=1 did not colorize --eval output"; fi
if NO_COLOR=1 FORCE_COLOR=1 "$BIN" --eval "1+1" 2>&1 | grep -q $'\033\['; then fail "NO_COLOR lost to FORCE_COLOR"; else PASS=$((PASS + 1)); fi

echo "== v1.2.0: memory slots =="
# assert_repl <label> <input> <home-suffix> <expected-last-result>
assert_repl() {
  local got
  got=$(printf '%b' "$2" | HOME="$TMPDIR/plusa-$3" timeout 10 "$BIN" 2>&1 \
        | sed 's/\x1b\[[0-9;]*[A-Za-z]//g' | sed -n 's/^= //p' | tail -1)
  if [ "$got" = "$4" ]; then PASS=$((PASS + 1));
  else fail "$1 -> got '$got' expected '$4'"; fi
}
assert_repl "mr feeds ans"         'ms 42\nmr\nans * 2\nquit\n'   mem-b   "84"
mem_grep() { # <label> <input> <home-suffix> <needle>
  local out
  out=$(printf '%b' "$2" | HOME="$TMPDIR/plusa-$3" timeout 10 "$BIN" 2>&1 | sed 's/\x1b\[[0-9;]*[A-Za-z]//g')
  if printf '%s\n' "$out" | grep -qF "$4"; then PASS=$((PASS + 1));
  else fail "$1 -> missing '$4'"; fi
}
mem_grep  "ms stores"              'ms 42\nquit\n'               mem-a   "memory = 42"
mem_grep  "m+ adds"                'ms 42\nm+ 8\nquit\n'         mem-c   "memory = 50"
mem_grep  "m- subtracts"           'ms 42\nm- 10\nquit\n'        mem-d   "memory = 32"
repl_out=$(printf 'ms 42\nmc\nmr\nquit\n' | HOME="$TMPDIR/plusa-mem-e" timeout 10 "$BIN" 2>&1 | sed 's/\x1b\[[0-9;]*[A-Za-z]//g')
if printf '%s\n' "$repl_out" | grep -qF "memory is empty"; then PASS=$((PASS + 1));
else fail "mc/mr did not clear memory"; fi

echo "== v1.2.0: angle units =="
assert_repl "deg sin(30)"          'deg\nsin(30)\nquit\n'         angle-a "0.5"
assert_repl "grad sin(100)"        'grad\nsin(100)\nquit\n'       angle-b "1"
assert_repl "rad sin(pi/2)"        'rad\nsin(pi/2)\nquit\n'       angle-c "1"
assert_repl "deg asin(1)"          'deg\nasin(1)\nquit\n'         angle-d "90"

echo "== v1.2.0: output radix =="
assert_repl "mode hex"             'mode hex\n255\nquit\n'        radix-a "0xff"
assert_repl "mode bin"             'mode bin\n5\nquit\n'          radix-b "0b101"
assert_repl "mode sci"             'mode sci\n1234\nquit\n'       radix-c "1.234000e+03"
assert_repl "hex keeps decimals"   'mode hex\n1.5\nquit\n'        radix-d "1.5"
assert_repl "mode rejects junk"    'mode nope\n1\nquit\n'         radix-e "1"

echo "== v1.2.0: no-arg commands never swallow expressions =="
assert_repl "ans * 2 evaluates"    'ms 42\nmr\nans * 2\nquit\n'   swallow-a "84"
repl_out=$(printf 'vars + 1\nquit\n' | HOME="$TMPDIR/plusa-swallow-b" timeout 10 "$BIN" 2>&1 | sed 's/\x1b\[[0-9;]*[A-Za-z]//g')
if printf '%s\n' "$repl_out" | grep -qF "unknown variable: vars"; then PASS=$((PASS + 1));
else fail "vars + 1 was swallowed by the vars command instead of evaluating"; fi


echo "== regression: bare-number display law (no exponent leaks) =="
assert_eq "0.000001"            "0.000001"
assert_eq "100000000000000000000" "100000000000000000000"
assert_eq "1e-06"               "0.000001"
assert_eq "1.5e18"              "1500000000000000000"
assert_eq "-0"                  "0"
assert_eq "0.0000001"           "0.0000001"
assert_eq "1e-300"              "1e-300"     # too wide to expand: exponent stays

echo "== v1.3.0: new builtin math functions =="
assert_eq "cbrt(27)"          "3"
assert_eq "cbrt(-8)"          "-2"
assert_eq "isprime(97)"       "1"
assert_eq "isprime(1)"        "0"
assert_eq "isprime(0)"        "0"
assert_eq "isprime(100)"      "0"
assert_eq "nextprime(20)"     "23"
assert_eq "prevprime(100)"    "97"
assert_eq "nextprime(2)"      "3"
assert_eq "prevprime(3)"      "2"
assert_eq "nextprime(0.5)"    "2"
assert_eq "prevprime(4)"      "3"
assert_err "prevprime(2)"
assert_eq "fact(5)"           "120"
assert_eq "fact(4)"           "24"
assert_eq "gcd(12,18)"        "6"
assert_eq "lcm(4,6)"          "12"
assert_eq "clamp(5,3,7)"      "5"
assert_eq "clamp(2,3,7)"      "3"
assert_eq "clamp(9,3,7)"      "7"
assert_eq "sign(-3)"          "-1"
assert_eq "sign(0)"           "0"
assert_eq "sign(42)"          "1"
assert_eq "ncr(5,2)"          "10"
assert_eq "npr(5,2)"          "20"
assert_eq "log2(8)"           "3"
assert_eq "log2(1024)"        "10"
assert_approx "cot(pi/4)"     "1"
assert_approx "csc(pi/2)"     "1"
assert_approx "sec(pi)"       "-1"
assert_approx "asinh(0)"      "0"
assert_approx "acosh(1)"      "0"
assert_approx "atanh(0)"      "0"

echo "== persistent user vars survive restart =="
mkdir -p "$TMPDIR/plusa-persist"
HOME="$TMPDIR/plusa-persist" timeout 10 bash -c 'printf "x=42\ny=2*x+1\nquit\n" | "'"$BIN"'"' >/dev/null 2>&1
persisted=$(HOME="$TMPDIR/plusa-persist" timeout 10 bash -c 'printf "x\ny\nquit\n" | "'"$BIN"'"' 2>&1 | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' | grep -E "= ")
if printf '%s\n' "$persisted" | awk '/42/{found42=1; next} /85/{if(found42) passed=1} END{exit !passed}'; then
  PASS=$((PASS + 1))
else
  fail "persist user vars -> got: $persisted"
fi

echo "== v1.3.0: physical constants are baked in =="
assert_approx "G"             "6.6743e-11"
assert_eq "c"                 "299792458"
assert_approx "h"             "6.62607015e-34"
assert_approx "sigma"         "5.670374419e-8"
assert_approx "NA"            "6.02214076e23"
assert_approx "kB"            "1.380649e-23"
assert_approx "u"             "1.6605390666e-27"

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" = "0" ]
