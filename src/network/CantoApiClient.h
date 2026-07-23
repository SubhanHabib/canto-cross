#pragma once
#include <string>

/**
 * Minimal client for the Canto server's device REST endpoints beyond
 * OPDS/kosync. Currently a single call: article triage (shelf changes).
 *
 * The triage endpoint is a documented follow-up for the Canto server (see
 * docs/canto.md); until it ships, servers answer 404 which is surfaced as
 * UNSUPPORTED so the UI can degrade gracefully.
 */
class CantoApiClient {
 public:
  enum Error {
    OK = 0,
    NO_CREDENTIALS,
    NETWORK_ERROR,
    AUTH_FAILED,
    UNSUPPORTED,  // Endpoint absent (404) — server predates the triage API
    SERVER_ERROR,
    LOW_MEMORY
  };

  /**
   * Moves an article to a shelf: POST {base}/api/xt/articles/{id}/state with
   * body {"state":"inbox"|"later"|"archived"}. WiFi must be connected.
   */
  static Error setArticleState(const std::string& articleId, const char* state);

  static const char* errorString(Error error);

  /** HTTP status code from the last request (for diagnostics). */
  static int lastHttpCode;
};
