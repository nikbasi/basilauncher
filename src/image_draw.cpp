#include "image_draw.h"

#include "bmp_draw.h"
#include "jpg_draw.h"

#include <cstring>

namespace {

bool endsWithCi(const char* name, const char* ext) {
  if (!name || !ext) return false;
  const size_t n = strlen(name);
  const size_t e = strlen(ext);
  if (n < e) return false;
  for (size_t i = 0; i < e; ++i) {
    char a = name[n - e + i];
    char b = ext[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

}  // namespace

bool imageIsSupportedName(const char* name) {
  return endsWithCi(name, ".bmp") || endsWithCi(name, ".jpg") || endsWithCi(name, ".jpeg");
}

bool imageDrawFile(const char* path, ImageAbortCheck abortCheck) {
  if (!path || !path[0]) return false;
  if (endsWithCi(path, ".jpg") || endsWithCi(path, ".jpeg")) {
    return jpgDrawFile(path, abortCheck);
  }
  if (endsWithCi(path, ".bmp")) {
    return bmpDrawFile(path, abortCheck);
  }
  // Unknown extension: try BMP magic then JPEG.
  if (bmpDrawFile(path, abortCheck)) return true;
  return jpgDrawFile(path, abortCheck);
}
