#include "CantoSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CantoAuthActivity.h"
#include "CantoStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MENU_ITEMS = 6;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_CANTO_SERVER_URL,   StrId::STR_USERNAME,
                                     StrId::STR_PASSWORD,           StrId::STR_CANTO_START_SCREEN,
                                     StrId::STR_CANTO_AUTO_SYNC,    StrId::STR_CANTO_TEST_CONNECTION};
}  // namespace

void CantoSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  requestUpdate();
}

void CantoSettingsActivity::onExit() { Activity::onExit(); }

void CantoSettingsActivity::loop() {
  auto activateSelected = [this] { handleSelection(); };

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  int touchSel = static_cast<int>(selectedIndex);
  const auto listTouch = handleListTouch(touchSel, MENU_ITEMS, contentTop, contentHeight, false);
  if (listTouch != ListTouchResult::None) {
    selectedIndex = static_cast<size_t>(touchSel);
    if (listTouch == ListTouchResult::Activated) activateSelected();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  });
}

void CantoSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    // Server URL - prefill with https:// if empty to save typing
    const std::string currentUrl = CANTO_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CANTO_SERVER_URL),
                                                                   prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               CANTO_STORE.setServerUrl(urlToSave);
                               CANTO_STORE.saveToFile();
                             }
                           });
  } else if (selectedIndex == 1) {
    // Username
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_USERNAME),
                                                                   CANTO_STORE.getUsername(), 64, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               CANTO_STORE.setCredentials(kb.text, CANTO_STORE.getPassword());
                               CANTO_STORE.saveToFile();
                             }
                           });
  } else if (selectedIndex == 2) {
    // Password
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD),
                                                                   CANTO_STORE.getPassword(), 64, InputType::Password),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               CANTO_STORE.setCredentials(CANTO_STORE.getUsername(), kb.text);
                               CANTO_STORE.saveToFile();
                             }
                           });
  } else if (selectedIndex == 3) {
    // Canto start screen - toggle
    CANTO_STORE.setEnabled(!CANTO_STORE.getEnabled());
    CANTO_STORE.saveToFile();
    requestUpdate();
  } else if (selectedIndex == 4) {
    // Auto sync - toggle
    CANTO_STORE.setAutoSync(!CANTO_STORE.getAutoSync());
    CANTO_STORE.saveToFile();
    requestUpdate();
  } else if (selectedIndex == 5) {
    // Test connection
    if (!CANTO_STORE.isConfigured()) {
      return;
    }
    startActivityForResult(std::make_unique<CantoAuthActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

void CantoSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CANTO));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(MENU_ITEMS),
      static_cast<int>(selectedIndex), [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr,
      nullptr,
      [this](int index) {
        if (index == 0) {
          auto serverUrl = CANTO_STORE.getServerUrl();
          return serverUrl.empty() ? std::string(tr(STR_NOT_SET)) : serverUrl;
        } else if (index == 1) {
          auto username = CANTO_STORE.getUsername();
          return username.empty() ? std::string(tr(STR_NOT_SET)) : username;
        } else if (index == 2) {
          return CANTO_STORE.getPassword().empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
        } else if (index == 3) {
          return CANTO_STORE.getEnabled() ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
        } else if (index == 4) {
          return CANTO_STORE.getAutoSync() ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
        } else if (index == 5) {
          return CANTO_STORE.isConfigured() ? std::string("")
                                            : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
        }
        return std::string(tr(STR_NOT_SET));
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
