# Canto Reader Layer

This fork adds a **Canto** layer on top of CrossPoint Reader: the firmware
becomes the device half of [Canto Reader](https://github.com/SubhanHabib/open-reader)
(a local-first read-it-later app), while every CrossPoint feature stays intact
behind the **Device** entry.

The Canto web app already ships the server half (the *Xteink surface*, feature
flag `xteinkSurface`): it serves the reading list as OPDS 1.2 feeds, builds an
EPUB per article on demand, and speaks the KOReader kosync protocol for
reading-position sync. The firmware side reuses the existing OPDS parser,
HTTP downloader, and KOReader sync client against those endpoints.

## Setup

1. In the Canto web app, generate device credentials (`/api/xt/credentials`,
   see the app's Xteink device docs).
2. On the device: **Settings → System → Canto** — enter the server URL (e.g.
   `https://your-canto-server.com`), username, and password, then run **Test
   Connection**. Alternatively paste them from a browser via the device web
   settings page (File Transfer mode) — the Canto keys are exposed on
   `/api/settings`.
3. Toggle **Canto Start Screen** on to make the Canto home the boot landing
   screen. The CrossPoint home stays reachable via the **Device** row.

One server URL + credential pair drives both protocols; the firmware derives
`{server}/api/xt/opds` (reading list, downloads) and `{server}/api/xt/kosync`
(progress sync) from it. Configuration lives in `/.crosspoint/canto.json`
(password obfuscated with the device MAC, as for OPDS/KOReader credentials).

## On-disk layout

- `/Canto/` — downloaded article EPUBs. Filenames use the same
  `opdsBookFilename()` derivation the server registers kosync document
  identities for — **do not rename or move these files**, or triage loses its
  article mapping (progress sync still works via the server's binary-hash
  fallback).
- `/.crosspoint/canto.json` — server config plus the article index: up to 24
  entries mapping local path ↔ server article UUID, with the 6-byte
  `progress.bin` snapshot from the last successful sync.

## Sync semantics

The firmware never runs background WiFi. Sync piggybacks on WiFi sessions the
user already started, following the fork's network conventions (TLS heap gate,
WiFi teardown + silent restart on activity exit):

- **Pull on open** — opening an article from the Canto library (WiFi already
  up) fetches that document's kosync progress and applies it when it is ahead
  of the local position. Rich crosspoint-sync positions apply losslessly;
  percentage-only records (e.g. app-newer overrides) fall back to percentage
  positioning.
- **Deferred push** — there are no reader hooks. An article is "pending" when
  its `progress.bin` differs from the snapshot stored at last sync. Pending
  flags are recomputed whenever the Canto home renders; pending pushes flush
  in a bounded batch (3) at the start of any Canto library session, and fully
  via **Sync Now** on the Canto home.
- The in-reader manual KOReader sync (long-press / reader menu) is untouched
  and keeps using the global KOReader account, not the Canto account.

## Triage (article actions)

Press **Left** on a book entry in the Canto library to move it between
shelves (Inbox / Later / Archive). This calls an endpoint that is **not yet
implemented in open-reader**; the firmware treats a 404 as "server doesn't
support this yet" and degrades gracefully.

### Endpoint contract (open-reader follow-up)

```
POST {base}/api/xt/articles/{articleId}/state
Auth:    same device auth as all /api/xt/* endpoints
         (Basic base64(user:pass) or x-auth-user + x-auth-key: md5(password))
Gate:    requireDevice() (flag off -> 404, no service key -> 501, rate limited -> 429)
Body:    {"state": "inbox" | "later" | "archived"}
200:     {"ok": true, "state": "..."}
400:     invalid or missing state
401:     bad credentials
404:     {"error": "unknown_article"} for a foreign/deleted article id.
         (A bare 404 from servers predating this route reads as "unsupported"
         on the firmware.)
```

Server implementation notes: hard-scope by the device's `user_id`; set
`articles.state`, bump `updated_at` and `server_seq` (same pattern as the
`xteink_apply_progress` RPC in migration `20260717000001_xteink_surface.sql`)
so web/mobile clients pick the change up through normal sync. Deletion is out
of scope for contract v1.

## Testing against a local server

1. Run the open-reader web app with `NEXT_PUBLIC_APP_CHANNEL=dev` (the
   `xteinkSurface` flag is on in dev/beta) and the Supabase service key
   configured — without it `/api/xt/*` returns 501.
2. Apply migration `20260717000001_xteink_surface.sql`; create device
   credentials via `POST /api/xt/credentials` with a signed-in user session.
3. Point the device's Canto server URL at the machine (use an explicit
   `http://` prefix for plain-HTTP LAN servers; bare hostnames default to
   `https://`).
