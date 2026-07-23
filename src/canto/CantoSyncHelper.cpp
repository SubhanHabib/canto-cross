#include "CantoSyncHelper.h"

#include <Arduino.h>
#include <Epub.h>
#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <KOReaderSyncClient.h>
#include <Logging.h>
#include <ProgressMapper.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "CantoStore.h"
#include "activities/reader/EpubReaderUtils.h"

namespace {
constexpr size_t PROGRESS_BYTES = 6;
// Same threshold KOReaderSyncActivity uses to treat positions as equal.
constexpr float SAME_PROGRESS_EPSILON = 0.001f;

KOSyncAccount cantoAccount() {
  return KOSyncAccount{CANTO_STORE.kosyncBaseUrl(), CANTO_STORE.getUsername(), CANTO_STORE.getPassword()};
}

// Mirrors the cache-dir derivation in Epub's constructor (lib/Epub/Epub.h) so
// progress.bin can be located without instantiating an Epub.
std::string cachePathFor(const std::string& path) {
  return std::string("/.crosspoint/epub_") + std::to_string(std::hash<std::string>{}(path));
}

// Reads the 6-byte progress.bin (or the 4-byte legacy form, zero-padded).
// False when no progress has been saved yet — out is zeroed in that case.
bool readProgressBytes(const std::string& path, uint8_t out[PROGRESS_BYTES]) {
  memset(out, 0, PROGRESS_BYTES);
  HalFile f;
  if (!Storage.openFileForRead("CANTO", cachePathFor(path) + "/progress.bin", f)) {
    return false;
  }
  const int dataSize = f.read(out, PROGRESS_BYTES);
  if (dataSize != 4 && dataSize != static_cast<int>(PROGRESS_BYTES)) {
    memset(out, 0, PROGRESS_BYTES);
    return false;
  }
  return true;
}

struct LocalPosition {
  int spineIndex = 0;
  int pageNumber = 0;
  int pageCount = 0;
};

LocalPosition decodeProgress(const uint8_t bytes[PROGRESS_BYTES]) {
  LocalPosition pos;
  pos.spineIndex = bytes[0] + (bytes[1] << 8);
  pos.pageNumber = bytes[2] + (bytes[3] << 8);
  if (pos.pageNumber == UINT16_MAX) pos.pageNumber = 0;  // stale last-page sentinel
  pos.pageCount = bytes[4] + (bytes[5] << 8);
  return pos;
}

// Metadata-only Epub load, as in KOReaderSyncActivity::ensureEpubLoaded().
std::shared_ptr<Epub> loadEpub(const std::string& path) {
  auto epub = std::make_shared<Epub>(path, "/.crosspoint");
  epub->setupCacheDir();
  if (!epub->load(false, true)) {
    LOG_ERR("CANTO", "Failed to load epub for sync: %s", path.c_str());
    return nullptr;
  }
  return epub;
}
}  // namespace

namespace CantoSyncHelper {

size_t refreshPendingFlags() {
  size_t pending = 0;
  for (const auto& article : CANTO_STORE.getArticles()) {
    uint8_t bytes[PROGRESS_BYTES];
    readProgressBytes(article.path, bytes);
    const bool dirty = memcmp(bytes, article.lastSyncedProgress, PROGRESS_BYTES) != 0;
    CANTO_STORE.setPendingPush(article.path, dirty);
    if (dirty) pending++;
  }
  return pending;
}

bool pullApply(GfxRenderer& renderer, const std::string& path) {
  const std::string hash = KOReaderDocumentId::calculateFromFilename(path);
  if (hash.empty()) {
    LOG_ERR("CANTO", "Document hash failed for %s", path.c_str());
    return false;
  }

  // TLS before any Epub load: the handshake needs the contiguous heap.
  KOReaderProgress remote;
  const auto result = KOReaderSyncClient::getProgress(cantoAccount(), hash, remote);
  if (result == KOReaderSyncClient::NOT_FOUND) {
    return true;  // Nothing on the server yet — nothing to apply
  }
  if (result != KOReaderSyncClient::OK) {
    LOG_ERR("CANTO", "Progress fetch failed: %s", KOReaderSyncClient::errorString(result));
    return false;
  }

  uint8_t localBytes[PROGRESS_BYTES];
  readProgressBytes(path, localBytes);
  const LocalPosition local = decodeProgress(localBytes);

  auto epub = loadEpub(path);
  if (!epub) return false;

  CrossPointPosition localPos;
  localPos.spineIndex = local.spineIndex;
  localPos.pageNumber = local.pageNumber;
  localPos.totalPages = local.pageCount > 0 ? local.pageCount : 1;
  const SavedProgressPosition localKo = ProgressMapper::toSavedProgress(epub, localPos);

  if (remote.percentage <= localKo.percentage + SAME_PROGRESS_EPSILON) {
    return true;  // Local is equal or ahead — pushPending handles the upload
  }

  // Remote is ahead: map it to a CrossPoint position and persist it. Prefer
  // the lossless rich position (crosspoint-sync extension); fall back to the
  // xpath/percentage approximation.
  std::optional<CrossPointPosition> mapped;
  if (remote.position.has_value()) {
    mapped = ProgressMapper::fromRichPosition(epub, *remote.position, renderer);
  }
  if (!mapped.has_value()) {
    const SavedProgressPosition remoteKo{remote.progress, remote.percentage};
    mapped = ProgressMapper::toCrossPoint(epub, remoteKo, renderer, local.spineIndex, local.pageCount);
  }

  if (!EpubReaderUtils::saveProgress(*epub, mapped->spineIndex, mapped->pageNumber, 0)) {
    return false;
  }
  LOG_DBG("CANTO", "Applied remote progress %.1f%% (spine=%d page=%d) to %s", remote.percentage * 100,
          mapped->spineIndex, mapped->pageNumber, path.c_str());

  uint8_t syncedBytes[PROGRESS_BYTES];
  readProgressBytes(path, syncedBytes);
  CANTO_STORE.markSynced(path, syncedBytes);
  return true;
}

size_t pushPending(size_t maxCount) {
  size_t pushed = 0;
  // Iterate over a snapshot of paths: markSynced mutates the store entries.
  std::vector<std::string> paths;
  paths.reserve(CANTO_STORE.getArticles().size());
  for (const auto& article : CANTO_STORE.getArticles()) {
    if (article.pendingPush) paths.push_back(article.path);
  }

  for (const auto& path : paths) {
    if (maxCount > 0 && pushed >= maxCount) break;

    uint8_t bytes[PROGRESS_BYTES];
    if (!readProgressBytes(path, bytes)) {
      // No local progress on disk: treat the zero snapshot as synced so the
      // entry stops showing as pending.
      CANTO_STORE.markSynced(path, bytes);
      continue;
    }
    const LocalPosition local = decodeProgress(bytes);

    const std::string hash = KOReaderDocumentId::calculateFromFilename(path);
    if (hash.empty()) continue;

    KOReaderProgress progress;
    progress.document = hash;
    {
      // Compute xpath/percentage with the Epub in RAM, then release it before
      // the TLS call (release-before-sync heap pattern).
      auto epub = loadEpub(path);
      if (!epub) continue;
      CrossPointPosition pos;
      pos.spineIndex = local.spineIndex;
      pos.pageNumber = local.pageNumber;
      pos.totalPages = local.pageCount > 0 ? local.pageCount : 1;
      const SavedProgressPosition ko = ProgressMapper::toSavedProgress(epub, pos);
      progress.progress = ko.xpath;
      progress.percentage = ko.percentage;
    }

    KOReaderRichPosition rich;
    const float pct = progress.percentage < 0.0f ? 0.0f : progress.percentage > 1.0f ? 1.0f : progress.percentage;
    rich.pctQ = static_cast<uint32_t>(pct * 1000000.0f + 0.5f);
    rich.spineIndex = static_cast<uint16_t>(local.spineIndex);
    rich.pageNumber = static_cast<uint16_t>(local.pageNumber);
    rich.totalPages = static_cast<uint16_t>(local.pageCount > 0 ? local.pageCount : 1);
    rich.xpath = progress.progress;
    progress.position = std::move(rich);

    const auto result = KOReaderSyncClient::updateProgress(cantoAccount(), progress);
    if (result != KOReaderSyncClient::OK) {
      LOG_ERR("CANTO", "Progress push failed for %s: %s", path.c_str(), KOReaderSyncClient::errorString(result));
      // Auth/network problems will fail for every entry — stop early.
      if (result == KOReaderSyncClient::AUTH_FAILED || result == KOReaderSyncClient::NETWORK_ERROR ||
          result == KOReaderSyncClient::LOW_MEMORY) {
        break;
      }
      continue;
    }

    CANTO_STORE.markSynced(path, bytes);
    pushed++;
    delay(1);  // Yield between TLS round-trips
  }

  return pushed;
}

}  // namespace CantoSyncHelper
