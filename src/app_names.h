#pragma once

// Turning firmware images into names people recognise, and comparing their
// versions. Pure string work, so the host preview and tests can use it too.

#include <string>

// "crosspoint-1.5.33-t5s3.bin" -> title "Crosspoint", version "1.5.33".
// "T5S3-GameBoy-v1.2.bin"      -> title "GameBoy",    version "1.2".
// Board tags (t5s3, esp32s3, lilygo, ...) are dropped from the title unless
// nothing else is left. version is empty when the name carries none.
void appsTitleFromFileName(const char* fileName, std::string& title, std::string& version);

// An ESP-IDF app description is only worth showing when the build named
// itself: Arduino builds all report "arduino-lib-builder" and the IDF version.
bool appsDescNameUseful(const char* projectName);
bool appsDescVersionUseful(const char* version);

// "my_reader" -> "My reader": separators become spaces, first letter upper.
std::string appsPrettyName(const char* projectName);

// Lower-case letters and digits only, for matching an installed app to a
// newer file of the same app.
std::string appsMatchKey(const std::string& title);

// Numeric, component-wise: "1.10" > "1.9", "v2" == "2", "1.5.33" < "1.5.34".
// Returns <0, 0 or >0. Empty versions compare equal to everything.
int appsCompareVersions(const std::string& a, const std::string& b);
