#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

// One downloaded Canto article on the SD card.
//
// `articleId` is the server-side article UUID (OPDS entry <id> with the
// urn:uuid: prefix stripped) and is required for triage calls back to the
// server. `lastSyncedProgress` snapshots the 6-byte progress.bin payload at
// the last successful kosync push/pull; comparing it against the current
// progress.bin detects unpushed reading without any reader-activity hooks.
struct CantoArticle {
  std::string path;       // Absolute SD path, e.g. /Canto/Author - Title.epub
  std::string articleId;  // Server article UUID
  std::string title;
  std::string author;
  uint8_t lastSyncedProgress[6] = {0, 0, 0, 0, 0, 0};
  bool pendingPush = false;  // Runtime-only, recomputed from progress.bin (not persisted)
};

/**
 * Singleton store for the Canto Reader companion-server configuration and the
 * index of downloaded Canto articles.
 *
 * One server URL + one credential pair drive both protocols the Canto server
 * speaks: OPDS (reading-list feeds and article EPUBs) and kosync (reading
 * progress). The password is XOR-obfuscated with the device MAC and
 * base64-encoded on disk, same as the OPDS/KOReader stores.
 */
class CantoStore : public PersistableStore<CantoStore> {
 private:
  bool enabled = false;  // Canto home is the boot/landing screen when true
  bool autoSync = true;  // Pull progress when opening an article from the library
  std::string serverUrl;
  std::string username;
  std::string password;
  std::vector<CantoArticle> articles;  // Most recently downloaded first

  static constexpr size_t MAX_ARTICLES = 24;

  CantoStore() = default;
  ~CantoStore() = default;

  friend class PersistableStore<CantoStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/canto.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // --- Server configuration ---
  void setServerUrl(const std::string& url);
  const std::string& getServerUrl() const { return serverUrl; }
  void setCredentials(const std::string& user, const std::string& pass);
  const std::string& getUsername() const { return username; }
  const std::string& getPassword() const { return password; }
  bool hasCredentials() const { return !username.empty() && !password.empty(); }

  void setEnabled(bool value) { enabled = value; }
  bool getEnabled() const { return enabled; }
  void setAutoSync(bool value) { autoSync = value; }
  bool getAutoSync() const { return autoSync; }

  // Fully configured: a server URL plus credentials.
  bool isConfigured() const { return !serverUrl.empty() && hasCredentials(); }
  // Configured AND switched on — gates the Canto-first home routing.
  bool isEnabled() const { return enabled && isConfigured(); }

  // Server URL with protocol normalization (bare host defaults to https://,
  // Canto servers are typically TLS-hosted) and trailing slashes stripped.
  std::string getBaseUrl() const;
  // The two protocol roots the Canto server exposes for this device.
  std::string opdsRootUrl() const { return getBaseUrl() + "/api/xt/opds"; }
  std::string kosyncBaseUrl() const { return getBaseUrl() + "/api/xt/kosync"; }

  // --- Article index ---
  // Adds or refreshes an entry (dedup by path, moves to front), evicting the
  // oldest entry beyond MAX_ARTICLES. Persists.
  void recordArticle(const std::string& path, const std::string& articleId, const std::string& title,
                     const std::string& author);
  // nullptr when the path is not a known Canto article.
  const CantoArticle* findByPath(const std::string& path) const;
  // Stores the progress snapshot after a successful push/pull and clears the
  // pending flag. Persists only when the snapshot actually changed.
  void markSynced(const std::string& path, const uint8_t progress[6]);
  // Runtime pending flag (not persisted). Returns true when the flag changed.
  bool setPendingPush(const std::string& path, bool pending);
  // Drops the entry for a deleted/moved file. Persists when found.
  bool removeByPath(const std::string& path);

  const std::vector<CantoArticle>& getArticles() const { return articles; }
  size_t pendingPushCount() const;
};

#define CANTO_STORE CantoStore::getInstance()
