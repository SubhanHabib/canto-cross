#include "CantoLibraryActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <OpdsStream.h>
#include <WiFi.h>

#include "CantoStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "canto/CantoSyncHelper.h"
#include "components/UITheme.h"
#include "components/icons/search24.h"
#include "fontIds.h"
#include "network/CantoApiClient.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/UrlUtils.h"

namespace {
constexpr int PAGE_ITEMS = 23;
constexpr int HEADER_Y = 15;
constexpr int HEADER_X = 16;
constexpr int SEARCH_ICON_SIZE = 24;
constexpr int SEARCH_ICON_MARGIN = 14;
constexpr int SEARCH_ICON_Y = 15;
constexpr int DOWNLOAD_PROGRESS_STEP_PERCENT = 5;
constexpr unsigned long DOWNLOAD_PROGRESS_MIN_UPDATE_MS = 5000;

// Bound the session-start push batch so entering the library stays snappy;
// the remainder flushes on the next session or via Sync Now.
constexpr size_t MAX_PUSH_PER_SESSION = 3;

// Fixed download folder. The article index maps these paths to server article
// UUIDs; keeping every Canto download here keeps that mapping stable.
constexpr char CANTO_FOLDER[] = "/Canto";

// OPDS entry ids are `urn:uuid:<uuid>`; the server API wants the bare UUID.
std::string stripUrnUuid(const std::string& id) {
  constexpr char PREFIX[] = "urn:uuid:";
  constexpr size_t PREFIX_LEN = sizeof(PREFIX) - 1;
  if (id.compare(0, PREFIX_LEN, PREFIX) == 0) {
    return id.substr(PREFIX_LEN);
  }
  return id;
}

Rect searchIconRect(const GfxRenderer& renderer) {
  return Rect{renderer.getScreenWidth() - SEARCH_ICON_SIZE - SEARCH_ICON_MARGIN, SEARCH_ICON_Y, SEARCH_ICON_SIZE + 8,
              SEARCH_ICON_SIZE + 8};
}

bool contains(const Rect& rect, const int x, const int y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}
}  // namespace

std::string CantoLibraryActivity::localPathFor(const OpdsEntry& book) const {
  std::string path;
  path.reserve(96);
  path += CANTO_FOLDER;
  path += '/';
  path += opdsBookFilename(book.author, book.title, static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  return path;
}

bool CantoLibraryActivity::isDownloaded(const OpdsEntry& book) const {
  const std::string path = localPathFor(book);
  // Index lookup first (no SD I/O); fall back to an existence check so files
  // downloaded before an index wipe still open instead of re-downloading.
  if (CANTO_STORE.findByPath(path) != nullptr) return true;
  return Storage.exists(path.c_str());
}

void CantoLibraryActivity::onEnter() {
  Activity::onEnter();

  state = BrowserState::CHECK_WIFI;
  entries.clear();
  navigationHistory.clear();
  searchTemplate = "";
  currentPath = "";
  selectorIndex = 0;
  consumeConfirm = false;
  consumeBack = false;
  errorMessage.clear();
  pendingOpenPath.clear();
  serverUrl = CANTO_STORE.opdsRootUrl();
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  checkAndConnectWifi();
}

void CantoLibraryActivity::onExit() {
  Activity::onExit();
  entries.clear();
  navigationHistory.clear();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    pendingOpenPath.empty() ? silentRestart() : silentRestartToReader();
  } else if (!pendingOpenPath.empty()) {
    // No WiFi session to tear down but an article was selected for reading —
    // still route through the silent restart so boot lands in the reader.
    silentRestartToReader();
  }
}

void CantoLibraryActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION || state == BrowserState::SEARCH_INPUT) {
    return;
  }

  if (consumeConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    consumeConfirm = false;
    return;
  }
  if (consumeBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    consumeBack = false;
    return;
  }

  if (state == BrowserState::ERROR) {
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = BrowserState::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchFeed(currentPath);
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state == BrowserState::CHECK_WIFI ? onGoHome() : navigateBack();
    }
    return;
  }

  if (state == BrowserState::DOWNLOADING) return;

  if (state == BrowserState::BROWSING) {
    if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

    auto activateSelected = [this] {
      if (!entries.empty()) {
        const auto& entry = entries[selectorIndex];
        if (entry.type != OpdsEntryType::BOOK) {
          navigateToEntry(entry);
        } else if (isDownloaded(entry)) {
          openArticle(localPathFor(entry));
        } else {
          downloadBook(entry);
        }
      }
    };

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activateSelected();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (!searchTemplate.empty() && selectorIndex == 0) {
        launchSearch();
      } else {
        showTriagePopup();
      }
    }

    int tx = 0;
    int ty = 0;
    if (!searchTemplate.empty() && mappedInput.wasScreenTapped(tx, ty) && contains(searchIconRect(renderer), tx, ty)) {
      launchSearch();
      return;
    }

    if (!entries.empty()) {
      int row = -1;
      const auto touch = mappedInput.rowTouch(row, /*top=*/60, /*rowStep=*/30, PAGE_ITEMS);
      if (touch != MappedInputManager::RowTouch::None) {
        const int touched = selectorIndex / PAGE_ITEMS * PAGE_ITEMS + row;
        if (touched >= 0 && touched < static_cast<int>(entries.size())) {
          if (touch == MappedInputManager::RowTouch::Down) {
            if (selectorIndex != touched) {
              selectorIndex = touched;
              requestUpdate();
            }
          } else {
            selectorIndex = touched;
            activateSelected();
          }
          return;
        }
      }

      const auto swipe = mappedInput.wasSwipe();
      if (swipe == MappedInputManager::SwipeDir::Up) {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
        requestUpdate();
        return;
      }
      if (swipe == MappedInputManager::SwipeDir::Down) {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
        requestUpdate();
        return;
      }

      buttonNavigator.onNextRelease([this] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, entries.size());
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, entries.size());
        requestUpdate();
      });
      buttonNavigator.onNextContinuous([this] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
        requestUpdate();
      });
      buttonNavigator.onPreviousContinuous([this] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

void CantoLibraryActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* headerTitle = tr(STR_CANTO);
  const int headerRightInset = searchTemplate.empty() ? HEADER_X : (SEARCH_ICON_SIZE + SEARCH_ICON_MARGIN * 2 + 8);
  const auto clippedHeader =
      renderer.truncatedText(UI_12_FONT_ID, headerTitle, pageWidth - HEADER_X - headerRightInset, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, HEADER_X, HEADER_Y, clippedHeader.c_str(), true, EpdFontFamily::BOLD);
  if (!searchTemplate.empty()) {
    const auto rect = searchIconRect(renderer);
    renderer.drawIcon(Search24Icon.bits, rect.x + 4, rect.y + 4, Search24Icon.w);
  }

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    if (mappedInput.hasTouch()) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 40, tr(STR_TAP_TO_RETRY));
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DOWNLOADING));
    auto title = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, title.c_str());
    if (downloadTotal > 0) {
      GUI.drawProgressBar(renderer, Rect{50, pageHeight / 2 + 20, pageWidth - 100, 20}, downloadProgress,
                          downloadTotal);
    }
    renderer.displayBuffer();
    return;
  }

  const bool selectedIsBook = !entries.empty() && entries[selectorIndex].type == OpdsEntryType::BOOK;
  const char* confirmLabel = !selectedIsBook                        ? tr(STR_OPEN)
                             : isDownloaded(entries[selectorIndex]) ? tr(STR_CANTO_READ)
                                                                    : tr(STR_DOWNLOAD);
  const char* searchLabel = (!searchTemplate.empty() && selectorIndex == 0) ? tr(STR_SEARCH) : tr(STR_DIR_UP);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, searchLabel, tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ENTRIES));
  } else {
    const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

    for (size_t i = pageStartIndex; i < entries.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
      const auto& entry = entries[i];
      std::string displayText = (entry.type == OpdsEntryType::NAVIGATION) ? "> " + entry.title : entry.title;
      if (entry.type == OpdsEntryType::BOOK && !entry.author.empty()) displayText += " - " + entry.author;
      auto item = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }
  renderer.displayBuffer();
}

void CantoLibraryActivity::fetchFeed(const std::string& path) {
  if (serverUrl.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_CANTO_NOT_CONFIGURED);
    requestUpdate();
    return;
  }

  std::string url = UrlUtils::buildUrl(serverUrl, path);
  LOG_DBG("CANTO", "Fetching: %s", url.c_str());
  OpdsParser parser;
  {
    OpdsParserStream stream{parser};
    if (!HttpDownloader::fetchUrl(url, stream, CANTO_STORE.getUsername(), CANTO_STORE.getPassword())) {
      state = BrowserState::ERROR;
      errorMessage = tr(STR_FETCH_FEED_FAILED);
      requestUpdate();
      return;
    }
  }

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  searchTemplate = parser.getSearchTemplate();
  const auto& nextUrl = parser.getNextPageUrl();
  const auto& prevUrl = parser.getPrevPageUrl();
  const bool feedTruncated = parser.truncated();
  entries = std::move(parser).getEntries();

  entries.reserve(entries.size() + (prevUrl.empty() ? 0 : 1) + (nextUrl.empty() ? 0 : 1));
  if (!prevUrl.empty()) {
    entries.insert(entries.begin(), OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_PREV_PAGE), "", prevUrl, ""});
  }
  if (!nextUrl.empty()) {
    entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_NEXT_PAGE), "", nextUrl, ""});
  }
  if (feedTruncated) {
    LOG_INF("CANTO", "Feed truncated to fit memory");
  }

  selectorIndex = 0;
  state = entries.empty() ? BrowserState::ERROR : BrowserState::BROWSING;
  if (entries.empty()) errorMessage = tr(STR_NO_ENTRIES);
  requestUpdate();
}

void CantoLibraryActivity::releaseEntries() { std::vector<OpdsEntry>().swap(entries); }

void CantoLibraryActivity::navigateToEntry(const OpdsEntry& entry) {
  navigationHistory.push_back(currentPath);
  // Resolve to a full URL so sub-sub-navigation retains parent path context
  const std::string feedUrl = UrlUtils::buildUrl(serverUrl, currentPath);
  currentPath = UrlUtils::buildUrl(feedUrl, entry.href);

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(currentPath);
}

void CantoLibraryActivity::navigateBack() {
  if (navigationHistory.empty()) {
    onGoHome();
  } else {
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    releaseEntries();
    selectorIndex = 0;
    requestUpdate();
    fetchFeed(currentPath);
  }
}

void CantoLibraryActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadProgress = downloadTotal = 0;
  requestUpdate(true);

  // Build full download URL relative to the current feed, not the root server URL
  const std::string feedUrl = UrlUtils::buildUrl(serverUrl, currentPath);
  std::string downloadUrl = UrlUtils::buildUrl(feedUrl, book.href);

  if (!Storage.exists(CANTO_FOLDER) && !Storage.mkdir(CANTO_FOLDER)) {
    LOG_ERR("CANTO", "mkdir failed for %s", CANTO_FOLDER);
    state = BrowserState::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
    requestUpdate();
    return;
  }

  // The filename derivation must stay in lockstep with localPathFor(): the
  // server registers kosync document identities for exactly these filenames.
  const std::string filename = localPathFor(book);
  LOG_DBG("CANTO", "Downloading: %s -> %s", downloadUrl.c_str(), filename.c_str());

  int lastRenderedPercent = -1;
  unsigned long lastProgressUpdateMs = 0;
  const auto result = HttpDownloader::downloadToFile(
      downloadUrl, filename,
      [this, &lastRenderedPercent, &lastProgressUpdateMs](const size_t downloaded, const size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total) : 0;
        const unsigned long now = millis();
        if (percent >= 100 || lastRenderedPercent < 0 ||
            percent >= lastRenderedPercent + DOWNLOAD_PROGRESS_STEP_PERCENT ||
            now - lastProgressUpdateMs >= DOWNLOAD_PROGRESS_MIN_UPDATE_MS) {
          lastRenderedPercent = percent;
          lastProgressUpdateMs = now;
          requestUpdate(true);
        }
      },
      nullptr, CANTO_STORE.getUsername(), CANTO_STORE.getPassword());

  if (result == HttpDownloader::OK) {
    clearBookCache(filename);
    CANTO_STORE.recordArticle(filename, stripUrnUuid(book.id), book.title, book.author);
    openArticle(filename);
  } else {
    LOG_ERR("CANTO", "Download failed: %d", static_cast<int>(result));
    state = BrowserState::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
    requestUpdate();
  }
}

void CantoLibraryActivity::openArticle(const std::string& path) {
  if (CANTO_STORE.getAutoSync() && WiFi.status() == WL_CONNECTED) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_CANTO_CHECKING_PROGRESS);
    requestUpdate(true);
    // Best-effort: a failed pull still opens the article at its local position.
    CantoSyncHelper::pullApply(renderer, path);
  }
  APP_STATE.openEpubPath = path;
  APP_STATE.saveToFile();
  pendingOpenPath = path;
  // Exiting triggers onExit's silent restart into the reader, which also
  // clears the WiFi/TLS heap fragmentation before the EPUB engine starts.
  onGoHome();
}

void CantoLibraryActivity::flushPendingSync() {
  if (!CANTO_STORE.getAutoSync()) return;
  if (CantoSyncHelper::refreshPendingFlags() == 0) return;
  statusMessage = tr(STR_CANTO_SYNCING);
  requestUpdate(true);
  CantoSyncHelper::pushPending(MAX_PUSH_PER_SESSION);
}

void CantoLibraryActivity::showTriagePopup() {
  if (entries.empty()) return;
  const auto& entry = entries[selectorIndex];
  if (entry.type != OpdsEntryType::BOOK) return;

  const std::string articleId = stripUrnUuid(entry.id);
  if (articleId.empty()) return;

  // Server shelf states, indexed to match the popup options below.
  static constexpr const char* STATES[] = {"inbox", "later", "archived"};
  std::vector<std::string> options;
  options.reserve(3);
  options.push_back(tr(STR_CANTO_MOVE_INBOX));
  options.push_back(tr(STR_CANTO_MOVE_LATER));
  options.push_back(tr(STR_CANTO_ARCHIVE));

  optionPopup.show(StrId::STR_CANTO_ACTIONS, options, 0,
                   [this, articleId](int idx) { performTriage(articleId, STATES[idx]); });
  requestUpdate();
}

void CantoLibraryActivity::performTriage(const std::string& articleId, const char* stateStr) {
  state = BrowserState::LOADING;
  statusMessage = tr(STR_CANTO_SYNCING);
  requestUpdate(true);

  const auto result = CantoApiClient::setArticleState(articleId, stateStr);
  if (result == CantoApiClient::OK) {
    // Reload the current shelf so the moved article disappears/reappears.
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchFeed(currentPath);
    return;
  }

  state = BrowserState::ERROR;
  errorMessage =
      result == CantoApiClient::UNSUPPORTED ? tr(STR_CANTO_TRIAGE_UNSUPPORTED) : CantoApiClient::errorString(result);
  requestUpdate();
}

void CantoLibraryActivity::launchSearch() {
  consumeConfirm = true;
  state = BrowserState::SEARCH_INPUT;
  requestUpdate();

  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH));
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    state = BrowserState::BROWSING;
    if (!result.isCancelled) {
      performSearch(std::get<KeyboardResult>(result.data).text);
    } else {
      requestUpdate();
    }
  });
}

void CantoLibraryActivity::performSearch(const std::string& query) {
  if (query.empty() || searchTemplate.empty()) {
    state = BrowserState::BROWSING;
    requestUpdate();
    return;
  }

  auto urlEncode = [](const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        out += static_cast<char>(c);
      else {
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", c);
        out += buf;
      }
    }
    return out;
  };

  std::string url = searchTemplate;
  const std::string placeholder = "{searchTerms}";
  const size_t pos = url.find(placeholder);
  if (pos != std::string::npos) url.replace(pos, placeholder.length(), urlEncode(query));

  navigationHistory.push_back(currentPath);
  currentPath = url;

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  releaseEntries();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(url);
}

void CantoLibraryActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    flushPendingSync();
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchFeed(currentPath);
    return;
  }
  launchWifiSelection();
}

void CantoLibraryActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void CantoLibraryActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = BrowserState::LOADING;
    flushPendingSync();
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchFeed(currentPath);
  } else {
    // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
