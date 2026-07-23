#pragma once
#include <OpdsParser.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Browses the Canto reading list (Inbox/Later/Archive/collections/search)
 * served as OPDS feeds by the Canto companion server, downloads article EPUBs
 * into the /Canto folder, and opens them in the reader.
 *
 * Modeled on OpdsBookBrowserActivity, with Canto-specific behavior: the server
 * is derived from CANTO_STORE, downloaded articles are recorded in the article
 * index (path <-> server article UUID), already-downloaded entries open
 * directly, and a successful download opens the reader immediately via a
 * silent restart.
 */
class CantoLibraryActivity final : public Activity {
 public:
  enum class BrowserState { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, DOWNLOADING, ERROR, SEARCH_INPUT };

  explicit CantoLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CantoLibrary", renderer, mappedInput), buttonNavigator() {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  BrowserState state = BrowserState::LOADING;
  std::vector<OpdsEntry> entries;
  std::vector<std::string> navigationHistory;
  std::string currentPath;
  std::string searchTemplate;
  bool consumeConfirm = false;
  bool consumeBack = false;
  int selectorIndex = 0;
  std::string errorMessage;
  std::string statusMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;

  std::string serverUrl;  // CANTO_STORE.opdsRootUrl(), resolved on enter
  // When non-empty, onExit silent-restarts into the reader for this path.
  std::string pendingOpenPath;

  // Local SD path an entry downloads to (fixed /Canto folder + the same
  // filename derivation the server registers kosync identities for).
  std::string localPathFor(const OpdsEntry& book) const;
  // True when the entry's EPUB is already on the SD card.
  bool isDownloaded(const OpdsEntry& book) const;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  // Pushes a bounded batch of pending progress updates at session start.
  void flushPendingSync();
  void fetchFeed(const std::string& path);
  void releaseEntries();
  void navigateToEntry(const OpdsEntry& entry);
  void navigateBack();
  void downloadBook(const OpdsEntry& book);
  void openArticle(const std::string& path);
  void launchSearch();
  void performSearch(const std::string& query);
  bool preventAutoSleep() override { return true; }
};
