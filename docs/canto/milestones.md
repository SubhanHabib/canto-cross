# Canto Cross — Milestones

Task-level record of Phase 1 (see `roadmap.md` for ordering rationale). Each milestone is one commit on `claude/canto-reader-migration-keyq66`, independently buildable, with its verification status. "✅ code-complete" means committed with host-side checks green; the ESP32 compile check (CI) and the hardware pass are still owed for all of them (Phase 2).

---

## M1 — `CantoStore` ✅ code-complete

**What:** Persistent store (`/.crosspoint/canto.json`) for the Canto server URL, device credentials (MAC-XOR obfuscated, same as OPDS/KOReader stores), enabled/auto-sync flags, and the **article index** — up to 24 entries mapping local path ↔ server article UUID with the 6-byte `progress.bin` snapshot from the last successful sync. One URL + credential pair derives both endpoint roots (`opdsRootUrl()`, `kosyncBaseUrl()`).
**Where:** `src/CantoStore.{h,cpp}`; loaded in `src/main.cpp` beside the other stores; exposed on the web settings API via a new `STR_CANTO` category block in `src/SettingsList.h` (web-only by construction — device tabs filter to their four categories).
**Verify:** keys round-trip through `GET/POST /api/settings` in File Transfer mode; password lands obfuscated in `canto.json`.

## M2 — `KOReaderSyncClient` explicit-account refactor ✅ code-complete

**What:** `KOSyncAccount{baseUrl, username, password}` overloads for `authenticate` / `getProgress` / `updateProgress`, so the Canto layer can speak kosync with its own account. Store-backed entry points delegate to the overloads — behavior-neutral by design.
**Where:** `lib/KOReaderSync/KOReaderSyncClient.{h,cpp}` only; zero call-site changes.
**Verify:** manual KOReader sync from the reader still works against any kosync server (regression, not new behavior).

## M3 — Canto settings + connection test ✅ code-complete

**What:** `CantoSettingsActivity` (Settings → System → Canto): server URL / username / password via keyboard entry, Canto-start-screen and auto-sync toggles, Test Connection. `CantoAuthActivity` runs the test against the server's kosync auth endpoint over an on-demand Wi-Fi session (teardown + silent restart on exit, per convention).
**Where:** `src/activities/canto/CantoSettingsActivity.{h,cpp}`, `CantoAuthActivity.{h,cpp}`; `SettingAction::Canto` wiring in `src/activities/settings/SettingsActivity.{h,cpp}`.
**Verify:** with credentials from open-reader's `/api/xt/credentials`, Test Connection → 200; wrong password → auth-failed message.

## M4 — `CantoLibraryActivity` ✅ code-complete

**What:** Browses the reading list (Inbox/Later/Archive/collections/search) from `/api/xt/opds`, downloads article EPUBs into `/Canto`, records each download in the article index, opens already-downloaded entries directly ("Read" vs "Download" confirm label), and routes into the reader via silent restart. **Critical invariant:** filenames use the exact `opdsBookFilename()` derivation the server registers kosync identities for — `localPathFor()` and `downloadBook()` must never diverge.
**Where:** `src/activities/canto/CantoLibraryActivity.{h,cpp}` (modeled on `OpdsBookBrowserActivity` — see `decisions.md` D5); `goToCantoLibrary()` in `ActivityManager`; conditional Canto row on the CrossPoint home (`HomeActivity`, `HomeMenuItem::CANTO`).
**Verify:** shelf browse + pagination (server pages at 50, parser caps at 62) + search; download lands in `/Canto` with the expected filename (spot-check a unicode/illegal-char title — both sides cap at 100 UTF-8 bytes); article opens in the reader after the silent reboot; `canto.json` gains the entry with the bare UUID.

## M5 — Headless progress sync ✅ code-complete

**What:** `CantoSyncHelper` — no activity, no reader hooks. `refreshPendingFlags()` diffs each article's `progress.bin` against its last-synced snapshot (offline, SD only). `pullApply()` fetches one document's remote progress and applies it when ahead (rich crosspoint position preferred, percentage fallback). `pushPending()` uploads pending positions. TLS is always sequenced **before** Epub loads (55 KB contiguous-heap gate). Wired into the library: bounded push batch (3) at session start, pull before open when auto-sync is on.
**Where:** `src/canto/CantoSyncHelper.{h,cpp}`; call sites in `CantoLibraryActivity`.
**Verify:** read to N% on device, reopen library → PUT lands (server `xteink_progress` + `articles.reading_position`); move position in the web app, open the article from the library → device opens at the app's percentage (including the app-newer empty-xpath override).

## M6 — Canto home + Canto-first routing ✅ code-complete

**What:** `CantoHomeActivity` — the landing screen when Canto is enabled: up to 5 recent downloaded articles (offline, pruned of missing files), Library / Sync Now (pending-count hint) / Device / Settings rows; Back continues the most recent article; unconfigured state shows a setup hint. `CantoSyncActivity` — modal Sync Now (skips Wi-Fi when nothing is pending). `goHome()` becomes a smart router: plain go-home (boot, reader home gesture, pop-to-empty) → Canto home when enabled; device-home children keep returning to the CrossPoint home; `goToDeviceHome()` bypasses the routing for the Device cross-link.
**Where:** `src/activities/canto/CantoHomeActivity.{h,cpp}`, `CantoSyncActivity.{h,cpp}`; routing in `src/activities/ActivityManager.cpp`.
**Verify:** boot with Canto enabled → Canto home; Device → CrossPoint home fully intact (incl. return-row highlighting); reader home gesture → Canto home; disable Canto → boots to CrossPoint home; quick-resume and crash-report boot unaffected.

## M7 — Triage + docs ✅ code-complete

**What:** Left on a book entry → article-actions popup (Move to Inbox / Later / Archive) → `CantoApiClient::setArticleState()` posts to `{base}/api/xt/articles/{id}/state` with the shared device auth. 404 (endpoint not shipped yet) surfaces as "server doesn't support this yet". `integration.md` documents the layer and the endpoint contract as the open-reader follow-up.
**Where:** `src/network/CantoApiClient.{h,cpp}`; popup wiring in `CantoLibraryActivity`; `docs/canto/integration.md`.
**Verify:** against current open-reader → graceful 404 message; after the server follow-up ships → shelf change reflected in the web app and the refetched device feed.

---

## The Phase 2 hardware checklist (owed for all of the above)

1. Settings entry + web-paste setup path work; auth 200 and 401 paths render correctly.
2. Shelf browse / pagination / search across a real library; feed truncation behaves.
3. Download filename byte-matches `crosspointFilename` server-side for ASCII **and** unicode/illegal-char titles.
4. Progress round-trip both directions, including the percentage-only (app-newer) override.
5. Triage 404 fallback message.
6. Heap: `ESP.getFreeHeap()` > 50 KB throughout library browsing and sync; no leak across repeated enter/exit.
7. Regressions: OPDS browser, manual KOReader sync, file transfer, sleep/quick-resume, crash-report boot, Canto-disabled boot.
