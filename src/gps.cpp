#include "gps.h"

#include "board_hal.h"
#include "canvas.h"
#include "jpg_draw.h"

#include <BoardT5S3.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kTile = 256;
constexpr int kTileSlots = 16;
constexpr int kMaxSats = 16;
constexpr int kMaxTrack = 256;
constexpr int kTrackStepM = 25;
constexpr int kGrayCut = 200;

struct TrackPt {
  int32_t latE7 = 0;
  int32_t lonE7 = 0;
  int16_t altM = 0;
};

struct TileSlot {
  int z = -1;
  int x = -1;
  int y = -1;
  bool have = false;
  bool missing = false;
  uint8_t* bits = nullptr;
};

bool gRadioOn = false;
// NMEA keeps arriving while a map tile is decoded. 38400 baud for about four
// seconds fits; the hardware FIFO would not.
constexpr size_t kGpsRxBytes = 16384;
bool gSession = false;
uint8_t* gPane = nullptr;
int gPaneW = 0;
int gPaneH = 0;
int gStride = 0;
int gOriginX = 0;
int gOriginY = 0;
int gPaneZoom = -1;
bool gPaneReady = false;
GpsView gView;
GpsSat gSats[kMaxSats];
int gSatCount = 0;
int gFix = 0;
bool gZoomChosen = false;
bool gZoomRangeRead = false;
double gCenterLat = 60.21;
double gCenterLon = 25.03;
bool gFollow = true;
TrackPt gTrack[kMaxTrack];
int gTrackCount = 0;
char gLine[100];
size_t gLineLen = 0;
TileSlot gTiles[kTileSlots];
uint8_t* gGray = nullptr;

void holdChipSelects() {
  pinMode(T5S3_LORA_CS, OUTPUT);
  digitalWrite(T5S3_LORA_CS, HIGH);
  pinMode(T5S3_SD_CS, OUTPUT);
  digitalWrite(T5S3_SD_CS, HIGH);
}

int gSdDepth = 0;
bool gSawMaps = false;

// The SX1262 shares the SD SPI bus and the GPS power rail. NSS held high
// tri-states its MISO, so a tile read does not have to drop that rail. Cutting
// it cold-starts the receiver and throws away the search already in progress.
void sdBegin() {
  if (gSdDepth++ > 0) return;
  holdChipSelects();
}

void sdEnd() {
  if (gSdDepth == 0 || --gSdDepth > 0) return;
  holdChipSelects();
}

double nmeaDeg(const char* dm) {
  if (!dm || !dm[0]) return 0;
  const double v = atof(dm);
  const int deg = static_cast<int>(v / 100.0);
  const double minutes = v - static_cast<double>(deg) * 100.0;
  return static_cast<double>(deg) + minutes / 60.0;
}

bool nmeaChecksum(const char* line) {
  if (!line || line[0] != '$') return false;
  const char* star = strchr(line, '*');
  if (!star || star[1] == 0 || star[2] == 0) return false;
  uint8_t sum = 0;
  for (const char* p = line + 1; p < star; ++p) sum = static_cast<uint8_t>(sum ^ static_cast<uint8_t>(*p));
  unsigned int got = 0;
  if (sscanf(star + 1, "%2x", &got) != 1) return false;
  return sum == static_cast<uint8_t>(got);
}

int splitFields(char* line, char** fields, int maxFields) {
  int n = 0;
  if (maxFields <= 0) return 0;
  fields[n++] = line;
  for (char* p = line; *p && n < maxFields; ++p) {
    if (*p == ',') {
      *p = 0;
      fields[n++] = p + 1;
    } else if (*p == '*') {
      *p = 0;
      break;
    }
  }
  return n;
}

const char* sentenceKind(const char* type) {
  if (!type || type[0] != '$' || strlen(type) < 6) return "";
  return type + 3;
}

void publishView() {
  gView.fix = gFix;
  gView.satsView = gSatCount;
  gView.trackCount = gTrackCount;
}

double metersBetween(double lat1, double lon1, double lat2, double lon2) {
  constexpr double kM = 111320.0;
  const double mid = (lat1 + lat2) * 0.5 * 0.017453292519943295;
  const double x = (lon2 - lon1) * cos(mid) * kM;
  const double y = (lat2 - lat1) * kM;
  return sqrt(x * x + y * y);
}

void notePosition(double lat, double lon, bool hasAlt, int altM) {
  gView.hasPos = true;
  gView.lat = lat;
  gView.lon = lon;
  if (gFix == 0) gFix = hasAlt ? 3 : 2;
  if (hasAlt) {
    gView.hasAlt = true;
    gView.altM = altM;
  }
  const int32_t e7lat = static_cast<int32_t>(lat * 1e7);
  const int32_t e7lon = static_cast<int32_t>(lon * 1e7);
  if (gTrackCount > 0) {
    const TrackPt& prev = gTrack[gTrackCount - 1];
    const double plat = static_cast<double>(prev.latE7) / 1e7;
    const double plon = static_cast<double>(prev.lonE7) / 1e7;
    if (metersBetween(plat, plon, lat, lon) < kTrackStepM) return;
  }
  if (gTrackCount >= kMaxTrack) {
    memmove(gTrack, gTrack + 1, sizeof(TrackPt) * (kMaxTrack - 1));
    gTrackCount = kMaxTrack - 1;
  }
  gTrack[gTrackCount++] = TrackPt{e7lat, e7lon, static_cast<int16_t>(hasAlt ? altM : gView.altM)};
  gView.trackCount = gTrackCount;
}

void parseGga(char** f, int n) {
  if (n < 8) return;
  const int quality = atoi(f[6]);
  gView.satsUsed = atoi(f[7]);
  if (quality <= 0 || !f[2][0] || !f[4][0]) return;
  double lat = nmeaDeg(f[2]);
  double lon = nmeaDeg(f[4]);
  if (f[3][0] == 'S') lat = -lat;
  if (f[5][0] == 'W') lon = -lon;
  const bool hasAlt = n >= 10 && f[9][0];
  notePosition(lat, lon, hasAlt, hasAlt ? atoi(f[9]) : 0);
}

void parseRmc(char** f, int n) {
  if (n < 7 || f[2][0] != 'A' || !f[3][0] || !f[5][0]) return;
  double lat = nmeaDeg(f[3]);
  double lon = nmeaDeg(f[5]);
  if (f[4][0] == 'S') lat = -lat;
  if (f[6][0] == 'W') lon = -lon;
  notePosition(lat, lon, false, 0);
}

void parseGsa(char** f, int n) {
  if (n < 3) return;
  const int mode = atoi(f[2]);
  if (mode == 2 || mode == 3) gFix = mode;
  else gFix = 0;
}

void parseGsv(char** f, int n) {
  if (n < 4) return;
  const int total = atoi(f[1]);
  const int index = atoi(f[2]);
  if (index <= 1) gSatCount = 0;
  for (int base = 4; base + 3 < n && gSatCount < kMaxSats; base += 4) {
    if (!f[base][0]) continue;
    GpsSat sat;
    sat.el = f[base + 1][0] ? atoi(f[base + 1]) : -1;
    sat.az = f[base + 2][0] ? atoi(f[base + 2]) : 0;
    sat.snr = f[base + 3][0] ? atoi(f[base + 3]) : 0;
    gSats[gSatCount++] = sat;
  }
  (void)total;
  publishView();
}

bool handleLine(char* line, bool& fixChanged) {
  if (!nmeaChecksum(line)) return false;
  char* fields[24];
  const int n = splitFields(line, fields, 24);
  if (n <= 0) return false;
  const char* kind = sentenceKind(fields[0]);
  const int prev = gFix;
  if (strcmp(kind, "GGA") == 0) parseGga(fields, n);
  else if (strcmp(kind, "RMC") == 0) parseRmc(fields, n);
  else if (strcmp(kind, "GSA") == 0) parseGsa(fields, n);
  else if (strcmp(kind, "GSV") == 0) parseGsv(fields, n);
  else return false;
  if (gFix != prev) fixChanged = true;
  publishView();
  return true;
}

void mercator(double lat, double lon, int z, double& wx, double& wy) {
  if (lat > 85) lat = 85;
  if (lat < -85) lat = -85;
  const double n = static_cast<double>(kTile) * static_cast<double>(1 << z);
  const double latR = lat * 0.017453292519943295;
  wx = (lon + 180.0) / 360.0 * n;
  wy = (1.0 - log(tan(latR) + 1.0 / cos(latR)) / 3.14159265358979323846) / 2.0 * n;
}

void readZoomRange() {
  if (gZoomRangeRead) return;
  gZoomRangeRead = true;
  gView.zoomMin = 0;
  gView.zoomMax = 18;
  sdBegin();
  gSawMaps = SD.exists("/maps");
  File f = SD.open("/maps/zooms.txt", FILE_READ);
  if (!f) {
    sdEnd();
    return;
  }
  char buf[24] = {};
  const size_t n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  int a = 0, b = 0;
  if (sscanf(buf, "%d %d", &a, &b) == 2 && a >= 0 && b >= a && b <= 18) {
    gView.zoomMin = a;
    gView.zoomMax = b;
  }
  if (gView.zoom < gView.zoomMin) gView.zoom = gView.zoomMin;
  if (gView.zoom > gView.zoomMax) gView.zoom = gView.zoomMax;
  sdEnd();
}

bool tileFileExists(int z, int x, int y) {
  char path[40];
  snprintf(path, sizeof(path), "/maps/%d/%d/%d.jpg", z, x, y);
  sdBegin();
  const bool ok = SD.exists(path);
  if (ok) gSawMaps = true;
  sdEnd();
  return ok;
}

void inverseMercator(double wx, double wy, int z, double& lat, double& lon) {
  const double n = static_cast<double>(kTile) * static_cast<double>(1 << z);
  if (n <= 0) return;
  lon = wx / n * 360.0 - 180.0;
  const double t = 3.14159265358979323846 * (1.0 - 2.0 * wy / n);
  lat = atan(sinh(t)) * 57.29577951308232;
  if (lat > 85) lat = 85;
  if (lat < -85) lat = -85;
  if (lon > 180) lon = 180;
  if (lon < -180) lon = -180;
}

void syncCenter() {
  if (!gFollow || !gView.hasPos) return;
  const bool moved = fabs(gCenterLat - gView.lat) > 1e-7 || fabs(gCenterLon - gView.lon) > 1e-7;
  gCenterLat = gView.lat;
  gCenterLon = gView.lon;
  if (!moved) return;
  double wx = 0, wy = 0;
  mercator(gCenterLat, gCenterLon, gView.zoom, wx, wy);
  const int tx = static_cast<int>(floor(wx / kTile));
  const int ty = static_cast<int>(floor(wy / kTile));
  if (!tileFileExists(gView.zoom, tx, ty)) gZoomChosen = false;
}

void chooseZoom() {
  if (gZoomChosen) return;
  readZoomRange();
  int best = -1;
  int bestDist = 99;
  for (int z = gView.zoomMin; z <= gView.zoomMax; ++z) {
    double wx = 0, wy = 0;
    mercator(gCenterLat, gCenterLon, z, wx, wy);
    const int tx = static_cast<int>(floor(wx / kTile));
    const int ty = static_cast<int>(floor(wy / kTile));
    if (!tileFileExists(z, tx, ty)) continue;
    const int dist = abs(z - 15);
    if (dist < bestDist) {
      bestDist = dist;
      best = z;
    }
  }
  if (best >= 0) gView.zoom = best;
  gZoomChosen = true;
}

uint8_t* grayBuf() {
  if (!gGray) {
    gGray = static_cast<uint8_t*>(heap_caps_malloc(kTile * kTile, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!gGray) gGray = static_cast<uint8_t*>(malloc(kTile * kTile));
  }
  return gGray;
}

uint8_t* slotBits(TileSlot& slot) {
  if (!slot.bits) {
    slot.bits = static_cast<uint8_t*>(heap_caps_malloc(kTile * kTile / 8, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!slot.bits) slot.bits = static_cast<uint8_t*>(malloc(kTile * kTile / 8));
  }
  return slot.bits;
}

int divFloor(int a, int b) {
  int q = a / b;
  if (a % b != 0 && a < 0) --q;
  return q;
}

void loadTile(TileSlot& slot, int z, int x, int y) {
  slot.z = z;
  slot.x = x;
  slot.y = y;
  slot.have = false;
  slot.missing = true;
  uint8_t* bits = slotBits(slot);
  uint8_t* gray = grayBuf();
  if (!bits || !gray) return;
  char path[40];
  snprintf(path, sizeof(path), "/maps/%d/%d/%d.jpg", z, x, y);
  sdBegin();
  esp_task_wdt_reset();
  const bool ok = jpgReadGray(path, gray, kTile, kTile);
  sdEnd();
  if (!ok) return;
  memset(bits, 0, kTile * kTile / 8);
  for (int i = 0; i < kTile * kTile; ++i) {
    if (gray[i] < kGrayCut) bits[i >> 3] = static_cast<uint8_t>(bits[i >> 3] | (0x80 >> (i & 7)));
  }
  slot.have = true;
  slot.missing = false;
}

bool bitAt(const uint8_t* bits, int x, int y) {
  const int i = y * kTile + x;
  return (bits[i >> 3] & (0x80 >> (i & 7))) != 0;
}

void blitTile(const uint8_t* bits, int tileWx, int tileWy, int paneX, int paneY, int paneW, int paneH,
              int originX, int originY) {
  const int destX0 = paneX + (tileWx - originX);
  const int destY0 = paneY + (tileWy - originY);
  for (int ty = 0; ty < kTile; ++ty) {
    const int dy = destY0 + ty;
    if (dy < paneY || dy >= paneY + paneH) continue;
    if ((ty & 31) == 0) esp_task_wdt_reset();
    for (int tx = 0; tx < kTile; ++tx) {
      const int dx = destX0 + tx;
      if (dx < paneX || dx >= paneX + paneW) continue;
      canvasSetPixel(dx, dy, bitAt(bits, tx, ty));
    }
  }
}

int outCode(int x, int y, int L, int T, int R, int B) {
  int c = 0;
  if (x < L) c |= 1;
  if (x > R) c |= 2;
  if (y < T) c |= 4;
  if (y > B) c |= 8;
  return c;
}

bool clipLine(int& x0, int& y0, int& x1, int& y1, int L, int T, int R, int B) {
  for (int guard = 0; guard < 8; ++guard) {
    const int c0 = outCode(x0, y0, L, T, R, B);
    const int c1 = outCode(x1, y1, L, T, R, B);
    if ((c0 | c1) == 0) return true;
    if (c0 & c1) return false;
    const int c = c0 ? c0 : c1;
    int x = x0, y = y0;
    if ((c & 8) && y1 != y0) {
      x = x0 + (x1 - x0) * (B - y0) / (y1 - y0);
      y = B;
    } else if ((c & 4) && y1 != y0) {
      x = x0 + (x1 - x0) * (T - y0) / (y1 - y0);
      y = T;
    } else if ((c & 2) && x1 != x0) {
      y = y0 + (y1 - y0) * (R - x0) / (x1 - x0);
      x = R;
    } else if (x1 != x0) {
      y = y0 + (y1 - y0) * (L - x0) / (x1 - x0);
      x = L;
    } else {
      return false;
    }
    if (c == c0) {
      x0 = x;
      y0 = y;
    } else {
      x1 = x;
      y1 = y;
    }
  }
  return false;
}

void drawScale(int x, int y, int paneW) {
  const double mPerPx =
      156543.03392 * cos(gCenterLat * 0.017453292519943295) / static_cast<double>(1 << gView.zoom);
  if (mPerPx <= 0) return;
  const int choices[] = {100, 200, 500, 1000, 2000, 5000, 10000};
  int meters = choices[0];
  int barPx = static_cast<int>(meters / mPerPx);
  for (int c : choices) {
    const int px = static_cast<int>(c / mPerPx);
    if (px >= 36 && px <= paneW / 2) {
      meters = c;
      barPx = px;
    }
  }
  if (barPx < 8) return;
  char label[16];
  if (meters >= 1000 && meters % 1000 == 0) snprintf(label, sizeof(label), "%d km", meters / 1000);
  else snprintf(label, sizeof(label), "%d m", meters);
  const int textW = canvasTextWidth(label, 1);
  const int span = barPx > textW ? barPx : textW;
  canvasFillRect(x, y - canvasTextHeight(1) - 2, span + 4, canvasTextHeight(1) + 8, false);
  canvasDrawLine(x, y, x + barPx, y, true);
  canvasDrawLine(x, y - 4, x, y + 4, true);
  canvasDrawLine(x + barPx, y - 4, x + barPx, y + 4, true);
  canvasDrawString(x, y - canvasTextHeight(1) - 2, label, true, 1);
}

void writeGpx() {
  if (gTrackCount <= 0) return;
  holdChipSelects();
  if (!SD.exists("/gps")) SD.mkdir("/gps");
  File f = SD.open("/gps/track.gpx", FILE_WRITE);
  if (!f) return;
  f.print("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\">\n<trk><trkseg>\n");
  for (int i = 0; i < gTrackCount; ++i) {
    esp_task_wdt_reset();
    const double lat = static_cast<double>(gTrack[i].latE7) / 1e7;
    const double lon = static_cast<double>(gTrack[i].lonE7) / 1e7;
    char line[80];
    snprintf(line, sizeof(line), "<trkpt lat=\"%.6f\" lon=\"%.6f\"><ele>%d</ele></trkpt>\n", lat, lon,
             static_cast<int>(gTrack[i].altM));
    f.print(line);
  }
  f.print("</trkseg></trk>\n</gpx>\n");
  f.close();
}

uint8_t* paneCol(int x) { return gPane + static_cast<size_t>(x) * gStride; }

void paneSet(int lx, int ly, bool black) {
  if (!gPane || lx < 0 || ly < 0 || lx >= gPaneW || ly >= gPaneH) return;
  uint8_t& b = paneCol(lx)[ly >> 3];
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (ly & 7));
  if (black) b = static_cast<uint8_t>(b | mask);
  else b = static_cast<uint8_t>(b & static_cast<uint8_t>(~mask));
}

uint8_t takeBits(const uint8_t* src, int stride, int bit, int n) {
  const int i = bit >> 3;
  const int off = bit & 7;
  const uint16_t hi = (i >= 0 && i < stride) ? src[i] : 0;
  const uint16_t lo = (i + 1 >= 0 && i + 1 < stride) ? src[i + 1] : 0;
  const uint16_t window = static_cast<uint16_t>((hi << 8) | lo);
  const int shift = 16 - off - n;
  if (shift < 0 || n <= 0 || n > 8) return 0;
  return static_cast<uint8_t>((window >> shift) & ((1u << n) - 1));
}

void depositBits(uint8_t* dst, int bit, uint8_t chunk, int n) {
  const int dOff = bit & 7;
  const int shift = 8 - dOff - n;
  const uint8_t mask = static_cast<uint8_t>(((1u << n) - 1) << shift);
  const uint8_t placed = static_cast<uint8_t>(chunk << shift);
  uint8_t& b = dst[bit >> 3];
  b = static_cast<uint8_t>((b & static_cast<uint8_t>(~mask)) | placed);
}

void copyBitSpan(uint8_t* dst, int dstBit, const uint8_t* src, int srcStride, int srcBit, int n) {
  while (n > 0) {
    const int sOff = srcBit & 7;
    const int dOff = dstBit & 7;
    int chunk = 8 - sOff;
    if (chunk > 8 - dOff) chunk = 8 - dOff;
    if (chunk > n) chunk = n;
    if (chunk <= 0) return;
    depositBits(dst, dstBit, takeBits(src, srcStride, srcBit, chunk), chunk);
    dstBit += chunk;
    srcBit += chunk;
    n -= chunk;
  }
}

void shiftColumn(uint8_t* col, int height, int dy) {
  if (dy == 0 || height <= 0) return;
  const int stride = (height + 7) >> 3;
  uint8_t tmp[128];
  if (stride <= 0 || stride > static_cast<int>(sizeof(tmp))) return;
  memcpy(tmp, col, stride);
  memset(col, 0, stride);
  const int ady = dy < 0 ? -dy : dy;
  if (ady >= height) return;
  const int n = height - ady;
  const int src = dy > 0 ? 0 : ady;
  const int dst = dy > 0 ? dy : 0;
  copyBitSpan(col, dst, tmp, stride, src, n);
}

bool ensurePane(int w, int h) {
  if (w <= 0 || h <= 0 || w > kScreenW || h > kScreenH) return false;
  const int stride = (h + 7) >> 3;
  if (!gPane) {
    const size_t bytes = static_cast<size_t>(kScreenW) * ((kScreenH + 7) / 8);
    gPane = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!gPane) gPane = static_cast<uint8_t*>(malloc(bytes));
    if (!gPane) return false;
  }
  if (gPaneW != w || gPaneH != h || gStride != stride) {
    gPaneW = w;
    gPaneH = h;
    gStride = stride;
    gPaneReady = false;
  }
  return true;
}

void paneClear(int x, int y, int w, int h) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (!gPane || w <= 0 || h <= 0 || x >= gPaneW || y >= gPaneH) return;
  if (x + w > gPaneW) w = gPaneW - x;
  if (y + h > gPaneH) h = gPaneH - y;
  if (y == 0 && h == gPaneH) {
    for (int lx = x; lx < x + w; ++lx) memset(paneCol(lx), 0, gStride);
    return;
  }
  for (int lx = x; lx < x + w; ++lx) {
    for (int ly = y; ly < y + h; ++ly) paneSet(lx, ly, false);
  }
}

void blitTilePane(const uint8_t* bits, int tileWx, int tileWy, int clipX, int clipY, int clipW, int clipH) {
  const int destX0 = tileWx - gOriginX;
  const int destY0 = tileWy - gOriginY;
  int x0 = clipX > destX0 ? clipX : destX0;
  int y0 = clipY > destY0 ? clipY : destY0;
  int x1 = clipX + clipW;
  int y1 = clipY + clipH;
  const int tileX1 = destX0 + kTile;
  const int tileY1 = destY0 + kTile;
  if (x1 > tileX1) x1 = tileX1;
  if (y1 > tileY1) y1 = tileY1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > gPaneW) x1 = gPaneW;
  if (y1 > gPaneH) y1 = gPaneH;
  for (int ly = y0; ly < y1; ++ly) {
    if ((ly & 31) == 0) esp_task_wdt_reset();
    const int ty = ly - destY0;
    for (int lx = x0; lx < x1; ++lx) {
      if (bitAt(bits, lx - destX0, ty)) paneSet(lx, ly, true);
    }
  }
}

void paintCachedTiles(int clipX, int clipY, int clipW, int clipH) {
  if (clipW <= 0 || clipH <= 0) return;
  paneClear(clipX, clipY, clipW, clipH);
  for (int i = 0; i < kTileSlots; ++i) {
    const TileSlot& tile = gTiles[i];
    if (!tile.have || !tile.bits || tile.z != gView.zoom) continue;
    blitTilePane(tile.bits, tile.x * kTile, tile.y * kTile, clipX, clipY, clipW, clipH);
  }
}

void paneLine(int x0, int y0, int x1, int y1) {
  const int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (int guard = 0; guard < 4096; ++guard) {
    paneSet(x0, y0, true);
    if (x0 == x1 && y0 == y1) return;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void projectPane(double lat, double lon, int& lx, int& ly) {
  double px = 0, py = 0;
  mercator(lat, lon, gView.zoom, px, py);
  lx = static_cast<int>(floor(px)) - gOriginX;
  ly = static_cast<int>(floor(py)) - gOriginY;
}

void drawPaneOverlays() {
  const int right = gPaneW - 1;
  const int bottom = gPaneH - 1;
  for (int i = 1; i < gTrackCount; ++i) {
    int xA = 0, yA = 0, xB = 0, yB = 0;
    projectPane(static_cast<double>(gTrack[i - 1].latE7) / 1e7, static_cast<double>(gTrack[i - 1].lonE7) / 1e7,
                xA, yA);
    projectPane(static_cast<double>(gTrack[i].latE7) / 1e7, static_cast<double>(gTrack[i].lonE7) / 1e7, xB, yB);
    if (!clipLine(xA, yA, xB, yB, 0, 0, right, bottom)) continue;
    paneLine(xA, yA, xB, yB);
  }
  if (!gView.hasPos) return;
  int sx = 0, sy = 0;
  projectPane(gView.lat, gView.lon, sx, sy);
  int x0 = sx - 10, y0 = sy, x1 = sx + 10, y1 = sy;
  if (clipLine(x0, y0, x1, y1, 0, 0, right, bottom)) paneLine(x0, y0, x1, y1);
  x0 = sx;
  y0 = sy - 10;
  x1 = sx;
  y1 = sy + 10;
  if (clipLine(x0, y0, x1, y1, 0, 0, right, bottom)) paneLine(x0, y0, x1, y1);
}

void scrollPanePixels(int dx, int dy) {
  const int adx = dx < 0 ? -dx : dx;
  const int ady = dy < 0 ? -dy : dy;
  if (adx >= gPaneW || ady >= gPaneH) {
    memset(gPane, 0, static_cast<size_t>(gPaneW) * gStride);
    gOriginX -= dx;
    gOriginY -= dy;
    paintCachedTiles(0, 0, gPaneW, gPaneH);
    return;
  }
  if (dx > 0) {
    for (int x = gPaneW - 1; x >= dx; --x) memcpy(paneCol(x), paneCol(x - dx), gStride);
  } else if (dx < 0) {
    for (int x = 0; x < gPaneW + dx; ++x) memcpy(paneCol(x), paneCol(x - dx), gStride);
  }
  const int keepX0 = dx > 0 ? dx : 0;
  const int keepX1 = dx < 0 ? gPaneW + dx : gPaneW;
  if (dy != 0) {
    for (int x = keepX0; x < keepX1; ++x) {
      if (((x - keepX0) & 31) == 0) esp_task_wdt_reset();
      shiftColumn(paneCol(x), gPaneH, dy);
    }
  }
  gOriginX -= dx;
  gOriginY -= dy;
  if (dx > 0) paintCachedTiles(0, 0, dx, gPaneH);
  else if (dx < 0) paintCachedTiles(gPaneW + dx, 0, -dx, gPaneH);
  if (dy > 0) paintCachedTiles(keepX0, 0, keepX1 - keepX0, dy);
  else if (dy < 0) paintCachedTiles(keepX0, gPaneH + dy, keepX1 - keepX0, -dy);
}

bool zoomTowardAt(int dir, int offsetX, int offsetY, bool manual) {
  readZoomRange();
  const int next = gView.zoom + dir;
  if (next < gView.zoomMin || next > gView.zoomMax) return false;

  double centerX = 0, centerY = 0;
  mercator(gCenterLat, gCenterLon, gView.zoom, centerX, centerY);
  const double factor = dir > 0 ? 2.0 : 0.5;
  const double nextCenterX = (centerX + offsetX) * factor - offsetX;
  const double nextCenterY = (centerY + offsetY) * factor - offsetY;
  double nextLat = 0, nextLon = 0;
  inverseMercator(nextCenterX, nextCenterY, next, nextLat, nextLon);
  const int tx = static_cast<int>(floor(nextCenterX / kTile));
  const int ty = static_cast<int>(floor(nextCenterY / kTile));
  if (!tileFileExists(next, tx, ty)) return false;

  gView.zoom = next;
  gCenterLat = nextLat;
  gCenterLon = nextLon;
  if (manual) gFollow = false;
  gPaneReady = false;
  return true;
}

bool zoomToward(int dir) { return zoomTowardAt(dir, 0, 0, false); }

}  // namespace

void gpsStart(bool fresh) {
  gSession = true;
  gPaneReady = false;
  if (fresh) {
    gTrackCount = 0;
    gView.trackCount = 0;
    gView.hasPos = false;
    gView.hasAlt = false;
    gFix = 0;
    gView.fix = 0;
    gSatCount = 0;
    gZoomChosen = false;
    gView.zoom = 15;
    gCenterLat = 60.21;
    gCenterLon = 25.03;
    gFollow = true;
  }
  if (gRadioOn) return;
  holdChipSelects();
  BoardT5S3::writePca9535Pin(PCA9535_IO00_LORA_GPS_EN, true);
  BoardT5S3::setPca9535PinMode(PCA9535_IO00_LORA_GPS_EN, OUTPUT);
  delay(80);
  Serial1.setRxBufferSize(kGpsRxBytes);
  Serial1.begin(boardGpsBaud(), SERIAL_8N1, T5S3_GPS_RXD, T5S3_GPS_TXD);
  gLineLen = 0;
  gRadioOn = true;
}

void gpsPause() {
  if (!gRadioOn) return;
  Serial1.end();
  BoardT5S3::disableGpsLora();
  gRadioOn = false;
}

void gpsLeave() {
  gpsPause();
  writeGpx();
  gSession = false;
  gPaneReady = false;
}

bool gpsActive() { return gRadioOn; }

bool gpsSession() { return gSession; }

GpsPoll gpsPoll() {
  GpsPoll out;
  if (!gRadioOn) return out;
  int lines = 0;
  while (Serial1.available() > 0 && lines < 12) {
    const char c = static_cast<char>(Serial1.read());
    if (c == '\r') continue;
    if (c != '\n') {
      if (gLineLen + 1 < sizeof(gLine)) gLine[gLineLen++] = c;
      else gLineLen = 0;
      continue;
    }
    if (gLineLen == 0) continue;
    gLine[gLineLen] = 0;
    gLineLen = 0;
    ++lines;
    if (handleLine(gLine, out.fixChanged)) out.sentence = true;
  }
  return out;
}

const GpsView& gpsView() { return gView; }

int gpsSatCount() { return gSatCount > 12 ? 12 : gSatCount; }

GpsSat gpsSat(int index) {
  if (index < 0 || index >= gSatCount) return {};
  return gSats[index];
}

bool gpsHasMaps() { return gSawMaps; }

bool gpsZoomIn() { return zoomToward(1); }
bool gpsZoomOut() { return zoomToward(-1); }

bool gpsPinchZoom(float scale, int offsetX, int offsetY) {
  if (scale == 1.0f) return false;
  const int direction = scale > 1.0f ? 1 : -1;
  const int steps = (scale >= 1.8f || scale <= 0.55f) ? 2 : 1;
  bool changed = false;
  for (int i = 0; i < steps; ++i) {
    if (!zoomTowardAt(direction, offsetX, offsetY, true)) break;
    changed = true;
  }
  return changed;
}

bool gpsMapFollowMoved() {
  if (!gFollow || !gView.hasPos) return false;
  return fabs(gCenterLat - gView.lat) > 1e-7 || fabs(gCenterLon - gView.lon) > 1e-7;
}

bool gpsPan(int dx, int dy) {
  if (dx == 0 && dy == 0) return false;
  gFollow = false;
  double wx = 0, wy = 0;
  mercator(gCenterLat, gCenterLon, gView.zoom, wx, wy);
  wx -= dx;
  wy -= dy;
  inverseMercator(wx, wy, gView.zoom, gCenterLat, gCenterLon);
  return true;
}

bool gpsRecenter() {
  if (!gView.hasPos) return false;
  gFollow = true;
  gCenterLat = gView.lat;
  gCenterLon = gView.lon;
  gZoomChosen = false;
  return true;
}

bool gpsDrawMap(int x, int y, int w, int h) {
  canvasFillRect(x, y, w, h, false);
  if (w <= 0 || h <= 0) return false;
  sdBegin();
  syncCenter();
  chooseZoom();
  double wx = 0, wy = 0;
  mercator(gCenterLat, gCenterLon, gView.zoom, wx, wy);
  const int originX = static_cast<int>(floor(wx)) - w / 2;
  const int originY = static_cast<int>(floor(wy)) - h / 2;
  const int x0 = divFloor(originX, kTile);
  const int y0 = divFloor(originY, kTile);
  const int x1 = divFloor(originX + w - 1, kTile);
  const int y1 = divFloor(originY + h - 1, kTile);
  bool any = false;
  int used = 0;
  for (int ty = y0; ty <= y1; ++ty) {
    for (int tx = x0; tx <= x1; ++tx) {
      if (used >= kTileSlots) break;
      TileSlot& tile = gTiles[used++];
      if (tile.z != gView.zoom || tile.x != tx || tile.y != ty || (!tile.have && !tile.missing)) {
        loadTile(tile, gView.zoom, tx, ty);
      }
      if (tile.have && tile.bits) any = true;
    }
  }
  for (int i = used; i < kTileSlots; ++i) {
    gTiles[i].have = false;
    gTiles[i].missing = false;
    gTiles[i].z = -1;
  }
  sdEnd();

  gOriginX = originX;
  gOriginY = originY;
  gPaneZoom = gView.zoom;
  if (ensurePane(w, h)) {
    memset(gPane, 0, static_cast<size_t>(gPaneW) * gStride);
    for (int i = 0; i < used; ++i) {
      const TileSlot& tile = gTiles[i];
      if (!tile.have || !tile.bits) continue;
      blitTilePane(tile.bits, tile.x * kTile, tile.y * kTile, 0, 0, w, h);
    }
    drawPaneOverlays();
    canvasBlitColumnBits(x, y, w, h, gPane, gStride);
    gPaneReady = true;
  } else {
    gPaneReady = false;
    for (int i = 0; i < used; ++i) {
      const TileSlot& tile = gTiles[i];
      if (!tile.have || !tile.bits) continue;
      blitTile(tile.bits, tile.x * kTile, tile.y * kTile, x, y, w, h, originX, originY);
    }
  }
  drawScale(x + 10, y + h - 28, w);
  return any;
}

bool gpsScrollMap(int dx, int dy, int x, int y, int w, int h) {
  if (!gPaneReady || !gPane || gPaneW != w || gPaneH != h || gPaneZoom != gView.zoom) return false;
  if (dx == 0 && dy == 0) return true;
  scrollPanePixels(dx, dy);
  drawPaneOverlays();
  canvasBlitColumnBits(x, y, w, h, gPane, gStride);
  drawScale(x + 10, y + h - 28, w);
  return true;
}
