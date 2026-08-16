#include "../core/options.h"
#ifdef USE_DLNA

#include "dlna_desc.h"
#include "dlna_http_guard.h"
#include <WiFiClient.h>
#include <HTTPClient.h>

namespace {
class CooperativeDlnaClient : public WiFiClient {
  public:
    int available() override {
      serviceIdle();
      return WiFiClient::available();
    }

    int read() override {
      serviceIdle();
      uint8_t value = 0;
      const int result = WiFiClient::read(&value, 1);
      if (result < 0) return result;
      return result == 0 ? -1 : value;
    }

    int read(uint8_t* buffer, size_t size) override {
      serviceIdle();
      return WiFiClient::read(buffer, size);
    }

  private:
    void serviceIdle() {
      if ((++operationCount & 0x0FU) == 0U) vTaskDelay(1);
    }

    uint8_t operationCount = 0;
};
} // namespace

bool DlnaDescription::resolveControlURL(const String& descUrl, String& outControlUrl) {
  outControlUrl = "";

  HTTPClient http;
  CooperativeDlnaClient client;

  Serial.printf("[DLNA] GET %s\n", descUrl.c_str());
  
   DlnaHttpGuard lock;
  
  if (!http.begin(client, descUrl)) {
    Serial.println("[DLNA] HTTP begin failed");
    return false;
  }

  http.setConnectTimeout(3000);
  http.setTimeout(4000);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[DLNA] HTTP error: %d\n", code);
    http.end();
    return false;
  }

  String controlPath;
  bool ok = parseStream(*http.getStreamPtr(), controlPath);
  http.end();

  if (!ok) {
    Serial.println("[DLNA] ContentDirectory not found");
    return false;
  }

  String base = extractBaseUrl(descUrl);
  outControlUrl = base + controlPath;

  return true;
}

bool DlnaDescription::parseStream(Stream& s, String& outControlPath) {
  bool inService = false;
  bool isContentDir = false;

  String line;
  while (s.available()) {
    line = s.readStringUntil('\n');
    line.trim();

    if (line.indexOf("<service>") >= 0) {
      inService = true;
      isContentDir = false;
    }

    // EXAKT serviceType ellenőrzés
    if (inService &&
        line.indexOf("<serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>") >= 0) {
      isContentDir = true;
    }

    if (inService && isContentDir &&
        line.indexOf("<controlURL>") >= 0) {
      int a = line.indexOf("<controlURL>") + 12;
      int b = line.indexOf("</controlURL>");
      if (b > a) {
        outControlPath = line.substring(a, b);
        outControlPath.trim();
        return true;
      }
    }

    if (line.indexOf("</service>") >= 0) {
      inService = false;
      isContentDir = false;
    }
  }
  return false;
}

String DlnaDescription::extractBaseUrl(const String& fullUrl) {
  // http://IP:PORT/desc/device.xml → http://IP:PORT
  int idx = fullUrl.indexOf('/', 8); // 8 = after http://
  if (idx > 0) return fullUrl.substring(0, idx);
  return fullUrl;
}
#endif // USE_DLNA
