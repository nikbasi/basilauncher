#pragma once

#include <stdint.h>
#include <stddef.h>

enum class WifiMode : uint8_t { Off = 0, SoftAp, Station };

struct WifiStatus {
  WifiMode mode = WifiMode::Off;
  bool connected = false;
  char ssid[33] = {};
  char ip[16] = {};
  char url[40] = {};
  int rssi = 0;
  char detail[48] = {};
};

bool wifiHasSavedNetwork();
void wifiSaveNetwork(const char* ssid, const char* pass);
void wifiClearSavedNetwork();
void wifiGetSavedSsid(char* out, size_t outLen);
// True when this SSID has a stored passphrase. Newest networks stay first.
bool wifiLookupPassword(const char* ssid, char* out, size_t outLen);

// Start WPA2 SoftAP "Basilauncher". Returns false on failure.
bool wifiStartSoftAp();

// Hotspot passphrase (8-63 printable characters). Default is "basilauncher".
const char* wifiApPassword();
bool wifiSetApPassword(const char* pass);

// Join saved network (blocking up to timeoutMs). Returns false on failure.
bool wifiStartStation(uint32_t timeoutMs = 20000);

// Save these credentials, then join. An empty password is an open network.
bool wifiJoin(const char* ssid, const char* pass, uint32_t timeoutMs = 20000);

struct WifiAp {
  char ssid[33];
  int rssi;
  bool open;
};

// Blocking scan. Keeps an existing station connection. Returns how many
// entries were written, strongest first.
int wifiScan(WifiAp* out, int maxOut);

void wifiStop();
bool wifiIsActive();
bool wifiIsHotspot();
bool wifiIsStation();
WifiMode wifiCurrentMode();
WifiStatus wifiGetStatus();

// Poll DNS (captive) - call from loop while Wi-Fi screen is open.
void wifiPoll();

// Best-effort NTP -> RTC. Requires Station with internet.
bool wifiSyncClock(uint32_t timeoutMs = 8000);

// SoftAP SSID used for WIFI: QR payloads.
const char* wifiApSsid();
