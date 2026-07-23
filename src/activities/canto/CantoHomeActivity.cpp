#include "CantoHomeActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CantoSettingsActivity.h"
#include "CantoStore.h"
#include "CantoSyncActivity.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "canto/CantoSyncHelper.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Bound the article rows so the fixed menu rows always stay on screen.
constexpr size_t MAX_HOME_ARTICLES = 5;
}  // namespace

void CantoHomeActivity::reloadContent() {
  configured = CANTO_STORE.isConfigured();
  articles.clear();
  pendingCount = 0;
  if (!configured) return;

  const auto& indexed = CANTO_STORE.getArticles();
  articles.reserve(std::min(indexed.size(), MAX_HOME_ARTICLES));
  for (const auto& article : indexed) {
    if (articles.size() >= MAX_HOME_ARTICLES) break;
    if (!Storage.exists(article.path.c_str())) continue;  // deleted off-device
    const auto lastSlash = article.path.rfind('/');
    const std::string fallback = lastSlash != std::string::npos ? article.path.substr(lastSlash + 1) : article.path;
    articles.push_back(HomeArticle{article.path, article.title.empty() ? fallback : article.title});
  }

  // Offline SD diff of progress.bin snapshots — keeps the Sync Now hint fresh
  // without any reader hooks.
  pendingCount = CantoSyncHelper::refreshPendingFlags();
}

void CantoHomeActivity::onEnter() {
  Activity::onEnter();
  reloadContent();
  selectorIndex = 0;
  backPressSeen = false;
  requestUpdate();
}

void CantoHomeActivity::onExit() { Activity::onExit(); }

void CantoHomeActivity::openSettings() {
  startActivityForResult(std::make_unique<CantoSettingsActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) {
                           reloadContent();
                           selectorIndex = 0;
                           requestUpdate();
                         });
}

void CantoHomeActivity::activateSelection() {
  if (selectorIndex < static_cast<int>(articles.size())) {
    activityManager.goToReader(articles[selectorIndex].path);
    return;
  }
  switch (selectorIndex - static_cast<int>(articles.size())) {
    case MENU_LIBRARY:
      activityManager.goToCantoLibrary();
      break;
    case MENU_SYNC:
      startActivityForResult(std::make_unique<CantoSyncActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               reloadContent();
                               requestUpdate();
                             });
      break;
    case MENU_DEVICE:
      activityManager.goToDeviceHome();
      break;
    case MENU_SETTINGS:
      openSettings();
      break;
    default:
      break;
  }
}

void CantoHomeActivity::loop() {
  if (!configured) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen = true;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backPressSeen) {
      activityManager.goToDeviceHome();
      return;
    }
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
      openSettings();
    }
    return;
  }

  const int itemCount = getItemCount();

  buttonNavigator.onNext([this, itemCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, itemCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, itemCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, itemCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, itemCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, itemCount);
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen = true;

  // Back is otherwise unused here: continue the most recent article.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backPressSeen && !articles.empty()) {
    activityManager.goToReader(articles[0].path);
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int menuTop = metrics.homeTopPadding;
  int menuRow = -1;
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, metrics.menuRowHeight + metrics.menuSpacing,
                                              itemCount, 0, INT32_MAX, metrics.menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != menuRow) {
        selectorIndex = menuRow;
        requestUpdate();
      }
    } else {
      selectorIndex = menuRow;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void CantoHomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CANTO));

  if (!configured) {
    const int top = pageHeight / 2 - 20;
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CANTO_NOT_CONFIGURED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + 30, tr(STR_CANTO_SETUP_HINT));
    const auto labels = mappedInput.mapLabels(tr(STR_CANTO_DEVICE_HOME), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Sync Now label carries the pending count when there is anything to push.
  char syncLabel[48];
  if (pendingCount > 0) {
    snprintf(syncLabel, sizeof(syncLabel), "%s (%u)", tr(STR_CANTO_SYNC_NOW), (unsigned)pendingCount);
  } else {
    snprintf(syncLabel, sizeof(syncLabel), "%s", tr(STR_CANTO_SYNC_NOW));
  }

  std::vector<std::string> items;
  std::vector<UIIcon> icons;
  items.reserve(articles.size() + MENU_ROW_COUNT);
  icons.reserve(articles.size() + MENU_ROW_COUNT);
  for (const auto& article : articles) {
    items.push_back(article.title);
    icons.push_back(Book);
  }
  items.push_back(tr(STR_CANTO_LIBRARY));
  icons.push_back(Library);
  items.push_back(syncLabel);
  icons.push_back(Transfer);
  items.push_back(tr(STR_CANTO_DEVICE_HOME));
  icons.push_back(Folder);
  items.push_back(tr(STR_SETTINGS_TITLE));
  icons.push_back(Settings);

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding, pageWidth,
           pageHeight - (metrics.homeTopPadding + metrics.verticalSpacing + metrics.buttonHintsHeight)},
      static_cast<int>(items.size()), selectorIndex, [&items](int index) { return items[index]; },
      [&icons](int index) { return icons[index]; });

  const auto labels = mappedInput.mapLabels(articles.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
