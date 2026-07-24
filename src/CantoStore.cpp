#include "CantoStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr size_t PROGRESS_BYTES = 6;

// Serialize the 6-byte progress snapshot as 12 lowercase hex chars.
void progressToHex(const uint8_t progress[PROGRESS_BYTES], char out[PROGRESS_BYTES * 2 + 1]) {
  for (size_t i = 0; i < PROGRESS_BYTES; i++) {
    snprintf(&out[i * 2], 3, "%02x", progress[i]);
  }
}

bool hexToProgress(const char* hex, uint8_t out[PROGRESS_BYTES]) {
  if (!hex || strlen(hex) != PROGRESS_BYTES * 2) {
    return false;
  }
  for (size_t i = 0; i < PROGRESS_BYTES; i++) {
    unsigned value;
    if (sscanf(&hex[i * 2], "%2x", &value) != 1) {
      return false;
    }
    out[i] = static_cast<uint8_t>(value);
  }
  return true;
}
}  // namespace

void CantoStore::toJson(JsonDocument& doc) const {
  doc["enabled"] = enabled;
  doc["autoSync"] = autoSync;
  doc["serverUrl"] = serverUrl;
  doc["username"] = username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(password);

  JsonArray arr = doc["articles"].to<JsonArray>();
  for (const auto& article : articles) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = article.path;
    obj["id"] = article.articleId;
    obj["title"] = article.title;
    obj["author"] = article.author;
    char hex[PROGRESS_BYTES * 2 + 1];
    progressToHex(article.lastSyncedProgress, hex);
    obj["synced"] = hex;
  }
}

bool CantoStore::fromJson(JsonVariantConst doc) {
  enabled = doc["enabled"] | false;
  autoSync = doc["autoSync"] | true;
  serverUrl = doc["serverUrl"] | "";
  username = doc["username"] | "";

  bool needsResave = false;
  password = extractPassword(doc, needsResave);

  articles.clear();
  JsonArrayConst arr = doc["articles"].as<JsonArrayConst>();
  articles.reserve(std::min(arr.size(), MAX_ARTICLES));
  for (JsonObjectConst obj : arr) {
    if (articles.size() >= MAX_ARTICLES) break;
    CantoArticle article;
    article.path = obj["path"] | "";
    article.articleId = obj["id"] | "";
    article.title = obj["title"] | "";
    article.author = obj["author"] | "";
    if (article.path.empty()) continue;
    if (!hexToProgress(obj["synced"] | "", article.lastSyncedProgress)) {
      memset(article.lastSyncedProgress, 0, PROGRESS_BYTES);
    }
    articles.push_back(std::move(article));
  }

  LOG_DBG("CANTO", "Loaded config (enabled=%d) and %zu articles", enabled ? 1 : 0, articles.size());

  if (needsResave) {
    requestResave();
  }
  return true;
}

void CantoStore::setServerUrl(const std::string& url) {
  serverUrl = url;
  LOG_DBG("CANTO", "Set server URL: %s", url.empty() ? "(none)" : url.c_str());
}

void CantoStore::setCredentials(const std::string& user, const std::string& pass) {
  username = user;
  password = pass;
  LOG_DBG("CANTO", "Set credentials for user: %s", user.c_str());
}

std::string CantoStore::getBaseUrl() const {
  std::string url = serverUrl;
  if (!url.empty() && url.find("://") == std::string::npos) {
    // Canto servers are typically TLS-hosted; bare hosts default to https.
    // LAN/dev servers should be entered with an explicit http:// prefix.
    url = "https://" + url;
  }
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
  return url;
}

void CantoStore::recordArticle(const std::string& path, const std::string& articleId, const std::string& title,
                               const std::string& author) {
  auto it = std::find_if(articles.begin(), articles.end(),
                         [&path](const CantoArticle& article) { return article.path == path; });
  if (it != articles.end()) {
    // Refresh metadata and move to front, keeping the sync snapshot.
    CantoArticle existing = std::move(*it);
    existing.articleId = articleId;
    existing.title = title;
    existing.author = author;
    articles.erase(it);
    articles.insert(articles.begin(), std::move(existing));
  } else {
    if (articles.size() >= MAX_ARTICLES) {
      articles.pop_back();
    }
    CantoArticle article;
    article.path = path;
    article.articleId = articleId;
    article.title = title;
    article.author = author;
    articles.insert(articles.begin(), std::move(article));
  }
  saveToFile();
  LOG_DBG("CANTO", "Recorded article: %s", path.c_str());
}

const CantoArticle* CantoStore::findByPath(const std::string& path) const {
  for (const auto& article : articles) {
    if (article.path == path) {
      return &article;
    }
  }
  return nullptr;
}

void CantoStore::markSynced(const std::string& path, const uint8_t progress[6]) {
  for (auto& article : articles) {
    if (article.path != path) continue;
    article.pendingPush = false;
    if (memcmp(article.lastSyncedProgress, progress, PROGRESS_BYTES) == 0) {
      return;  // Snapshot unchanged — skip the SD write
    }
    memcpy(article.lastSyncedProgress, progress, PROGRESS_BYTES);
    saveToFile();
    return;
  }
}

bool CantoStore::setPendingPush(const std::string& path, bool pending) {
  for (auto& article : articles) {
    if (article.path != path) continue;
    if (article.pendingPush == pending) return false;
    article.pendingPush = pending;
    return true;
  }
  return false;
}

bool CantoStore::removeByPath(const std::string& path) {
  auto it = std::find_if(articles.begin(), articles.end(),
                         [&path](const CantoArticle& article) { return article.path == path; });
  if (it == articles.end()) {
    return false;
  }
  articles.erase(it);
  saveToFile();
  LOG_DBG("CANTO", "Removed article: %s", path.c_str());
  return true;
}

size_t CantoStore::pendingPushCount() const {
  size_t count = 0;
  for (const auto& article : articles) {
    if (article.pendingPush) count++;
  }
  return count;
}
