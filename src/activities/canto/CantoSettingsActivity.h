#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Submenu for the Canto Reader layer: server URL, device credentials,
 * start-screen and auto-sync toggles, and a connection test.
 */
class CantoSettingsActivity final : public Activity {
 public:
  explicit CantoSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CantoSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  size_t selectedIndex = 0;

  void handleSelection();
};
