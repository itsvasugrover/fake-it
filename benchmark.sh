#!/usr/bin/env bash
# benchmark.sh — compare fake-it (C++) vs fake-git-history (Node/bun)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$SCRIPT_DIR/build/Release/fake-it"

# ── Shared flags ───────────────────────────────────────────────────────────────
COMMON_ARGS=(
    --frequency 100
    --startDate "2025/01/01"
    --endDate   "2026/03/01"
    --commitsPerDay "4,7"
)
FAKEIT_EXTRA=(--name "Vasu Grover" --email "itsvasugrover@gmail.com")

# ── Colours ────────────────────────────────────────────────────────────────────
BOLD=$'\033[1m'
CYAN=$'\033[0;36m'
GREEN=$'\033[0;32m'
YELLOW=$'\033[0;33m'
RED=$'\033[0;31m'
RESET=$'\033[0m'

header() { printf "\n${BOLD}${CYAN}==> %s${RESET}\n" "$*"; }
info()   { printf "    ${YELLOW}%s${RESET}\n" "$*"; }
ok()     { printf "    ${GREEN}%s${RESET}\n" "$*"; }
err()    { printf "    ${RED}%s${RESET}\n" "$*" >&2; }

# ── 1. Check for bun ───────────────────────────────────────────────────────────
header "Checking requirements"

if ! command -v bun &>/dev/null; then
    err "bun is not installed or not in PATH."
    echo
    echo "  bun is required to run the 'bunx fake-git-history' side of the benchmark."
    echo "  Install it with:"
    echo
    echo "    curl -fsSL https://bun.sh/install | bash"
    echo
    echo "  Then re-run this script."
    exit 1
fi

ok "bun found: $(bun --version)"

# ── 2. Build fake-it if needed ────────────────────────────────────────────────
header "Checking fake-it binary"

if [[ -x "$BINARY" ]]; then
    ok "Binary already exists: $BINARY"
else
    info "Binary not found — building via build.sh …"
    cd "$SCRIPT_DIR"
    bash build.sh
    if [[ ! -x "$BINARY" ]]; then
        err "Build finished but binary not found at $BINARY"
        exit 1
    fi
    ok "Build succeeded."
fi

# ── 3. Temp workspace ─────────────────────────────────────────────────────────
header "Setting up temp workspace"

TMPDIR_ROOT="$(mktemp -d /tmp/fake-history-bench-XXXXXX)"
trap 'rm -rf "$TMPDIR_ROOT"' EXIT

FAKEIT_DIR="$TMPDIR_ROOT/bench-fakeit"
FAKEGH_DIR="$TMPDIR_ROOT/bench-fake-git-history"
mkdir -p "$FAKEIT_DIR" "$FAKEGH_DIR"

info "Temp directory: $TMPDIR_ROOT"

# ── helper: time a command and store elapsed seconds in a variable ────────────
# Usage: time_cmd VARNAME cmd [args...]
# The command's stdout/stderr is suppressed; VARNAME receives a float like "1.234".
time_cmd() {
    local _var="$1"; shift
    local _start _end
    _start=$(date +%s%N)
    "$@" >/dev/null 2>&1 || true
    _end=$(date +%s%N)
    printf -v "$_var" '%.3f' "$(awk "BEGIN { printf \"%.3f\", ($_end - $_start) / 1e9 }")"
}

# ── 4. Benchmark fake-it ──────────────────────────────────────────────────────
header "Running fake-it"

cd "$FAKEIT_DIR"
git init -q

info "Command: $BINARY ${COMMON_ARGS[*]} ${FAKEIT_EXTRA[*]} --dir ."
time_cmd FAKEIT_TIME "$BINARY" "${COMMON_ARGS[@]}" "${FAKEIT_EXTRA[@]}" --dir .
FAKEIT_TIME_DISPLAY="$FAKEIT_TIME s"

ok "fake-it finished in ${BOLD}$FAKEIT_TIME_DISPLAY${RESET}"

FAKEIT_COMMITS=$(git -C "$FAKEIT_DIR" rev-list --count HEAD 2>/dev/null || echo 0)
info "Commits created: $FAKEIT_COMMITS"

# ── 5. Benchmark fake-git-history (via bunx) ──────────────────────────────────
header "Running fake-git-history (bunx)"

# fake-git-history always creates a my-history/ subdir — run from FAKEGH_DIR
cd "$FAKEGH_DIR"

info "Command: bunx fake-git-history ${COMMON_ARGS[*]}"
time_cmd FAKEGH_TIME bunx fake-git-history "${COMMON_ARGS[@]}"
FAKEGH_TIME_DISPLAY="$FAKEGH_TIME s"

ok "fake-git-history finished in ${BOLD}$FAKEGH_TIME_DISPLAY${RESET}"

# The tool always writes into a my-history/ subdirectory
FAKEGH_COMMITS=$(git -C "$FAKEGH_DIR/my-history" rev-list --count HEAD 2>/dev/null || echo 0)
info "Commits created: $FAKEGH_COMMITS"

# ── 6. Results ────────────────────────────────────────────────────────────────
header "Results"

# Compute speedup (awk handles float division)
SPEEDUP=$(awk "BEGIN { if ($FAKEIT_TIME > 0) printf \"%.1f\", $FAKEGH_TIME / $FAKEIT_TIME; else print \"N/A\" }")

printf "\n"
printf "  %-30s %s\n" "Tool" "Time"
printf "  %-30s %s\n" "────────────────────────────" "──────────"
printf "  %-30s ${GREEN}${BOLD}%s${RESET}\n"    "fake-it (C++, libgit2)"     "$FAKEIT_TIME_DISPLAY"
printf "  %-30s %s\n"                            "fake-git-history (Node/bun)" "$FAKEGH_TIME_DISPLAY"
printf "\n"
printf "  ${BOLD}fake-it was ${GREEN}${SPEEDUP}×${RESET}${BOLD} faster${RESET}\n"
printf "\n"
