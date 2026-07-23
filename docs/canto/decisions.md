# Canto Cross — Design Decisions

The *why* behind the Canto layer's choices. `integration.md` and `milestones.md` say what was built; this says why, and what was rejected. **Read this before reopening a settled decision.**

Format: each entry is Context → Decision → Why → Rejected alternatives.

---

### D1 — Reuse the existing OPDS + kosync bridge; never invent a protocol

**Context:** The firmware needed a synced reading list and progress sync against the Canto server.
**Decision:** The Canto layer talks only to endpoints open-reader already serves (`/api/xt/opds`, `/api/xt/kosync`) using clients the firmware already ships (`OpdsParser`, `HttpDownloader`, `KOReaderSyncClient`). The one genuinely new call (triage) is a single REST POST behind a documented contract.
**Why:** Both halves of the bridge existed before this fork started — open-reader's Xteink surface was built *for* CrossPoint and byte-for-byte reimplements its hashing/filename logic. Wiring beats rewriting: less code, less RAM, and protocol compatibility is somebody else's regression suite.
**Rejected:** A bespoke JSON sync API (new client + new server surface, no benefit); using the Kindle `/k` surface's pairing model (self-contained but browser-oriented, wrong fit for a firmware client).

### D2 — Dedicated `CantoStore`; don't piggyback on OPDS_STORE / KOREADER_STORE

**Context:** The server wants one device credential pair for both OPDS (Basic auth) and kosync (`x-auth-*`). The firmware has existing stores for each protocol.
**Decision:** One `CantoStore` holds the server URL + credentials and derives both endpoint roots; it also owns the article index. The global OPDS and KOReader stores are untouched.
**Why:** Reusing `KOREADER_STORE` would hijack the user's global kosync identity (the in-reader manual sync depends on it); an `OPDS_STORE` entry would surface the Canto feed in the generic OPDS browser with the wrong behaviors. A dedicated store gives the single-setup flow the design wanted and keeps the article index (path ↔ UUID ↔ sync snapshot) in one place.
**Rejected:** Synthesizing entries into both existing stores at setup time — two sources of truth, global side effects, and no home for the index.

### D3 — Document identity = filename fidelity into a fixed `/Canto` folder

**Context:** Progress sync matches documents by hash. The server registers the MD5 of all three CrossPoint filename variants (plus the binary partial-MD5 of the served bytes) when it builds an article EPUB.
**Decision:** Downloads always go to `/Canto/` named by the exact existing `opdsBookFilename()` call; Canto sync always uses FILENAME matching. `localPathFor()` is the single source of that derivation.
**Why:** Filename matching costs no SD reads and is deterministic on both sides *because the server reimplements the firmware's own sanitizer*. The server's binary-hash registration remains a safety net if a file is renamed. A fixed folder keeps the article-index mapping stable and makes "is this a Canto article" trivial.
**Rejected:** Binary matching as primary (SD reads per sync, breaks if the server ever rebuilds an EPUB with different bytes); per-article server-assigned filenames (would need a new API and still have to match the registered hashes).

### D4 — Canto-first landing via a smart `goHome()`, gated on `isEnabled()`

**Context:** The user wants Canto as the primary experience with CrossPoint reachable, without breaking boot resume, crash routing, or the return-to-home-row highlighting of device features.
**Decision:** `goHome()` routes plain go-home calls (boot, reader home gesture, pop-to-empty, Canto activities) to `CantoHomeActivity` when Canto is enabled; device-home children (file browser, recents, OPDS, transfer, settings) keep returning to the CrossPoint `HomeActivity` with their row highlighted. `goToDeviceHome()` bypasses the routing for the explicit "Device" cross-link. Unconfigured devices boot exactly as upstream.
**Why:** One routing point, zero changes to `main.cpp`'s boot router or the silent-restart machinery — silent restarts land on the right home automatically. The gate on `isEnabled()` makes the whole layer opt-in and keeps first-boot behavior identical to upstream.
**Rejected:** Changing the boot router in `main.cpp` (more surface, misses the reader home gesture); a "default home" setting with its own routing switch (settings surface for something derivable).

### D5 — Model `CantoLibraryActivity` on the OPDS browser; don't subclass it

**Context:** The library needed ~85% of `OpdsBookBrowserActivity`'s proven state machine plus six behavioral differences (injected server, UUID capture, fixed folder, index recording, open-after-download, triage popup).
**Decision:** Copy the state machine into a new activity; keep all heavy lifting in the shared code both use (`OpdsParser`, `HttpDownloader`, `WifiSelectionActivity`, `UrlUtils`, `opdsBookFilename`, `clearBookCache`).
**Why:** The browser is `final` with private state — de-finalizing and virtualizing six extension points would complicate a stable upstream file and make future upstream merges harder (the fork's light-touch rule). The duplicated part is glue; the logic that could actually drift lives in shared utilities.
**Rejected:** Subclassing (invasive upstream diff); parameterizing the existing browser with callbacks (turns a readable activity into a framework).

### D6 — No background Wi-Fi; sync piggybacks on existing sessions, no reader hooks

**Context:** The convention is Wi-Fi on-demand inside explicit activities, teardown + silent restart on exit (heap defrag). "Sync on every reader open/close" would cost 7–15 s per transition and fight deep-sleep teardown.
**Decision:** Pull-on-open runs only when opening from the library (Wi-Fi already up). Push is deferred: pending state is *derived* by diffing `progress.bin` against the last-synced snapshot in the article index — recomputed offline whenever the Canto home renders — and flushed in a bounded batch at the start of any Canto network session, or fully via Sync Now. `EpubReaderActivity` is not touched.
**Why:** Zero extra Wi-Fi cycles, zero reader regressions, and the pending computation is a 6-byte file read per article. The snapshot diff also survives reboots for free because it's persisted with the index.
**Rejected:** Reader onExit hooks (couples the reader to Canto, needs Wi-Fi at the worst moment); a periodic background sync task (violates the no-background-Wi-Fi rule and the battery model); pushing on every save-point (SD churn + the same Wi-Fi problem).

### D7 — Triage ships firmware-first against a documented contract, degrading on 404

**Context:** OPDS is read-only; open-reader has no state-change endpoint. Scope for this effort was firmware-only.
**Decision:** The firmware implements triage against `POST /api/xt/articles/{id}/state` (contract in `integration.md`), treats 404 as "server doesn't support this yet", and the contract is recorded as the open-reader follow-up.
**Why:** Decouples the two repos' timelines; the UI ships and lights up when the server catches up. A 404 is unambiguous on servers that predate the route, so degradation is safe.
**Rejected:** Waiting on the server (couples releases); abusing kosync metadata fields to smuggle state changes (breaks protocol cleanliness and other kosync servers).

### D8 — Light rebrand only; stay mergeable with upstream

**Context:** This is a fork of an actively developed upstream (CrossPoint). A full rebrand (splash, hostname, OTA source, About) maximizes identity but every touched upstream file is a future merge conflict.
**Decision:** The layer is named "Canto" via `tr()` strings and new files under `src/activities/canto/`, `src/canto/`, `docs/canto/`. Splash, hostname (`crosspoint.local`), OTA update source, and About stay CrossPoint. Upstream's `SCOPE.md` connector freeze is deviated from deliberately (it's the point of the fork), but its memory/HAL/i18n conventions are followed everywhere.
**Why:** Upstream ships fixes this fork wants (reader, cache, hardware support). Small diffs in upstream files + new code in new directories keeps `git merge upstream/master` tractable. OTA staying on CrossPoint also means updates keep working without running release infrastructure.
**Rejected:** Full rebrand now (merge pain, and an OTA source question with no infrastructure behind it); no branding at all (the layer would be indistinguishable from the OPDS browser in the UI).
