#include "CantoSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "canto/CantoSyncHelper.h"
#include "components/UITheme.h"
#include "fontIds.h"

void CantoSyncActivity::onEnter() {
  Activity::onEnter();

  pendingCount = CantoSyncHelper::refreshPendingFlags();
  if (pendingCount == 0) {
    state = DONE;
    statusMessage = tr(STR_CANTO_NOTHING_TO_SYNC);
    requestUpdate();
    return;
  }

  wifiActivated = true;
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void CantoSyncActivity::onExit() {
  Activity::onExit();

  if (wifiActivated && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void CantoSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    {
      RenderLock lock(*this);
      state = FAILED;
      statusMessage = tr(STR_WIFI_CONN_FAILED);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = SYNCING;
    statusMessage = tr(STR_CANTO_SYNCING);
  }
  requestUpdate(true);

  performSync();
}

void CantoSyncActivity::performSync() {
  pushedCount = CantoSyncHelper::pushPending();

  {
    RenderLock lock(*this);
    if (pushedCount == pendingCount) {
      state = DONE;
      statusMessage = tr(STR_CANTO_SYNCED);
    } else {
      state = FAILED;
      char buf[64];
      snprintf(buf, sizeof(buf), "%s (%u/%u)", tr(STR_CANTO_SYNC_FAILED), (unsigned)pushedCount,
               (unsigned)pendingCount);
      statusMessage = buf;
    }
  }
  requestUpdate();
}

void CantoSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CANTO_SYNC_NOW));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  renderer.drawCenteredText(UI_10_FONT_ID, top, statusMessage.c_str(), true,
                            state == SYNCING ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), state == SYNCING ? "" : tr(STR_DONE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void CantoSyncActivity::loop() {
  if (state == DONE || state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
  }
}
