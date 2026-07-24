#pragma once

#include "activities/Activity.h"

/**
 * Tests the Canto server credentials: connects to WiFi, then authenticates
 * against the server's kosync endpoint (which shares device credentials with
 * the OPDS endpoint, so one successful auth validates both).
 */
class CantoAuthActivity final : public Activity {
 public:
  explicit CantoAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CantoAuth", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == AUTHENTICATING; }

 private:
  enum State { WIFI_SELECTION, CONNECTING, AUTHENTICATING, SUCCESS, FAILED };

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string errorMessage;

  void onWifiSelectionComplete(bool success);
  void performAuthentication();
};
