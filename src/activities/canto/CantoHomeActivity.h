#pragma once
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * The Canto-first landing screen: recently downloaded articles (offline, from
 * the CantoStore index) followed by Library / Sync Now / Device / Settings
 * menu rows. Back opens the most recent article directly, mirroring the
 * CrossPoint home shortcut. When Canto is not configured yet it shows a setup
 * hint instead and Confirm opens the Canto settings.
 */
class CantoHomeActivity final : public Activity {
 public:
  explicit CantoHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CantoHome", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  // Menu rows that follow the article entries.
  enum MenuRow { MENU_LIBRARY = 0, MENU_SYNC, MENU_DEVICE, MENU_SETTINGS, MENU_ROW_COUNT };

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool configured = false;
  size_t pendingCount = 0;
  // See HomeActivity: ignore the stale Back release from the previous activity.
  bool backPressSeen = false;

  struct HomeArticle {
    std::string path;
    std::string title;
  };
  std::vector<HomeArticle> articles;

  int getItemCount() const { return static_cast<int>(articles.size()) + MENU_ROW_COUNT; }
  void reloadContent();
  void activateSelection();
  void openSettings();
};
