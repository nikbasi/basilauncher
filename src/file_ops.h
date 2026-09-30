#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t kFilePathMax = 192;
constexpr size_t kTextEditMax = 6144;  // leave headroom under DRAM pressure

struct FileClipboard {
  char path[kFilePathMax] = {};
  bool isCut = false;
  bool hasItem = false;
};

FileClipboard& fileClipboard();
void fileClipboardClear();
void fileClipboardSet(const char* path, bool cut);

bool fileOpsJoin(const char* dir, const char* name, char* out, size_t outLen);
bool fileOpsBasename(const char* path, char* out, size_t outLen);
bool fileOpsEndsWith(const char* name, const char* ext);  // case-insensitive ext
bool fileOpsIsBin(const char* name);
bool fileOpsIsBmp(const char* name);
bool fileOpsIsText(const char* name);

bool fileOpsExists(const char* path);
bool fileOpsIsDir(const char* path);
bool fileOpsDirEmpty(const char* path);
bool fileOpsMkdir(const char* path);
bool fileOpsRemove(const char* path);  // file or empty directory
bool fileOpsRename(const char* from, const char* to);
bool fileOpsCopyFile(const char* from, const char* to);

// Paste clipboard into destDir. On success clears clipboard when cut.
// Writes a short human message into err on failure.
bool fileClipboardPaste(const char* destDir, char* err, size_t errLen);

bool fileOpsLoadText(const char* path, char* buf, size_t bufLen, size_t* outLen);
bool fileOpsSaveText(const char* path, const char* text, size_t len);
