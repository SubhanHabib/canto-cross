# Canto Cross — Start Here

This is the index for the **Canto fork docs**. If you're a fresh Claude instance or a new chat, **read this first**, then the docs relevant to the task. (Upstream CrossPoint's own docs live one level up in `docs/` and at the repo root — `ROADMAP.md`, `SCOPE.md`, `USER_GUIDE.md`, `docs/contributing/architecture.md`. They still apply to everything that isn't the Canto layer.)

## What we're building

The **device half of Canto Reader**: a fork of CrossPoint Reader (ESP32-C3 e-ink firmware for the Xteink X4) that turns the e-reader into a first-class Canto client — synced reading list, article downloads, automatic reading-position sync, and triage — while keeping **every CrossPoint feature intact** behind a "Device" entry. The server half already exists in the [open-reader](https://github.com/SubhanHabib/open-reader) repo (the *Xteink surface*: OPDS feeds + kosync endpoints, feature flag `xteinkSurface`).

## How to use these docs

These docs are the **source of truth** for the Canto layer. When they conflict with a suggestion, the docs win. Before reopening a settled question, check `decisions.md` — the design was debated and each choice has a reason. Work **one milestone at a time** (see `roadmap.md`), verifying each before the next.

## The doc set

| Doc | What it's for | Read when |
| --- | --- | --- |
| `00-start-here.md` | This index | First |
| `roadmap.md` | Phase ordering, rationale, status at a glance | Deciding what's next |
| `milestones.md` | Task-level detail per shipped milestone: what landed, where, how to verify | Building / reviewing |
| `build.md` | Build process: toolchain, environments, generated files, formatting, tests, CI | Building anything |
| `decisions.md` | *Why* each design choice was made + rejected alternatives | Before changing direction |
| `integration.md` | The device↔server contract: setup, endpoints, sync semantics, on-disk layout, the triage API contract | Touching sync or the server |

## Current status (July 2026)

- **Phase 1 (firmware layer) is code-complete** — all seven milestones (M1–M7) are committed and pushed on `claude/canto-reader-migration-keyq66`. See `roadmap.md` for the per-milestone list.
- **Nothing is compile-verified by CI yet**: the CI build workflow only triggers on pull requests, and no PR has been opened. Native host tests (129) pass; formatting and i18n generation are clean.
- **Nothing is hardware-verified yet**: the full on-device checklist is in `milestones.md`.
- **The server side has one gap**: the triage endpoint (`POST /api/xt/articles/{id}/state`) is a documented follow-up for open-reader — the firmware degrades gracefully (404 → "server doesn't support this yet") until it ships. Contract in `integration.md`.
- **Mode:** solo + AI coding agents, matching the open-reader workflow.

## The three things that matter most

1. **The bridge already exists — reuse it.** The firmware's OPDS parser and KOReader kosync client talk to endpoints open-reader already serves. The Canto layer is wiring and UI, never a new protocol.
2. **CrossPoint stays intact.** The Canto layer sits on top; the CrossPoint home, reader, and every upstream feature remain reachable and unmodified. Keep diffs against upstream small and mergeable (light rebrand only).
3. **Respect the hardware conventions.** ~380 KB RAM, no PSRAM, 55 KB contiguous heap per TLS handshake, no background Wi-Fi, silent-restart after network sessions, `tr()` for all user-facing text. When Canto code and an upstream convention disagree, the convention wins.

Almost every decision in `decisions.md` is downstream of those three. When in doubt, check a choice against them.
