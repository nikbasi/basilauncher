#include "wifi_session.h"

#include "board_hal.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstring>
#include <time.h>

namespace {

constexpr const char* kApSsid = "Basilauncher";
constexpr const char* kDefaultApPass = "basilauncher";
constexpr uint8_t kApChannel = 1;
DNSServer* gDns = nullptr;
WifiMode gMode = WifiMode::Off;
char gSavedSsid[33] = {};
char gSavedPass[65] = {};
char gApPass[64] = {};
bool gCredsLoaded = false;
bool gApLoaded = false;

bool passwordOk(const char* pass) {
  if (!pass) return false;
  const size_t n = strlen(pass);
  if (n < 8 || n > 63) return false;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(pass[i]);
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

void loadApPass() {
  if (gApLoaded) return;
  gApLoaded = true;
  snprintf(gApPass, sizeof(gApPass), "%s", kDefaultApPass);
  Preferences prefs;
  if (!prefs.begin("basil", true)) return;
  const String pass = prefs.getString("apPass", "");
  prefs.end();
  if (passwordOk(pass.c_str())) snprintf(gApPass, sizeof(gApPass), "%s", pass.c_str());
}

void loadCreds() {
  if (gCredsLoaded) return;
  gCredsLoaded = true;
  gSavedSsid[0] = 0;
  gSavedPass[0] = 0;
  Preferences prefs;
  if (!prefs.begin("basil", true)) return;
  const String ssid = prefs.getString("wifiSsid", "");
  const String pass = prefs.getString("wifiPass", "");
  prefs.end();
  if (ssid.length() > 0 && ssid.length() < sizeof(gSavedSsid)) {
    snprintf(gSavedSsid, sizeof(gSavedSsid), "%s", ssid.c_str());
  }
  if (pass.length() < sizeof(gSavedPass)) {
    snprintf(gSavedPass, sizeof(gSavedPass), "%s", pass.c_str());
  }
}

void stopDns() {
  if (!gDns) return;
  gDns->stop();
  delete gDns;
  gDns = nullptr;
}

void fillIp(char* out, size_t n, IPAddress ip) {
  snprintf(out, n, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

}  // namespace

const char* wifiApSsid() {
  return kApSsid;
}

const char* wifiApPassword() {
  loadApPass();
  return gApPass;
}

bool wifiSetApPassword(const char* pass) {
  if (!passwordOk(pass)) return false;
  snprintf(gApPass, sizeof(gApPass), "%s", pass);
  gApLoaded = true;
  Preferences prefs;
  if (!prefs.begin("basil", false)) return true;
  prefs.putString("apPass", gApPass);
  prefs.end();
  Serial.println("WiFi: hotspot password updated");
  return true;
}

bool wifiHasSavedNetwork() {
  loadCreds();
  return gSavedSsid[0] != 0;
}

void wifiGetSavedSsid(char* out, size_t outLen) {
  loadCreds();
  if (!out || outLen == 0) return;
  snprintf(out, outLen, "%s", gSavedSsid);
}

void wifiSaveNetwork(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) return;
  snprintf(gSavedSsid, sizeof(gSavedSsid), "%.32s", ssid);
  snprintf(gSavedPass, sizeof(gSavedPass), "%.64s", pass ? pass : "");
  gCredsLoaded = true;
  Preferences prefs;
  if (!prefs.begin("basil", false)) return;
  prefs.putString("wifiSsid", gSavedSsid);
  prefs.putString("wifiPass", gSavedPass);
  prefs.end();
  Serial.printf("WiFi: saved network '%s'\n", gSavedSsid);
}

void wifiClearSavedNetwork() {
  gSavedSsid[0] = 0;
  gSavedPass[0] = 0;
  gCredsLoaded = true;
  Preferences prefs;
  if (!prefs.begin("basil", false)) return;
  prefs.remove("wifiSsid");
  prefs.remove("wifiPass");
  prefs.end();
}

bool wifiStartSoftAp() {
  wifiStop();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  delay(50);

  // Pin SoftAP to 192.168.4.1 before start (Arduino-ESP32 DHCP/gateway).
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  if (!WiFi.softAPConfig(apIp, gateway, subnet)) {
    Serial.println("WiFi: softAPConfig failed");
  }

  loadApPass();
  const bool ok = WiFi.softAP(kApSsid, gApPass, kApChannel, 0, 4);
  if (!ok) {
    Serial.println("WiFi: SoftAP failed");
    WiFi.mode(WIFI_OFF);
    gMode = WifiMode::Off;
    return false;
  }

  // Re-apply after softAP — some cores ignore the pre-start config.
  WiFi.softAPConfig(apIp, gateway, subnet);

  for (int i = 0; i < 50; ++i) {
    delay(20);
    if (WiFi.softAPIP() == apIp) break;
  }
  if (WiFi.softAPIP() != apIp) {
    Serial.printf("WiFi: SoftAP IP unexpected: %s\n", WiFi.softAPIP().toString().c_str());
  }
  gMode = WifiMode::SoftAp;

  gDns = new DNSServer();
  if (gDns) {
    gDns->setErrorReplyCode(DNSReplyCode::NoError);
    gDns->start(53, "*", apIp);
  }

  Serial.printf("WiFi: SoftAP '%s' ip=%s\n", kApSsid, WiFi.softAPIP().toString().c_str());
  return true;
}

bool wifiStartStation(uint32_t timeoutMs) {
  loadCreds();
  if (!gSavedSsid[0]) return false;
  wifiStop();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(gSavedSsid, gSavedPass[0] ? gSavedPass : nullptr);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi: STA failed for '%s'\n", gSavedSsid);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    gMode = WifiMode::Off;
    return false;
  }
  gMode = WifiMode::Station;
  Serial.printf("WiFi: STA '%s' ip=%s\n", gSavedSsid, WiFi.localIP().toString().c_str());
  return true;
}

void wifiStop() {
  stopDns();
  if (gMode == WifiMode::SoftAp) {
    WiFi.softAPdisconnect(true);
  } else if (gMode == WifiMode::Station) {
    WiFi.disconnect(true);
  }
  WiFi.mode(WIFI_OFF);
  gMode = WifiMode::Off;
}

bool wifiIsActive() {
  return gMode != WifiMode::Off;
}

WifiMode wifiCurrentMode() {
  return gMode;
}

WifiStatus wifiGetStatus() {
  WifiStatus st;
  st.mode = gMode;
  if (gMode == WifiMode::SoftAp) {
    st.connected = true;
    snprintf(st.ssid, sizeof(st.ssid), "%s", kApSsid);
    // Always advertise the pinned SoftAP address (matches softAPConfig).
    snprintf(st.ip, sizeof(st.ip), "192.168.4.1");
    snprintf(st.url, sizeof(st.url), "http://192.168.4.1/");
    snprintf(st.detail, sizeof(st.detail), "WPA2 password is on this screen");
  } else if (gMode == WifiMode::Station) {
    st.connected = WiFi.status() == WL_CONNECTED;
    snprintf(st.ssid, sizeof(st.ssid), "%s", WiFi.SSID().c_str());
    if (st.connected) {
      fillIp(st.ip, sizeof(st.ip), WiFi.localIP());
      snprintf(st.url, sizeof(st.url), "http://%s/", st.ip);
      st.rssi = WiFi.RSSI();
      snprintf(st.detail, sizeof(st.detail), "RSSI %d dBm", st.rssi);
    } else {
      snprintf(st.detail, sizeof(st.detail), "Disconnected");
    }
  } else {
    snprintf(st.detail, sizeof(st.detail), "Wi-Fi off");
  }
  return st;
}

void wifiPoll() {
  if (gDns) gDns->processNextRequest();
}

bool wifiSyncClock(uint32_t timeoutMs) {
  if (gMode != WifiMode::Station || WiFi.status() != WL_CONNECTED) return false;

  // UTC; RTC is wall-clock without TZ until we add a setting.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const uint32_t start = millis();
  time_t now = 0;
  while ((millis() - start) < timeoutMs) {
    now = time(nullptr);
    if (now > 1700000000) break;  // past 2023
    delay(100);
  }
  if (now < 1700000000) {
    Serial.println("WiFi: NTP failed");
    return false;
  }
  struct tm tm;
  gmtime_r(&now, &tm);
  const bool ok =
      boardSetClock(static_cast<uint16_t>(tm.tm_year + 1900), static_cast<uint8_t>(tm.tm_mon + 1),
                    static_cast<uint8_t>(tm.tm_mday), static_cast<uint8_t>(tm.tm_hour),
                    static_cast<uint8_t>(tm.tm_min));
  Serial.printf("WiFi: NTP %s -> %04d-%02d-%02d %02d:%02d UTC\n", ok ? "ok" : "rtc fail",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
  return ok;
}
