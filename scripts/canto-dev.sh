#!/usr/bin/env bash
#
# canto-dev.sh — local dev/build helper for the Canto Cross firmware fork.
#
# Thin wrapper over the repo's existing tooling (pio, gen_i18n.py,
# bin/clang-format-fix, debugging_monitor.py, the CMake test tree). It does not
# reimplement any of that — it just sequences the common local loops.
#
# Usage: scripts/canto-dev.sh <command>
#
#   check   verify prerequisites (pio, clang-format>=21, python3, submodule)
#   build   regenerate i18n, then `pio run -e default`
#   flash   build, then `pio run -e default -t upload`, then serial monitor
#   test    native CMake/ctest suite (test/)
#   fmt     ./bin/clang-format-fix -g  (git-modified files only)
#   all     check, fmt, test, build
#
set -euo pipefail

# Resolve repo root from this script's location so it runs from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

PIO_ENV="default"
I18N_TRANSLATIONS="lib/I18n/translations"
I18N_OUT="lib/I18n/"

die()  { printf 'canto-dev: error: %s\n' "$*" >&2; exit 1; }
info() { printf 'canto-dev: %s\n' "$*"; }

# --- check -----------------------------------------------------------------
# Reports every missing prerequisite (does not stop at the first) so one run
# tells you everything to fix. Exits non-zero if anything is missing.
cmd_check() {
  local missing=0

  if command -v pio >/dev/null 2>&1; then
    info "pio: $(pio --version 2>/dev/null)"
  else
    printf 'canto-dev: MISSING pio — install with: pipx install platformio (or pip install platformio)\n' >&2
    missing=1
  fi

  if command -v clang-format >/dev/null 2>&1; then
    local cf_ver cf_major
    cf_ver="$(clang-format --version)"
    cf_major="$(printf '%s\n' "${cf_ver}" | grep -oE '[0-9]+' | head -n1)"
    if [ -n "${cf_major}" ] && [ "${cf_major}" -ge 21 ]; then
      info "clang-format: ${cf_ver}"
    else
      printf 'canto-dev: MISSING clang-format>=21 — found: %s (.clang-format needs 21+)\n' "${cf_ver}" >&2
      missing=1
    fi
  else
    printf 'canto-dev: MISSING clang-format — install with: pipx install "clang-format>=21,<22"\n' >&2
    missing=1
  fi

  if command -v python3 >/dev/null 2>&1; then
    info "python3: $(python3 --version 2>&1)"
  else
    printf 'canto-dev: MISSING python3\n' >&2
    missing=1
  fi

  # Submodule initialized when `git submodule status` does not prefix it with '-'.
  if [ -f freeink-sdk/.git ] || [ -d freeink-sdk/.git ] || \
     ! git submodule status freeink-sdk 2>/dev/null | grep -q '^-'; then
    if [ -e freeink-sdk/library.json ] || [ -n "$(ls -A freeink-sdk 2>/dev/null)" ]; then
      info "submodule freeink-sdk: initialized"
    else
      printf 'canto-dev: MISSING submodule — run: git submodule update --init\n' >&2
      missing=1
    fi
  else
    printf 'canto-dev: MISSING submodule freeink-sdk — run: git submodule update --init\n' >&2
    missing=1
  fi

  [ "${missing}" -eq 0 ] || die "prerequisites missing (see above)"
  info "check OK"
}

# --- i18n + build ----------------------------------------------------------
gen_i18n() {
  info "regenerating i18n from ${I18N_TRANSLATIONS}"
  python3 scripts/gen_i18n.py "${I18N_TRANSLATIONS}" "${I18N_OUT}"
}

cmd_build() {
  gen_i18n
  info "pio run -e ${PIO_ENV}"
  pio run -e "${PIO_ENV}"
}

cmd_flash() {
  cmd_build
  info "pio run -e ${PIO_ENV} -t upload"
  pio run -e "${PIO_ENV}" -t upload
  info "launching serial monitor (Ctrl-C to exit)"
  python3 scripts/debugging_monitor.py
}

# --- native tests ----------------------------------------------------------
cmd_test() {
  info "configuring + building native test suite"
  cmake -B build-tests -S test
  cmake --build build-tests -j4
  info "ctest"
  ctest --test-dir build-tests --output-on-failure
}

# --- formatting ------------------------------------------------------------
cmd_fmt() {
  info "clang-format (git-modified files)"
  ./bin/clang-format-fix -g
}

# --- all -------------------------------------------------------------------
cmd_all() {
  cmd_check
  cmd_fmt
  cmd_test
  cmd_build
  info "all OK"
}

usage() {
  # Print the leading comment block (skip the shebang, stop at first non-# line).
  awk 'NR==1 {next} /^#/ {sub(/^# ?/, ""); print; next} {exit}' "${BASH_SOURCE[0]}"
}

main() {
  local cmd="${1:-}"
  case "${cmd}" in
    check) cmd_check ;;
    build) cmd_build ;;
    flash) cmd_flash ;;
    test)  cmd_test  ;;
    fmt)   cmd_fmt   ;;
    all)   cmd_all   ;;
    ""|-h|--help|help) usage ;;
    *) die "unknown command '${cmd}' (try: check build flash test fmt all)" ;;
  esac
}

main "$@"
