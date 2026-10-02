#include "file_ops.h"

#include <SD.h>
#include <cstdio>
#include <cstring>

namespace {

FileClipboard gClip;

char toLowerAscii(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  return c;
}

bool pathIsUnder(const char* path, const char* ancestor) {
  if (!path || !ancestor) return false;
  const size_t alen = strlen(ancestor);
  if (alen == 0) return false;
  if (strncmp(path, ancestor, alen) != 0) return false;
  if (path[alen] == 0) return true;  // exact
  if (ancestor[alen - 1] == '/') return true;
  return path[alen] == '/';
}

}  // namespace

FileClipboard& fileClipboard() { return gClip; }

void fileClipboardClear() {
  gClip.paths.clear();
  gClip.isCut = false;
  gClip.hasItem = false;
}

void fileClipboardSet(const char* path, bool cut) {
  if (!path || !path[0]) {
    fileClipboardClear();
    return;
  }
  gClip.paths = {path};
  gClip.isCut = cut;
  gClip.hasItem = true;
}

void fileClipboardSet(const std::vector<std::string>& paths, bool cut) {
  fileClipboardClear();
  for (const auto& path : paths) {
    if (!path.empty()) gClip.paths.push_back(path);
  }
  if (gClip.paths.empty()) return;
  gClip.isCut = cut;
  gClip.hasItem = true;
}

bool fileOpsJoin(const char* dir, const char* name, char* out, size_t outLen) {
  if (!out || outLen == 0 || !name || !name[0]) return false;
  if (!dir || !dir[0] || (dir[0] == '/' && dir[1] == 0)) {
    return snprintf(out, outLen, "/%s", name) < static_cast<int>(outLen);
  }
  const size_t dlen = strlen(dir);
  if (dir[dlen - 1] == '/') {
    return snprintf(out, outLen, "%s%s", dir, name) < static_cast<int>(outLen);
  }
  return snprintf(out, outLen, "%s/%s", dir, name) < static_cast<int>(outLen);
}

bool fileOpsBasename(const char* path, char* out, size_t outLen) {
  if (!out || outLen == 0) return false;
  if (!path || !path[0]) {
    out[0] = 0;
    return false;
  }
  const char* slash = strrchr(path, '/');
  const char* base = slash ? slash + 1 : path;
  if (!base[0]) {
    out[0] = 0;
    return false;
  }
  snprintf(out, outLen, "%s", base);
  return true;
}

bool fileOpsEndsWith(const char* name, const char* ext) {
  if (!name || !ext) return false;
  const size_t n = strlen(name);
  const size_t e = strlen(ext);
  if (e == 0 || n < e) return false;
  for (size_t i = 0; i < e; ++i) {
    if (toLowerAscii(name[n - e + i]) != toLowerAscii(ext[i])) return false;
  }
  return true;
}

bool fileOpsIsBin(const char* name) { return fileOpsEndsWith(name, ".bin"); }
bool fileOpsIsBmp(const char* name) { return fileOpsEndsWith(name, ".bmp"); }

bool fileOpsIsText(const char* name) {
  return fileOpsEndsWith(name, ".txt") || fileOpsEndsWith(name, ".md") ||
         fileOpsEndsWith(name, ".json") || fileOpsEndsWith(name, ".csv") ||
         fileOpsEndsWith(name, ".log") || fileOpsEndsWith(name, ".yaml") ||
         fileOpsEndsWith(name, ".yml") || fileOpsEndsWith(name, ".ini") ||
         fileOpsEndsWith(name, ".cfg") || fileOpsEndsWith(name, ".conf");
}

bool fileOpsExists(const char* path) { return path && path[0] && SD.exists(path); }

bool fileOpsIsDir(const char* path) {
  if (!fileOpsExists(path)) return false;
  File f = SD.open(path);
  if (!f) return false;
  const bool d = f.isDirectory();
  f.close();
  return d;
}

bool fileOpsDirEmpty(const char* path) {
  File root = SD.open(path);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return false;
  }
  File entry = root.openNextFile();
  while (entry) {
    String raw = entry.name();
    const char* rawC = raw.c_str();
    const char* base = strrchr(rawC, '/');
    base = base ? base + 1 : rawC;
    if (base[0] && strcmp(base, ".") != 0 && strcmp(base, "..") != 0) {
      entry.close();
      root.close();
      return false;
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  return true;
}

bool fileOpsMkdir(const char* path) {
  if (!path || !path[0]) return false;
  if (SD.exists(path)) return fileOpsIsDir(path);
  return SD.mkdir(path);
}

bool fileOpsRemove(const char* path) {
  if (!fileOpsExists(path)) return false;
  if (fileOpsIsDir(path)) {
    if (!fileOpsDirEmpty(path)) return false;
    return SD.rmdir(path);
  }
  return SD.remove(path);
}

bool fileOpsRename(const char* from, const char* to) {
  if (!from || !to || !from[0] || !to[0]) return false;
  if (!SD.exists(from)) return false;
  if (SD.exists(to)) return false;
  return SD.rename(from, to);
}

bool fileOpsCopyFile(const char* from, const char* to) {
  if (!from || !to || !from[0] || !to[0]) return false;
  if (fileOpsIsDir(from)) return false;
  if (SD.exists(to)) return false;

  File src = SD.open(from, FILE_READ);
  if (!src) return false;
  File dst = SD.open(to, FILE_WRITE);
  if (!dst) {
    src.close();
    return false;
  }

  uint8_t buf[4096];
  bool ok = true;
  while (src.available()) {
    const int n = src.read(buf, sizeof(buf));
    if (n < 0) {
      ok = false;
      break;
    }
    if (n == 0) break;
    if (dst.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
  }
  src.close();
  dst.close();
  if (!ok) SD.remove(to);
  return ok;
}

bool fileClipboardPaste(const char* destDir, char* err, size_t errLen) {
  auto setErr = [&](const char* msg) {
    if (err && errLen) snprintf(err, errLen, "%s", msg ? msg : "Failed");
  };
  if (!gClip.hasItem || gClip.paths.empty()) {
    setErr("Clipboard empty");
    return false;
  }
  if (!destDir || !destDir[0]) {
    setErr("Bad folder");
    return false;
  }

  struct PasteItem {
    std::string source;
    std::string dest;
  };
  std::vector<PasteItem> pending;
  pending.reserve(gClip.paths.size());

  // Validate the whole batch first so ordinary errors cannot leave a partially
  // moved selection.
  for (const auto& source : gClip.paths) {
    if (!fileOpsExists(source.c_str())) {
      setErr("Source missing");
      return false;
    }
    char base[96];
    if (!fileOpsBasename(source.c_str(), base, sizeof(base))) {
      setErr("Bad name");
      return false;
    }
    char dest[kFilePathMax];
    if (!fileOpsJoin(destDir, base, dest, sizeof(dest))) {
      setErr("Path too long");
      return false;
    }
    if (source == dest) {
      setErr("Same location");
      return false;
    }
    const bool srcDir = fileOpsIsDir(source.c_str());
    if (srcDir && !gClip.isCut) {
      setErr("Copy folder unsupported");
      return false;
    }
    if (srcDir && pathIsUnder(destDir, source.c_str())) {
      setErr("Can't move into itself");
      return false;
    }
    if (SD.exists(dest)) {
      setErr("Name exists");
      return false;
    }
    for (const auto& item : pending) {
      if (item.dest == dest) {
        setErr("Duplicate name");
        return false;
      }
    }
    pending.push_back({source, dest});
  }

  for (size_t i = 0; i < pending.size(); ++i) {
    const auto& item = pending[i];
    const bool ok = gClip.isCut ? fileOpsRename(item.source.c_str(), item.dest.c_str())
                                : fileOpsCopyFile(item.source.c_str(), item.dest.c_str());
    if (!ok) {
      if (gClip.isCut) {
        std::vector<std::string> remaining;
        for (size_t j = i; j < pending.size(); ++j) remaining.push_back(pending[j].source);
        fileClipboardSet(remaining, true);
      }
      if (i > 0) setErr(gClip.isCut ? "Some moved; remaining kept" : "Some copied; batch stopped");
      else setErr(gClip.isCut ? "Move failed" : "Copy failed");
      return false;
    }
  }
  if (gClip.isCut) fileClipboardClear();
  return true;
}

bool fileOpsLoadText(const char* path, char* buf, size_t bufLen, size_t* outLen) {
  if (outLen) *outLen = 0;
  if (!path || !buf || bufLen < 2) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  const size_t sz = f.size();
  if (sz >= bufLen) {
    f.close();
    return false;
  }
  const int n = f.read(reinterpret_cast<uint8_t*>(buf), sz);
  f.close();
  if (n < 0 || static_cast<size_t>(n) != sz) return false;
  buf[sz] = 0;
  if (outLen) *outLen = sz;
  return true;
}

bool fileOpsSaveText(const char* path, const char* text, size_t len) {
  if (!path || !path[0]) return false;
  char tmp[kFilePathMax];
  if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= static_cast<int>(sizeof(tmp))) return false;

  if (SD.exists(tmp)) SD.remove(tmp);
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  if (len > 0 && text) {
    if (f.write(reinterpret_cast<const uint8_t*>(text), len) != len) {
      f.close();
      SD.remove(tmp);
      return false;
    }
  }
  f.close();
  if (SD.exists(path)) SD.remove(path);
  if (!SD.rename(tmp, path)) {
    SD.remove(tmp);
    return false;
  }
  return true;
}
