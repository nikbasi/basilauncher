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

// Start open SoftAP "Basilauncher". Returns false on failure.
bool wifiStartSoftAp();

// Join saved network (blocking up to timeoutMs). Returns false on failure.
bool wifiStartStation(uint32_t timeoutMs = 20000);

void wifiStop();
bool wifiIsActive();
WifiMode wifiCurrentMode();
WifiStatus wifiGetStatus();

// Poll DNS (captive) - call from loop while Wi-Fi screen is open.
void wifiPoll();

// Best-effort NTP -> RTC. Requires Station with internet.
bool wifiSyncClock(uint32_t timeoutMs = 8000);

// SoftAP SSID used for WIFI: QR payloads.
const char* wifiApSsid();
