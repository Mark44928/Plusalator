#!/data/data/com.termux/files/usr/bin/env bash
# Installer CI: install the built binary, rerun, then uninstall.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="${TMPDIR:-/tmp}/plusalator-install-smoke.$$"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/home" "$TMP/state" "$TMP/prefix"
export HOME="$TMP/home" XDG_STATE_HOME="$TMP/state"
bash "$ROOT/install.sh" --no-build --no-menu --prefix "$TMP/prefix" >/dev/null
"$TMP/prefix/bin/plusalator" --version >/dev/null
test -f "$XDG_STATE_HOME/plusalator/installed.tsv"
bash "$ROOT/install.sh" --no-build --no-menu --prefix "$TMP/prefix" >/dev/null
bash "$ROOT/install.sh" --uninstall --yes >/dev/null
test ! -e "$TMP/prefix/bin/plusalator"
test ! -e "$XDG_STATE_HOME/plusalator/installed.tsv"
echo "installer smoke OK"
