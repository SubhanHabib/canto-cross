#pragma once
#include <GfxRenderer.h>

#include <cstddef>
#include <string>

/**
 * Headless reading-progress sync for Canto articles over the kosync protocol.
 *
 * No activity, no UI: these helpers piggyback on WiFi sessions that Canto
 * activities already hold (library browsing, explicit Sync Now) instead of
 * bringing WiFi up themselves. All TLS calls are sequenced before any Epub is
 * loaded, matching the release-before-sync heap pattern in
 * KOReaderSyncActivity.
 */
namespace CantoSyncHelper {

// Recomputes each indexed article's pendingPush flag by diffing its 6-byte
// progress.bin against the last-synced snapshot. Offline (SD reads only).
// Returns the number of articles now pending. Call whenever the Canto home
// renders so its "N to sync" hint stays fresh without reader hooks.
size_t refreshPendingFlags();

// Fetches remote progress for one article and applies it locally when it is
// ahead of the local position. WiFi must already be connected. Returns false
// only on hard errors (auth/network); a missing remote record is success.
bool pullApply(GfxRenderer& renderer, const std::string& path);

// Uploads local progress for up to maxCount pending articles (0 = no limit).
// WiFi must already be connected. Returns the number pushed successfully.
size_t pushPending(size_t maxCount = 0);

}  // namespace CantoSyncHelper
