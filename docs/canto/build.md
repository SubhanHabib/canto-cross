# Canto Cross — Build Process

Everything needed to build, check, and verify this fork. Nothing here is Canto-specific except where marked — this is upstream CrossPoint's process, written down in one place.

## Prerequisites

```bash
git submodule update --init          # freeink-sdk (SDK libs are symlinked from platformio.ini)
pip install platformio               # pio CLI (or use the VS Code extension)
pip install "clang-format>=21,<22"   # the repo's .clang-format needs clang-format 21+
```

## Firmware builds

PlatformIO environments (from `platformio.ini`):

| Environment | Purpose |
| --- | --- |
| `default` | Development — X3/X4, LOG_LEVEL=2, serial on |
| `gh_release` | Production (LOG_LEVEL=0) |
| `gh_release_rc` | Release candidate (LOG_LEVEL=1) |
| `slim` | Minimal — serial logging off to save flash |
| `sticky` | ESP32-S3 Seeed touch device |

```bash
pio run -e default                   # the everyday build
pio run -e slim && pio run -e sticky # run both before merging — CI builds all
pio run -t upload                    # flash the device
python3 scripts/debugging_monitor.py # serial monitor (color-coded)
```

**Cloud dev environment caveat:** in the remote (Claude Code) container, the ESP32 toolchain download from GitHub releases is blocked by the egress policy, so `pio run` cannot work there. The compile check is **CI on a pull request** (see below). Native tests, formatting, and i18n generation all work locally.

## Generated files — never edit by hand

- **i18n**: source of truth is `lib/I18n/translations/*.yaml` (`english.yaml` is the reference; other languages fall back per key, so new features add English keys only). Regenerate with:
  ```bash
  python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
  ```
  The three generated files (`I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`) are gitignored and regenerated at build time. The generator rejects comment lines in the YAML — keys only.
- **HTML**: `src/network/html/*.generated.h` come from `scripts/build_html.py` (a `pre:` build step). Edit the source HTML, not the headers.
- Also gitignored: `.pio/`, `compile_commands.json`, `platformio.local.ini`. Cross-check `git status` against `.gitignore` before staging.

## Formatting

```bash
./bin/clang-format-fix       # formats all tracked C/C++ files (needs clang-format 21+)
./bin/clang-format-fix -g    # only files modified in git status
```

CI fails the build if `clang-format-fix` would produce a diff. Run it before every commit.

## Native (host-side) tests

A CMake + googletest tree in `test/` covers the pure libraries (JSON parsers, OPDS filenames, hyphenation, bidi, …):

```bash
cmake -B build-tests -S test
cmake --build build-tests -j4
ctest --test-dir build-tests --output-on-failure
```

All 129 tests must pass. New pure logic should get a test target here; code that depends on Arduino/HAL can't be host-tested and relies on CI + hardware.

## CI

`.github/workflows/ci.yml` runs on **push to `master` and on pull requests** — a feature-branch push alone triggers nothing. It runs clang-format-21 and the PlatformIO builds. Practical consequence: **open a PR to get a compile check.** Fix CI before requesting review (upstream rule).

## Testing against a local Canto server

The device's server half is open-reader's Xteink surface. Full setup in `integration.md`; short version:

1. Run the open-reader web app with `NEXT_PUBLIC_APP_CHANNEL=dev` (turns the `xteinkSurface` flag on) and the Supabase service key set — without it `/api/xt/*` answers 501.
2. Apply migration `20260717000001_xteink_surface.sql`; mint device credentials via `POST /api/xt/credentials` with a signed-in session.
3. On the device, point the Canto server URL at the machine — use an explicit `http://` prefix for plain-HTTP LAN servers (bare hostnames default to `https://`).

## Hardware verification

The build isn't done until the device checklist in `milestones.md` passes: heap headroom (`ESP.getFreeHeap()` > 50 KB), all four orientations for any UI change, sleep/quick-resume regressions, and — for anything touching `lib/Epub/` or render settings — delete `/.crosspoint/` on the SD card and confirm a clean re-parse.
