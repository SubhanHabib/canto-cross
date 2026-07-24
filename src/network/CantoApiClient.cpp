#include "CantoApiClient.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <SecureHttpClient.h>
#include <base64.h>

#include "CantoStore.h"

int CantoApiClient::lastHttpCode = 0;

namespace {
// Same conservative TLS heap floor as KOReaderSyncClient.
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;

// The TLS floor only applies to https:// URLs; a plain-http request performs no
// handshake and needs far less contiguous heap (see KOReaderSyncClient).
bool insufficientHeap(const std::string& url) {
  if (url.rfind("https://", 0) != 0) {
    return false;  // plain http: no TLS handshake, no contiguous-heap floor
  }
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_HEAP_FOR_TLS || maxAllocHeap < MIN_HEAP_FOR_TLS) {
    LOG_ERR("CANTO", "Insufficient heap for TLS: %u free, %u max alloc (need %u)", freeHeap, maxAllocHeap,
            MIN_HEAP_FOR_TLS);
    return true;
  }
  return false;
}

std::string md5Hex(const std::string& input) {
  if (input.empty()) return "";
  MD5Builder md5;
  md5.begin();
  md5.add(input.c_str());
  md5.calculate();
  return md5.toString().c_str();
}

// Discards the response body as it streams in. Only the status code matters
// here, and buffering the body (the default sendRequest overload) can abort()
// on OOM when a server answers with a large HTML error page: std::string's
// append grows via the throwing operator new, which -fno-exceptions app code
// cannot catch (observed on-device against a dev server's 404 page).
bool discardBody(const uint8_t*, size_t) { return true; }

// Same device auth headers the server's other /api/xt/* endpoints accept.
void applyAuthHeaders(freeink::SecureHttpClient& http) {
  http.addHeader("x-auth-user", CANTO_STORE.getUsername());
  http.addHeader("x-auth-key", md5Hex(CANTO_STORE.getPassword()));
  const std::string credentials = CANTO_STORE.getUsername() + ":" + CANTO_STORE.getPassword();
  const String encoded = base64::encode(credentials.c_str());
  http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
}
}  // namespace

CantoApiClient::Error CantoApiClient::setArticleState(const std::string& articleId, const char* state) {
  lastHttpCode = 0;
  if (!CANTO_STORE.hasCredentials()) {
    return NO_CREDENTIALS;
  }
  if (articleId.empty()) {
    LOG_ERR("CANTO", "No article id for triage call");
    return UNSUPPORTED;
  }

  const std::string url = CANTO_STORE.getBaseUrl() + "/api/xt/articles/" + articleId + "/state";
  LOG_DBG("CANTO", "Triage: %s -> %s (heap: %u)", url.c_str(), state, (unsigned)ESP.getFreeHeap());
  if (insufficientHeap(url)) return LOW_MEMORY;

  JsonDocument doc;
  doc["state"] = state;
  std::string body;
  serializeJson(doc, body);

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("CANTO", "Bad URL: %s", url.c_str());
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  http.addHeader("Content-Type", "application/json");
  const int httpCode =  // status-only; see discardBody
      http.sendRequest("POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(), discardBody);
  http.end();
  lastHttpCode = httpCode;

  LOG_DBG("CANTO", "Triage response: %d", httpCode);

  if (httpCode <= 0) return NETWORK_ERROR;
  if (httpCode == 200) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode == 404) return UNSUPPORTED;
  return SERVER_ERROR;
}

const char* CantoApiClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_CREDENTIALS:
      return "No credentials configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Authentication failed";
    case UNSUPPORTED:
      return "Server doesn't support this";
    case SERVER_ERROR:
      return "Server error (try again later)";
    case LOW_MEMORY:
      return "Not enough memory — please retry";
    default:
      return "Unknown error";
  }
}
