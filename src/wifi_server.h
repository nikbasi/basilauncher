#pragma once

bool wifiServerStart();
void wifiServerStop();
void wifiServerPoll();
bool wifiServerRunning();

// Last upload result for the device UI (empty if none).
const char* wifiServerLastMessage();
void wifiServerClearMessage();
