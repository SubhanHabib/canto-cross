#pragma once

#include "activities/Activity.h"

/**
 * Modal "Sync Now" flow: connects to WiFi and pushes every pending reading
 * position to the Canto server, then reports the result. Skips WiFi entirely
 * when nothing is pending. Follows the network-activity teardown convention
 * (disconnect + silent restart on exit when WiFi was used).
 */
class CantoSyncActivity final : public Activity {
 public:
  explicit CantoSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CantoSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == SYNCING; }

 private:
  enum State { WIFI_SELECTION, SYNCING, DONE, FAILED };

  State state = WIFI_SELECTION;
  bool wifiActivated = false;
  size_t pushedCount = 0;
  size_t pendingCount = 0;
  std::string statusMessage;

  void onWifiSelectionComplete(bool success);
  void performSync();
};
