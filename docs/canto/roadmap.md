# Canto Cross — Roadmap

The ordered build. `milestones.md` holds the task-level detail per milestone; this lists *in what order and why*. Ordering rule: **each phase retires a specific risk, and you don't start one until the previous works.**

**Status at a glance (July 2026):** Phase 1 (M1–M7) ✅ code-complete on `claude/canto-reader-migration-keyq66` · Phase 2 (verification) 🔲 not started — no PR/CI build, no hardware pass · Phase 3 (server follow-ups, lives in open-reader) 🔲 · Phase 4 (deferred) — pull in on demand. "Code-complete" = committed with native tests green and formatting clean, **not** compile-verified for the ESP32 target and **not** device-verified.

---

## The ordering principle

Wire the two existing halves together before writing anything new: the firmware already speaks OPDS and kosync, the server already serves both. Phase 1 therefore built plumbing → settings → library → sync → landing screen → triage, each milestone independently buildable. Phase 2 is verification (cheap to run, expensive to skip). Phase 3 is the one genuinely new capability (triage) on the server side. Phase 4 is everything that needs real usage to justify.

---

## Phase 1 — The firmware layer  *(retires: "can the device be a real Canto client?")*  ✅

Seven milestones, each an independently buildable commit (details in `milestones.md`):

- **M1** `CantoStore` — server config + credentials + article index, web-settings exposure ✅
- **M2** `KOReaderSyncClient` explicit-account refactor (behavior-neutral) ✅
- **M3** Canto settings + connection test activities ✅
- **M4** `CantoLibraryActivity` — browse/search/download/open via `/api/xt/opds` ✅
- **M5** Headless progress sync — pull-on-open, deferred push, no reader hooks ✅
- **M6** `CantoHomeActivity` + Canto-first boot routing ✅
- **M7** Triage popup + `CantoApiClient` + the layer docs ✅

## Phase 2 — Verification  *(retires: "does it actually build and run?")*  🔲

In order, cheapest first:

1. **CI build** — open a PR from the feature branch; CI runs clang-format-21 and the PlatformIO builds (`default`, `slim`, `sticky`). Local firmware builds are impossible in the cloud dev environment (toolchain download blocked) — CI is the only compile check. Fix what it finds.
2. **Local server smoke test** — run open-reader with `xteinkSurface` on (dev channel), create device credentials, walk the whole loop from a real device: auth → shelves → download → read → progress round-trip both directions. Setup in `integration.md`.
3. **Hardware checklist** — the full on-device pass in `milestones.md` (filename fidelity with unicode titles, heap headroom while browsing, sleep/quick-resume regressions, Canto-disabled boot).

## Phase 3 — Server follow-ups  *(lives in open-reader, not this repo)*  🔲

- **Triage endpoint** — implement `POST /api/xt/articles/{id}/state` per the contract in `integration.md`. The firmware ships ready and degrades gracefully until then.
- **`xteinkSurface` production enablement** — the flag is dev/beta-only pending real-hardware certification; Phase 2's smoke test is exactly that evidence.

## Phase 4 — Deferred / on-demand  *(no fixed order; pull in when real usage demands)*

- Article covers/thumbnails on the Canto home (RAM cost vs. benefit — measure first)
- Triage from touch devices (long-press; the Left-button trigger doesn't exist on the touch board)
- Offline shelf cache (browse Inbox/Later/Archive titles without Wi-Fi, not just downloaded articles)
- Highlights on device (large lift both sides; needs a server contract first)
- Archive-on-finish (end-of-book option that triages the article)
- Full rebrand (splash, hostname, OTA source) — deliberately deferred; see `decisions.md` D8

---

## Reassessment checkpoints

- **After Phase 2.1 (CI green):** if the diff needed structural rework to compile, re-verify the native tests and re-read `decisions.md` before patching further.
- **After Phase 2.2 (server smoke test):** does the pull-on-open / deferred-push model feel right in real reading? If positions drift or pushes lag, revisit `decisions.md` D6 before adding reader hooks.
- **Before any Phase 4 item:** check it against upstream `SCOPE.md`'s RAM-cost vs. reading-benefit gate. The fork deviates from upstream's connector freeze deliberately, but not from its memory discipline.
