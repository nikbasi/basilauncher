#include "jpg_draw.h"

#include "canvas.h"

#include <JPEGDEC.h>
#include <SD.h>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

namespace {

// JPEGDEC needs ~20KB; keep it in BSS, not on the loop stack.
JPEGDEC gJpeg;
File gJpegFile;

struct JpgOut {
  uint8_t* img = nullptr;
  int width = 0;
  int height = 0;
  JpgAbortCheck abortCheck = nullptr;
  bool aborted = false;
  uint32_t blocks = 0;
};

JpgOut* gOut = nullptr;

void feedWatchdog() {
  // A full photo decode otherwise trips the task watchdog and reboots.
  esp_task_wdt_reset();
}

void* allocBuf(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(n);
  return p;
}

int jpegDrawBlock(JPEGDRAW* pDraw) {
  if (!gOut || !gOut->img || gOut->aborted) return 0;
  if ((gOut->blocks++ & 7) == 0) {
    feedWatchdog();
    if (gOut->abortCheck && gOut->abortCheck()) {
      gOut->aborted = true;
      return 0;
    }
  }
  if (!pDraw->pPixels || pDraw->iWidth <= 0 || pDraw->iHeight <= 0) return 1;

  const auto* src = reinterpret_cast<const uint8_t*>(pDraw->pPixels);
  const int x0 = pDraw->x;
  const int y0 = pDraw->y;
  const int bw = pDraw->iWidth;
  const int bh = pDraw->iHeight;
  for (int row = 0; row < bh; ++row) {
    const int dy = y0 + row;
    if (dy < 0 || dy >= gOut->height) {
      src += bw;
      continue;
    }
    uint8_t* dst = gOut->img + static_cast<size_t>(dy) * static_cast<size_t>(gOut->width);
    const int xStart = x0 < 0 ? -x0 : 0;
    const int xEnd = (x0 + bw > gOut->width) ? (gOut->width - x0) : bw;
    if (xEnd > xStart) {
      memcpy(dst + (x0 + xStart), src + xStart, static_cast<size_t>(xEnd - xStart));
    }
    src += bw;
  }
  return 1;
}

int sampleGray(const uint8_t* img, int srcW, int srcH, int sx, int sy) {
  if (sx < 0) sx = 0;
  if (sy < 0) sy = 0;
  if (sx >= srcW) sx = srcW - 1;
  if (sy >= srcH) sy = srcH - 1;
  return img[static_cast<size_t>(sy) * srcW + sx];
}

// Pull edges back out after the low-pass decode. Amount is half the Laplacian so
// fur and faces both gain contour without a heavy halo.
void sharpenGray(uint8_t* img, int w, int h) {
  if (!img || w < 3 || h < 3) return;
  auto* prev = static_cast<uint8_t*>(allocBuf(static_cast<size_t>(w)));
  auto* cur = static_cast<uint8_t*>(allocBuf(static_cast<size_t>(w)));
  if (!prev || !cur) {
    free(prev);
    free(cur);
    return;
  }
  memcpy(prev, img, static_cast<size_t>(w));
  for (int y = 1; y < h - 1; ++y) {
    if ((y & 31) == 0) feedWatchdog();
    memcpy(cur, img + static_cast<size_t>(y) * w, static_cast<size_t>(w));
    uint8_t* dst = img + static_cast<size_t>(y) * w;
    const uint8_t* down = img + static_cast<size_t>(y + 1) * w;
    for (int x = 1; x < w - 1; ++x) {
      const int avg = (cur[x - 1] + cur[x + 1] + prev[x] + down[x]) / 4;
      int v = cur[x] + (cur[x] - avg) / 2;
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      dst[x] = static_cast<uint8_t>(v);
    }
    memcpy(prev, cur, static_cast<size_t>(w));
  }
  free(prev);
  free(cur);
}

void ditherScreen(const uint8_t* img) {
  canvasClear();
  auto* errA = static_cast<int*>(calloc(static_cast<size_t>(kScreenW + 2), sizeof(int)));
  auto* errB = static_cast<int*>(calloc(static_cast<size_t>(kScreenW + 2), sizeof(int)));
  int* cur = errA;
  int* nxt = errB;
  for (int dy = 0; dy < kScreenH; ++dy) {
    if ((dy & 3) == 0) feedWatchdog();
    const uint8_t* row = img + static_cast<size_t>(dy) * kScreenW;
    if (nxt) memset(nxt, 0, static_cast<size_t>(kScreenW + 2) * sizeof(int));
    for (int dx = 0; dx < kScreenW; ++dx) {
      int lum = row[dx];
      if (cur) lum += cur[dx + 1];
      if (lum < 0) lum = 0;
      if (lum > 255) lum = 255;
      const bool black = lum < 128;
      canvasSetPixel(dx, dy, black);
      if (cur && nxt) {
        const int err = lum - (black ? 0 : 255);
        cur[dx + 2] += (err * 7) / 16;
        nxt[dx] += (err * 3) / 16;
        nxt[dx + 1] += (err * 5) / 16;
        nxt[dx + 2] += (err * 1) / 16;
      }
    }
    if (cur && nxt) {
      int* tmp = cur;
      cur = nxt;
      nxt = tmp;
    }
  }
  free(errA);
  free(errB);
}

// Scale the grayscale image to cover the panel (crop the longer axis), then dither.
void ditherGrayToCanvas(uint8_t* img, int srcW, int srcH) {
  if (!img || srcW < 1 || srcH < 1) return;
  sharpenGray(img, srcW, srcH);
  if (srcW == kScreenW && srcH == kScreenH) {
    ditherScreen(img);
    return;
  }
  canvasClear();

  int cropX = 0, cropY = 0, cropW = srcW, cropH = srcH;
  if (static_cast<int64_t>(srcW) * kScreenH > static_cast<int64_t>(srcH) * kScreenW) {
    cropW = static_cast<int>((static_cast<int64_t>(srcH) * kScreenW + kScreenH / 2) / kScreenH);
    if (cropW < 1) cropW = 1;
    if (cropW > srcW) cropW = srcW;
    cropX = (srcW - cropW) / 2;
  } else {
    cropH = static_cast<int>((static_cast<int64_t>(srcW) * kScreenH + kScreenW / 2) / kScreenW);
    if (cropH < 1) cropH = 1;
    if (cropH > srcH) cropH = srcH;
    cropY = (srcH - cropH) / 2;
  }

  auto* errA = static_cast<int*>(calloc(static_cast<size_t>(kScreenW + 2), sizeof(int)));
  auto* errB = static_cast<int*>(calloc(static_cast<size_t>(kScreenW + 2), sizeof(int)));
  int* cur = errA;
  int* nxt = errB;
  for (int dy = 0; dy < kScreenH; ++dy) {
    if ((dy & 3) == 0) feedWatchdog();
    const int sy0 = cropY + static_cast<int>((static_cast<int64_t>(dy) * cropH) / kScreenH);
    const int sy1 = sy0 + 1 < srcH ? sy0 + 1 : sy0;
    const int fy = static_cast<int>(((static_cast<int64_t>(dy) * cropH) % kScreenH) * 256 / kScreenH);
    if (nxt) memset(nxt, 0, static_cast<size_t>(kScreenW + 2) * sizeof(int));
    for (int dx = 0; dx < kScreenW; ++dx) {
      const int sx0 = cropX + static_cast<int>((static_cast<int64_t>(dx) * cropW) / kScreenW);
      const int sx1 = sx0 + 1 < srcW ? sx0 + 1 : sx0;
      const int fx = static_cast<int>(((static_cast<int64_t>(dx) * cropW) % kScreenW) * 256 / kScreenW);
      const int l00 = sampleGray(img, srcW, srcH, sx0, sy0);
      const int l10 = sampleGray(img, srcW, srcH, sx1, sy0);
      const int l01 = sampleGray(img, srcW, srcH, sx0, sy1);
      const int l11 = sampleGray(img, srcW, srcH, sx1, sy1);
      const int top = l00 + ((l10 - l00) * fx) / 256;
      const int bot = l01 + ((l11 - l01) * fx) / 256;
      int lum = top + ((bot - top) * fy) / 256;
      if (cur) lum += cur[dx + 1];
      if (lum < 0) lum = 0;
      if (lum > 255) lum = 255;
      const bool black = lum < 128;
      canvasSetPixel(dx, dy, black);
      if (cur && nxt) {
        const int out = black ? 0 : 255;
        const int err = lum - out;
        cur[dx + 2] += (err * 7) / 16;
        nxt[dx] += (err * 3) / 16;
        nxt[dx + 1] += (err * 5) / 16;
        nxt[dx + 2] += (err * 1) / 16;
      }
    }
    if (cur && nxt) {
      int* tmp = cur;
      cur = nxt;
      nxt = tmp;
    }
  }
  free(errA);
  free(errB);
}

// Power-of-two scale so we decode near panel size, not the full photo.
int pickScaleOptions(int srcW, int srcH, bool progressive, int& outW, int& outH) {
  int div = 1;
  int opts = 0;
  if (progressive) {
    // JPEGDEC returns only the DC scan, at 1/8, for progressive JPEGs.
    div = 8;
    opts = JPEG_SCALE_EIGHTH;
  } else {
    while (div < 8 && ((srcW / (div * 2)) >= kScreenW || (srcH / (div * 2)) >= kScreenH)) {
      div *= 2;
    }
    if (div == 2) opts = JPEG_SCALE_HALF;
    else if (div == 4) opts = JPEG_SCALE_QUARTER;
    else if (div == 8) opts = JPEG_SCALE_EIGHTH;
  }
  outW = srcW / div;
  outH = srcH / div;
  if (outW < 1) outW = 1;
  if (outH < 1) outH = 1;
  return opts;
}

bool decodeIntoCanvas(int opts, int outW, int outH, JpgAbortCheck abortCheck) {
  const int bufW = outW + 16;
  const int bufH = outH + 16;
  const size_t bytes = static_cast<size_t>(bufW) * static_cast<size_t>(bufH);
  auto* img = static_cast<uint8_t*>(allocBuf(bytes));
  if (!img) return false;
  memset(img, 0xFF, bytes);

  JpgOut out;
  out.img = img;
  out.width = bufW;
  out.height = bufH;
  out.abortCheck = abortCheck;
  gOut = &out;

  gJpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
  gJpeg.setMaxOutputSize(8);
  feedWatchdog();
  const int ok = gJpeg.decode(0, 0, opts);
  feedWatchdog();
  gOut = nullptr;
  gJpeg.close();
  if (gJpegFile) gJpegFile.close();
  feedWatchdog();

  if (out.aborted || !ok) {
    free(img);
    Serial.printf("JPEG: decode failed err=%d\n", gJpeg.getLastError());
    return false;
  }

  auto* packed = static_cast<uint8_t*>(allocBuf(static_cast<size_t>(outW) * static_cast<size_t>(outH)));
  if (!packed) {
    ditherGrayToCanvas(img, bufW, outH);
    free(img);
    return true;
  }
  for (int y = 0; y < outH; ++y) {
    if ((y & 31) == 0) feedWatchdog();
    memcpy(packed + static_cast<size_t>(y) * outW, img + static_cast<size_t>(y) * bufW,
           static_cast<size_t>(outW));
  }
  free(img);
  ditherGrayToCanvas(packed, outW, outH);
  free(packed);
  return true;
}

// Phone JPEGs are progressive. A full decode needs megabytes; the first scan is
// only DC values, and each DC is the 8x8 block average — a correct 1/8 image.
struct ProgHuff {
  uint8_t counts[17] = {};
  uint16_t firstCode[17] = {};
  uint16_t firstIndex[17] = {};
  uint8_t symbols[256] = {};
  bool ready = false;
};

struct ProgJpeg {
  File* file = nullptr;
  uint8_t buf[512] = {};
  uint32_t bufLen = 0;
  uint32_t bufPos = 0;
  uint32_t bitBuf = 0;
  int bitCount = 0;
  int pendingMarker = 0;

  int rawByte() {
    if (bufPos >= bufLen) {
      const int n = file->read(buf, sizeof(buf));
      if (n <= 0) return -1;
      bufLen = static_cast<uint32_t>(n);
      bufPos = 0;
    }
    return buf[bufPos++];
  }

  int nextBit() {
    if (bitCount == 0) {
      int b = rawByte();
      if (b < 0) return -1;
      if (b == 0xFF) {
        int m = rawByte();
        while (m == 0xFF) m = rawByte();
        if (m < 0) return -1;
        if (m != 0x00) {
          pendingMarker = m;
          return -1;
        }
        b = 0xFF;
      }
      bitBuf = static_cast<uint32_t>(b);
      bitCount = 8;
    }
    --bitCount;
    return static_cast<int>((bitBuf >> bitCount) & 1u);
  }

  int decodeHuff(const ProgHuff& t) {
    uint32_t code = 0;
    for (int len = 1; len <= 16; ++len) {
      const int b = nextBit();
      if (b < 0) return -1;
      code = (code << 1) | static_cast<uint32_t>(b);
      if (t.counts[len] != 0 && code < static_cast<uint32_t>(t.firstCode[len]) + t.counts[len]) {
        return t.symbols[t.firstIndex[len] + (code - t.firstCode[len])];
      }
    }
    return -1;
  }

  int32_t receiveExtend(int s, bool* ok) {
    int32_t v = 0;
    for (int i = 0; i < s; ++i) {
      const int b = nextBit();
      if (b < 0) {
        *ok = false;
        return 0;
      }
      v = (v << 1) | b;
    }
    if (s > 0 && v < (1 << (s - 1))) v -= (1 << s) - 1;
    return v;
  }
};

void buildProgHuff(ProgHuff& t) {
  uint16_t code = 0;
  uint16_t index = 0;
  for (int len = 1; len <= 16; ++len) {
    t.firstCode[len] = code;
    t.firstIndex[len] = index;
    code = static_cast<uint16_t>(code + t.counts[len]);
    index = static_cast<uint16_t>(index + t.counts[len]);
    code = static_cast<uint16_t>(code << 1);
  }
  t.ready = true;
}

// 4x4 keeps sixteen low-frequency terms (about twice the linear detail of 2x2).
// AC values are stored in int8 so a 12 MP photo still fits in PSRAM; DC stays int16.
int gStoreStride = 4;
uint32_t gLumaBlocks = 0;

int coefSlot(int k) {
  if (gStoreStride != 16) {
    if (k == 0) return 0;
    if (k == 1) return 1;
    if (k == 2) return 2;
    if (k == 4) return 3;
    return -1;
  }
  // Zigzag index → slot in the 4x4 natural block (v * 4 + u). -1 = not stored.
  static constexpr int8_t kMap[25] = {0,  1,  4,  8,  5,  2,  3,  6,  9,  12, -1, 13, 10,
                                      7,  -1, -1, 14, 11, -1, -1, -1, -1, -1, -1, 15};
  if (k < 0 || k >= 25) return -1;
  return kMap[k];
}

bool readBytes(ProgJpeg& pj, uint8_t* dst, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    const int b = pj.rawByte();
    if (b < 0) return false;
    if (dst) dst[i] = static_cast<uint8_t>(b);
  }
  return true;
}

int nextMarker(ProgJpeg& pj) {
  int b = pj.rawByte();
  while (b == 0xFF) b = pj.rawByte();
  return b;
}

int unsignedBits(ProgJpeg& pj, int n) {
  int v = 0;
  for (int i = 0; i < n; ++i) {
    const int b = pj.nextBit();
    if (b < 0) return -1;
    v = (v << 1) | b;
  }
  return v;
}

bool isNz(const uint8_t* nz, uint32_t bi, int k) {
  return (nz[bi * 8 + (k >> 3)] >> (k & 7)) & 1;
}

void setNz(uint8_t* nz, uint32_t bi, int k) {
  nz[bi * 8 + (k >> 3)] |= static_cast<uint8_t>(1u << (k & 7));
}

void setCoef(int16_t* coefs, uint32_t bi, int k, int val) {
  const int slot = coefSlot(k);
  if (slot < 0 || !coefs) return;
  if (val > 32767) val = 32767;
  if (val < -32768) val = -32768;
  if (gStoreStride != 16) {
    coefs[bi * 4 + slot] = static_cast<int16_t>(val);
    return;
  }
  if (slot == 0) {
    coefs[bi] = static_cast<int16_t>(val);
    return;
  }
  int v = val;
  if (v > 127) v = 127;
  if (v < -128) v = -128;
  reinterpret_cast<int8_t*>(coefs + gLumaBlocks)[bi * 15 + (slot - 1)] = static_cast<int8_t>(v);
}

int getCoef(const int16_t* coefs, uint32_t bi, int k) {
  const int slot = coefSlot(k);
  if (slot < 0 || !coefs) return 0;
  if (gStoreStride != 16) return coefs[bi * 4 + slot];
  if (slot == 0) return coefs[bi];
  return reinterpret_cast<const int8_t*>(coefs + gLumaBlocks)[bi * 15 + (slot - 1)];
}

bool refineBit(ProgJpeg& pj, uint8_t* nz, int16_t* coefs, uint32_t bi, int k, int al, bool store) {
  const int b = pj.nextBit();
  if (b < 0) return false;
  if (!b || !store) return true;
  const int p1 = 1 << al;
  const int c = getCoef(coefs, bi, k);
  if ((c & p1) == 0) setCoef(coefs, bi, k, c + (c >= 0 ? p1 : -p1));
  (void)nz;
  return true;
}

bool acFirst(ProgJpeg& pj, const ProgHuff& table, uint8_t* nz, int16_t* coefs, uint32_t bi, int ss, int se,
             int al, bool store, uint32_t& eobrun) {
  if (eobrun) {
    --eobrun;
    return true;
  }
  int k = ss;
  while (k <= se) {
    const int rs = pj.decodeHuff(table);
    if (rs < 0) return false;
    const int sbits = rs & 15;
    const int run = rs >> 4;
    if (sbits) {
      k += run;
      if (k > se) return false;
      bool ok = true;
      int val = static_cast<int>(pj.receiveExtend(sbits, &ok));
      if (!ok) return false;
      val <<= al;
      setNz(nz, bi, k);
      if (store) setCoef(coefs, bi, k, val);
    } else if (run == 15) {
      k += 15;
    } else {
      eobrun = 1u << run;
      if (run) {
        const int extra = unsignedBits(pj, run);
        if (extra < 0) return false;
        eobrun += static_cast<uint32_t>(extra);
      }
      --eobrun;
      break;
    }
    ++k;
  }
  return true;
}

bool acRefine(ProgJpeg& pj, const ProgHuff& table, uint8_t* nz, int16_t* coefs, uint32_t bi, int ss, int se,
              int al, bool store, uint32_t& eobrun) {
  const int p1 = 1 << al;
  int k = ss;
  if (eobrun == 0) {
    while (k <= se) {
      const int rs = pj.decodeHuff(table);
      if (rs < 0) return false;
      int rr = rs >> 4;
      int s = rs & 15;
      if (s) {
        if (s != 1) return false;
        const int sb = pj.nextBit();
        if (sb < 0) return false;
        s = sb ? p1 : -p1;
      } else if (rr != 15) {
        eobrun = 1u << rr;
        if (rr) {
          const int extra = unsignedBits(pj, rr);
          if (extra < 0) return false;
          eobrun += static_cast<uint32_t>(extra);
        }
        break;
      }
      for (;;) {
        if (isNz(nz, bi, k)) {
          if (!refineBit(pj, nz, coefs, bi, k, al, store)) return false;
        } else {
          --rr;
          if (rr < 0) break;
        }
        ++k;
        if (k > se) break;
      }
      if (s) {
        if (k > 63) return false;
        setNz(nz, bi, k);
        if (store) setCoef(coefs, bi, k, s);
      }
      ++k;
    }
  }
  if (eobrun > 0) {
    while (k <= se) {
      if (isNz(nz, bi, k)) {
        if (!refineBit(pj, nz, coefs, bi, k, al, store)) return false;
      }
      ++k;
    }
    --eobrun;
  }
  return true;
}

// Natural-order slot → JPEG zigzag index, for the 4x4 low-frequency block.
constexpr uint8_t kSlotZz[16] = {0, 1, 5, 6, 2, 4, 7, 13, 3, 8, 12, 17, 9, 11, 16, 24};

// Cosine basis for sample positions 1,3,5,7 and frequencies 0..3, Q14, DC scaled by 1/sqrt(2).
constexpr int kBasis[4][4] = {
    {11585, 13623, 6270, -3196},
    {11585, 3196, -15137, -9102},
    {11585, -9102, -6270, 16069},
    {11585, -16069, 15137, -13623},
};

int idct4(const int* f, int xi, int yi) {
  int64_t sum = 0;
  for (int v = 0; v < 4; ++v) {
    for (int u = 0; u < 4; ++u) {
      sum += static_cast<int64_t>(kBasis[yi][v]) * kBasis[xi][u] * f[v * 4 + u];
    }
  }
  sum >>= 28;
  return static_cast<int>(sum / 4);
}

int idctQuad(int c00, int c01, int c10, int c11, int xs, int ys) {
  constexpr int kA0 = 11585;
  constexpr int kA1 = 9102;
  const int ax = xs * kA1;
  const int ay = ys * kA1;
  int64_t sum = static_cast<int64_t>(c00) * kA0 * kA0;
  sum += static_cast<int64_t>(c01) * kA0 * ay;
  sum += static_cast<int64_t>(c10) * ax * kA0;
  sum += static_cast<int64_t>(c11) * ax * ay;
  sum >>= 28;
  return static_cast<int>(sum / 4);
}

struct SharpJpeg {
  ProgJpeg pj;
  ProgHuff* dcTables = nullptr;
  ProgHuff* acTables = nullptr;
  uint16_t quant[4][64] = {};
  struct Comp {
    uint8_t id = 0, h = 1, v = 1, tq = 0, td = 0, ta = 0;
  } comps[4];
  uint8_t ncomp = 0;
  uint16_t width = 0, height = 0, restartInterval = 0;
  uint16_t mcusX = 0, mcusY = 0, blocksW = 0, blocksH = 0, padW = 0;
  uint8_t sampleN = 2;
  int16_t* coefs = nullptr;
  uint8_t* nz[4] = {};
  uint32_t nzBlocks[4] = {};
  int32_t pred[4] = {};
  uint32_t eobrun = 0;
};

void freeSharp(SharpJpeg& st) {
  free(st.dcTables);
  free(st.acTables);
  free(st.coefs);
  for (int i = 0; i < 4; ++i) free(st.nz[i]);
  st.dcTables = nullptr;
  st.acTables = nullptr;
  st.coefs = nullptr;
  for (int i = 0; i < 4; ++i) st.nz[i] = nullptr;
  gStoreStride = 4;
  gLumaBlocks = 0;
}

bool allocSharp(SharpJpeg& st) {
  uint8_t hmax = 1, vmax = 1;
  for (uint8_t c = 0; c < st.ncomp; ++c) {
    if (st.comps[c].h > hmax) hmax = st.comps[c].h;
    if (st.comps[c].v > vmax) vmax = st.comps[c].v;
  }
  st.mcusX = static_cast<uint16_t>((st.width + 8 * hmax - 1) / (8 * hmax));
  st.mcusY = static_cast<uint16_t>((st.height + 8 * vmax - 1) / (8 * vmax));
  st.blocksW = static_cast<uint16_t>((st.width + 7) / 8);
  st.blocksH = static_cast<uint16_t>((st.height + 7) / 8);
  st.padW = static_cast<uint16_t>(st.mcusX * st.comps[0].h);
  const uint32_t lumaBlocks = static_cast<uint32_t>(st.padW) * (st.mcusY * st.comps[0].v);
  gLumaBlocks = lumaBlocks;
  size_t nzBytes = 0;
  uint32_t blocks[4] = {};
  for (uint8_t c = 0; c < st.ncomp; ++c) {
    blocks[c] = static_cast<uint32_t>(st.mcusX * st.comps[c].h) * (st.mcusY * st.comps[c].v);
    nzBytes += static_cast<size_t>(blocks[c]) * 8;
  }
  // int16 DC + 15 int8 AC coefficients per luma block.
  const size_t coefWide = static_cast<size_t>(lumaBlocks) * (sizeof(int16_t) + 15);
  const size_t coefNarrow = static_cast<size_t>(lumaBlocks) * 4 * sizeof(int16_t);
  gStoreStride = 4;
  st.sampleN = 2;
  if (coefWide + nzBytes <= 6 * 1024 * 1024) {
    st.coefs = static_cast<int16_t*>(allocBuf(coefWide));
    if (st.coefs) {
      memset(st.coefs, 0, coefWide);
      gStoreStride = 16;
      st.sampleN = 4;
    }
  }
  if (!st.coefs) {
    if (coefNarrow + nzBytes > 5 * 1024 * 1024) return false;
    st.coefs = static_cast<int16_t*>(allocBuf(coefNarrow));
    if (!st.coefs) return false;
    memset(st.coefs, 0, coefNarrow);
    gStoreStride = 4;
    st.sampleN = 2;
  }
  for (uint8_t c = 0; c < st.ncomp; ++c) {
    st.nzBlocks[c] = blocks[c];
    st.nz[c] = static_cast<uint8_t*>(allocBuf(static_cast<size_t>(blocks[c]) * 8));
    if (!st.nz[c]) return false;
    memset(st.nz[c], 0, static_cast<size_t>(blocks[c]) * 8);
  }
  return true;
}

bool runSharpScan(SharpJpeg& st, const uint8_t* scanComp, uint8_t ns, int ss, int se, int ah, int al,
                  JpgAbortCheck abortCheck) {
  st.eobrun = 0;
  const bool interleaved = ns > 1;
  uint32_t rows = 0, cols = 0;
  if (interleaved) {
    rows = st.mcusY;
    cols = st.mcusX;
  } else {
    const auto& comp = st.comps[scanComp[0]];
    rows = static_cast<uint32_t>(st.mcusY) * comp.v;
    cols = static_cast<uint32_t>(st.mcusX) * comp.h;
  }
  uint32_t since = 0;
  for (uint32_t ry = 0; ry < rows; ++ry) {
    if ((ry & 1) == 0) {
      feedWatchdog();
      if (abortCheck && abortCheck()) return false;
    }
    for (uint32_t cx = 0; cx < cols; ++cx) {
      if (st.restartInterval && since == st.restartInterval) {
        st.pj.bitCount = 0;
        int mk = st.pj.pendingMarker;
        st.pj.pendingMarker = 0;
        if (mk == 0) mk = nextMarker(st.pj);
        if (mk < 0xD0 || mk > 0xD7) return false;
        st.pred[0] = st.pred[1] = st.pred[2] = st.pred[3] = 0;
        st.eobrun = 0;
        since = 0;
      }
      const uint8_t nsc = interleaved ? ns : 1;
      for (uint8_t sci = 0; sci < nsc; ++sci) {
        const uint8_t ci = scanComp[sci];
        const auto& comp = st.comps[ci];
        const uint8_t bh = interleaved ? comp.h : 1;
        const uint8_t bv = interleaved ? comp.v : 1;
        const uint32_t stride = static_cast<uint32_t>(st.mcusX) * comp.h;
        const bool store = ci == 0;
        for (uint8_t by = 0; by < bv; ++by) {
          for (uint8_t bx = 0; bx < bh; ++bx) {
            const uint32_t px = interleaved ? cx * comp.h + bx : cx;
            const uint32_t py = interleaved ? ry * comp.v + by : ry;
            const uint32_t bi = py * stride + px;
            if (bi >= st.nzBlocks[ci]) return false;
            bool ok = false;
            if (ss == 0 && se == 0) {
              if (ah == 0) {
                if (!st.dcTables[comp.td].ready) return false;
                const int t = st.pj.decodeHuff(st.dcTables[comp.td]);
                if (t < 0 || t > 15) return false;
                bool extOk = true;
                const int diff = t ? static_cast<int>(st.pj.receiveExtend(t, &extOk)) : 0;
                if (!extOk) return false;
                st.pred[ci] += diff;
                const int val = st.pred[ci] << al;
                if (val) setNz(st.nz[ci], bi, 0);
                if (store) setCoef(st.coefs, bi, 0, val);
                ok = true;
              } else {
                const int b = st.pj.nextBit();
                if (b < 0) return false;
                if (store && b) {
                  const int p1 = 1 << al;
                  const int c = getCoef(st.coefs, bi, 0);
                  setCoef(st.coefs, bi, 0, c | p1);
                }
                ok = true;
              }
            } else if (ah == 0) {
              if (!st.acTables[comp.ta].ready) return false;
              ok = acFirst(st.pj, st.acTables[comp.ta], st.nz[ci], st.coefs, bi, ss, se, al, store, st.eobrun);
            } else {
              if (!st.acTables[comp.ta].ready) return false;
              ok = acRefine(st.pj, st.acTables[comp.ta], st.nz[ci], st.coefs, bi, ss, se, al, store, st.eobrun);
            }
            if (!ok) return false;
          }
        }
      }
      ++since;
    }
  }
  st.pj.bitCount = 0;
  return true;
}

bool renderSharp(SharpJpeg& st) {
  const int n = st.sampleN == 4 ? 4 : 2;
  const int srcW = st.blocksW * n;
  const int srcH = st.blocksH * n;
  if (srcW < 2 || srcH < 2) return false;

  // Nonzero maps are only needed while reading the file.
  for (int i = 0; i < 4; ++i) {
    free(st.nz[i]);
    st.nz[i] = nullptr;
  }

  int cropX = 0, cropY = 0, cropW = srcW, cropH = srcH;
  if (static_cast<int64_t>(srcW) * kScreenH > static_cast<int64_t>(srcH) * kScreenW) {
    cropW = static_cast<int>((static_cast<int64_t>(srcH) * kScreenW + kScreenH / 2) / kScreenH);
    if (cropW < 1) cropW = 1;
    if (cropW > srcW) cropW = srcW;
    cropX = (srcW - cropW) / 2;
  } else {
    cropH = static_cast<int>((static_cast<int64_t>(srcW) * kScreenH + kScreenW / 2) / kScreenW);
    if (cropH < 1) cropH = 1;
    if (cropH > srcH) cropH = srcH;
    cropY = (srcH - cropH) / 2;
  }

  const size_t pixels = static_cast<size_t>(kScreenW) * kScreenH;
  auto* sum = static_cast<uint16_t*>(allocBuf(pixels * sizeof(uint16_t)));
  auto* cnt = static_cast<uint8_t*>(allocBuf(pixels));
  if (!sum || !cnt) {
    free(sum);
    free(cnt);
    return false;
  }
  memset(sum, 0, pixels * sizeof(uint16_t));
  memset(cnt, 0, pixels);

  const uint16_t* qt = st.quant[st.comps[0].tq];
  const int x0 = cropX / n;
  const int x1 = (cropX + cropW + n - 1) / n;
  const int y0 = cropY / n;
  const int y1 = (cropY + cropH + n - 1) / n;
  for (int by = y0; by < y1 && by < st.blocksH; ++by) {
    if ((by & 3) == 0) feedWatchdog();
    for (int bx = x0; bx < x1 && bx < st.blocksW; ++bx) {
      const uint32_t bi = static_cast<uint32_t>(by) * st.padW + bx;
      uint8_t pix[16];
      if (n == 4) {
        int f[16];
        for (int s = 0; s < 16; ++s) f[s] = getCoef(st.coefs, bi, kSlotZz[s]) * qt[kSlotZz[s]];
        for (int yi = 0; yi < 4; ++yi) {
          for (int xi = 0; xi < 4; ++xi) {
            int g = idct4(f, xi, yi) + 128;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            pix[yi * 4 + xi] = static_cast<uint8_t>(g);
          }
        }
      } else {
        const int c00 = getCoef(st.coefs, bi, 0) * qt[0];
        const int c01 = getCoef(st.coefs, bi, 1) * qt[1];
        const int c10 = getCoef(st.coefs, bi, 2) * qt[2];
        const int c11 = getCoef(st.coefs, bi, 4) * qt[4];
        const int samples[4] = {
            idctQuad(c00, c01, c10, c11, +1, +1), idctQuad(c00, c01, c10, c11, -1, +1),
            idctQuad(c00, c01, c10, c11, +1, -1), idctQuad(c00, c01, c10, c11, -1, -1)};
        for (int i = 0; i < 4; ++i) {
          int g = samples[i] + 128;
          if (g < 0) g = 0;
          if (g > 255) g = 255;
          pix[i] = static_cast<uint8_t>(g);
        }
      }
      for (int dy = 0; dy < n; ++dy) {
        const int sy = by * n + dy;
        if (sy < cropY || sy >= cropY + cropH) continue;
        const int screenY = static_cast<int>((static_cast<int64_t>(sy - cropY) * kScreenH) / cropH);
        if (screenY < 0 || screenY >= kScreenH) continue;
        for (int dx = 0; dx < n; ++dx) {
          const int sx = bx * n + dx;
          if (sx < cropX || sx >= cropX + cropW) continue;
          const int screenX = static_cast<int>((static_cast<int64_t>(sx - cropX) * kScreenW) / cropW);
          if (screenX < 0 || screenX >= kScreenW) continue;
          const size_t i = static_cast<size_t>(screenY) * kScreenW + screenX;
          const int sample = n == 4 ? pix[dy * 4 + dx] : pix[dy * 2 + dx];
          sum[i] = static_cast<uint16_t>(sum[i] + sample);
          if (cnt[i] != 255) ++cnt[i];
        }
      }
    }
  }
  feedWatchdog();

  auto* gray = static_cast<uint8_t*>(allocBuf(pixels));
  if (!gray) {
    free(sum);
    free(cnt);
    return false;
  }
  for (size_t i = 0; i < pixels; ++i) gray[i] = cnt[i] ? static_cast<uint8_t>(sum[i] / cnt[i]) : 255;
  free(sum);
  free(cnt);
  free(st.coefs);
  st.coefs = nullptr;

  sharpenGray(gray, kScreenW, kScreenH);
  ditherScreen(gray);
  free(gray);
  return true;
}

// Returns the marker that followed the scan, or -1.
int decodeSharpScan(SharpJpeg& st, uint32_t segLen, JpgAbortCheck abortCheck) {
  uint8_t nsB = 0;
  if (!readBytes(st.pj, &nsB, 1) || nsB == 0 || nsB > 4 || segLen != 1u + nsB * 2u + 3u) return -1;
  uint8_t scanComp[4] = {};
  for (uint8_t i = 0; i < nsB; ++i) {
    uint8_t sc[2];
    if (!readBytes(st.pj, sc, 2)) return -1;
    uint8_t idx = 0xFF;
    for (uint8_t c = 0; c < st.ncomp; ++c) {
      if (st.comps[c].id == sc[0]) idx = c;
    }
    if (idx == 0xFF) return -1;
    st.comps[idx].td = sc[1] >> 4;
    st.comps[idx].ta = sc[1] & 0x0F;
    scanComp[i] = idx;
  }
  uint8_t prog[3];
  if (!readBytes(st.pj, prog, 3)) return -1;
  const int ss = prog[0];
  const int se = prog[1];
  const int ah = prog[2] >> 4;
  const int al = prog[2] & 0x0F;
  if (ss > se || se > 63) return -1;
  if (!runSharpScan(st, scanComp, nsB, ss, se, ah, al, abortCheck)) return -1;
  int mk = st.pj.pendingMarker;
  st.pj.pendingMarker = 0;
  if (mk == 0) mk = nextMarker(st.pj);
  return mk;
}

bool decodeProgressiveSharp(File& file, JpgAbortCheck abortCheck) {
  file.seek(0);
  SharpJpeg st;
  st.pj.file = &file;
  for (auto& row : st.quant) {
    for (uint16_t& q : row) q = 1;
  }
  st.dcTables = static_cast<ProgHuff*>(calloc(4, sizeof(ProgHuff)));
  st.acTables = static_cast<ProgHuff*>(calloc(4, sizeof(ProgHuff)));
  if (!st.dcTables || !st.acTables) {
    freeSharp(st);
    return false;
  }
  uint8_t hdr[2];
  if (!readBytes(st.pj, hdr, 2) || hdr[0] != 0xFF || hdr[1] != 0xD8) {
    freeSharp(st);
    return false;
  }

  int marker = nextMarker(st.pj);
  bool ready = false;
  while (marker != 0xD9 && marker >= 0) {
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      marker = nextMarker(st.pj);
      continue;
    }
    uint8_t lenB[2];
    if (!readBytes(st.pj, lenB, 2)) break;
    uint32_t segLen = (static_cast<uint32_t>(lenB[0]) << 8 | lenB[1]);
    if (segLen < 2) break;
    segLen -= 2;
    if (marker == 0xDA) {
      if (!ready) break;
      marker = decodeSharpScan(st, segLen, abortCheck);
      if (marker < 0) break;
      continue;
    }
    if (marker == 0xDB) {
      while (segLen > 0) {
        uint8_t pqtq = 0;
        if (!readBytes(st.pj, &pqtq, 1)) {
          marker = -1;
          break;
        }
        const uint8_t pq = pqtq >> 4;
        const uint8_t tq = pqtq & 0x0F;
        const uint32_t n = pq ? 128u : 64u;
        if (segLen < 1 + n || tq > 3) {
          marker = -1;
          break;
        }
        uint8_t vals[128];
        if (!readBytes(st.pj, vals, n)) {
          marker = -1;
          break;
        }
        if (pq) {
          for (int i = 0; i < 64; ++i) st.quant[tq][i] = static_cast<uint16_t>((vals[2 * i] << 8) | vals[2 * i + 1]);
        } else {
          for (int i = 0; i < 64; ++i) st.quant[tq][i] = vals[i];
        }
        segLen -= 1 + n;
      }
      if (marker < 0) break;
    } else if (marker == 0xC4) {
      while (segLen > 0) {
        uint8_t tcth = 0;
        if (!readBytes(st.pj, &tcth, 1)) {
          marker = -1;
          break;
        }
        const uint8_t tc = tcth >> 4;
        const uint8_t th = tcth & 0x0F;
        uint8_t counts[16];
        if (segLen < 17 || th > 3 || !readBytes(st.pj, counts, 16)) {
          marker = -1;
          break;
        }
        uint32_t total = 0;
        for (int i = 0; i < 16; ++i) total += counts[i];
        if (total > 256 || segLen < 17 + total) {
          marker = -1;
          break;
        }
        uint8_t symbols[256];
        if (!readBytes(st.pj, symbols, total)) {
          marker = -1;
          break;
        }
        ProgHuff& table = tc == 0 ? st.dcTables[th] : st.acTables[th];
        memset(&table, 0, sizeof(table));
        for (int i = 0; i < 16; ++i) table.counts[i + 1] = counts[i];
        memcpy(table.symbols, symbols, total);
        buildProgHuff(table);
        segLen -= 17 + total;
      }
      if (marker < 0) break;
    } else if (marker == 0xC2) {
      uint8_t sof[6];
      if (segLen < 6 || !readBytes(st.pj, sof, 6)) break;
      st.height = static_cast<uint16_t>((sof[1] << 8) | sof[2]);
      st.width = static_cast<uint16_t>((sof[3] << 8) | sof[4]);
      st.ncomp = sof[5];
      if (st.ncomp == 0 || st.ncomp > 4 || st.width == 0 || st.height == 0 || st.width > 8000 || st.height > 8000 ||
          segLen != 6u + st.ncomp * 3u) {
        break;
      }
      bool bad = false;
      for (uint8_t c = 0; c < st.ncomp; ++c) {
        uint8_t cc[3];
        if (!readBytes(st.pj, cc, 3)) {
          bad = true;
          break;
        }
        st.comps[c].id = cc[0];
        st.comps[c].h = cc[1] >> 4;
        st.comps[c].v = cc[1] & 0x0F;
        st.comps[c].tq = cc[2] & 0x03;
        if (st.comps[c].h == 0 || st.comps[c].v == 0 || st.comps[c].h > 4 || st.comps[c].v > 4) bad = true;
      }
      if (bad || !allocSharp(st)) break;
      ready = true;
    } else if (marker == 0xDD) {
      uint8_t d[2];
      if (segLen != 2 || !readBytes(st.pj, d, 2)) break;
      st.restartInterval = static_cast<uint16_t>((d[0] << 8) | d[1]);
    } else if (marker == 0xC0 || marker == 0xC1) {
      break;
    } else {
      if (!readBytes(st.pj, nullptr, segLen)) break;
    }
    marker = nextMarker(st.pj);
  }

  const bool ok = marker == 0xD9 && ready && renderSharp(st);
  if (ok) {
    Serial.printf("JPEG: sharp %dx%d from %ux%u\n", st.blocksW * st.sampleN, st.blocksH * st.sampleN, st.width,
                  st.height);
  }
  freeSharp(st);
  return ok;
}

bool decodeProgressiveDc(File& file, JpgAbortCheck abortCheck) {
  file.seek(0);
  ProgJpeg pj;
  pj.file = &file;

  auto rd = [&](uint8_t* dst, uint32_t n) -> bool {
    for (uint32_t i = 0; i < n; ++i) {
      const int b = pj.rawByte();
      if (b < 0) return false;
      dst[i] = static_cast<uint8_t>(b);
    }
    return true;
  };

  uint8_t hdr[2];
  if (!rd(hdr, 2) || hdr[0] != 0xFF || hdr[1] != 0xD8) return false;

  ProgHuff dcTables[4];
  uint16_t width = 0, height = 0, restartInterval = 0;
  uint16_t dcQuant[4] = {1, 1, 1, 1};
  struct Comp {
    uint8_t id = 0, h = 1, v = 1, tq = 0, td = 0;
  } comps[4];
  uint8_t ncomp = 0;
  uint8_t scanComp[4] = {};
  uint8_t ns = 0;
  uint8_t al = 0;

  for (;;) {
    int m = pj.rawByte();
    while (m == 0xFF) m = pj.rawByte();
    if (m < 0) return false;
    if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) continue;
    uint8_t lenB[2];
    if (!rd(lenB, 2)) return false;
    uint32_t segLen = (static_cast<uint32_t>(lenB[0]) << 8 | lenB[1]);
    if (segLen < 2) return false;
    segLen -= 2;
    if (m == 0xDB) {
      while (segLen > 0) {
        uint8_t pqtq;
        if (!rd(&pqtq, 1)) return false;
        const uint8_t pq = pqtq >> 4, tq = pqtq & 0x0F;
        const uint32_t bytes = pq ? 128 : 64;
        if (segLen < 1 + bytes || tq > 3) return false;
        uint8_t q0[2];
        if (!rd(q0, pq ? 2 : 1)) return false;
        dcQuant[tq] = pq ? static_cast<uint16_t>(q0[0] << 8 | q0[1]) : q0[0];
        for (uint32_t i = pq ? 2u : 1u; i < bytes; ++i) {
          uint8_t skip;
          if (!rd(&skip, 1)) return false;
        }
        segLen -= 1 + bytes;
      }
    } else if (m == 0xC4) {
      while (segLen > 0) {
        uint8_t tcth;
        if (!rd(&tcth, 1)) return false;
        const uint8_t tc = tcth >> 4, th = tcth & 0x0F;
        uint8_t counts[16];
        if (segLen < 17 || !rd(counts, 16)) return false;
        uint32_t total = 0;
        for (int i = 0; i < 16; ++i) total += counts[i];
        if (total > 256 || segLen < 17 + total) return false;
        if (tc == 0 && th < 4) {
          ProgHuff& t = dcTables[th];
          for (int i = 0; i < 16; ++i) t.counts[i + 1] = counts[i];
          if (!rd(t.symbols, total)) return false;
          buildProgHuff(t);
        } else {
          uint8_t skip;
          for (uint32_t i = 0; i < total; ++i) {
            if (!rd(&skip, 1)) return false;
          }
        }
        segLen -= 17 + total;
      }
    } else if (m == 0xC2) {
      uint8_t sof[6];
      if (!rd(sof, 6)) return false;
      height = static_cast<uint16_t>(sof[1] << 8 | sof[2]);
      width = static_cast<uint16_t>(sof[3] << 8 | sof[4]);
      ncomp = sof[5];
      if (ncomp == 0 || ncomp > 4 || segLen != 6u + ncomp * 3u) return false;
      for (uint8_t c = 0; c < ncomp; ++c) {
        uint8_t cc[3];
        if (!rd(cc, 3)) return false;
        comps[c].id = cc[0];
        comps[c].h = cc[1] >> 4;
        comps[c].v = cc[1] & 0x0F;
        comps[c].tq = cc[2] & 0x03;
        if (comps[c].h == 0 || comps[c].v == 0) return false;
      }
    } else if (m == 0xC0 || m == 0xC1) {
      return false;
    } else if (m == 0xDD) {
      uint8_t d[2];
      if (segLen != 2 || !rd(d, 2)) return false;
      restartInterval = static_cast<uint16_t>(d[0] << 8 | d[1]);
    } else if (m == 0xDA) {
      uint8_t nsB;
      if (!rd(&nsB, 1)) return false;
      ns = nsB;
      if (ns == 0 || ns > 4 || segLen != 1u + ns * 2u + 3u) return false;
      for (uint8_t i = 0; i < ns; ++i) {
        uint8_t sc[2];
        if (!rd(sc, 2)) return false;
        uint8_t idx = 0xFF;
        for (uint8_t c = 0; c < ncomp; ++c) {
          if (comps[c].id == sc[0]) idx = c;
        }
        if (idx == 0xFF) return false;
        comps[idx].td = sc[1] >> 4;
        scanComp[i] = idx;
      }
      uint8_t prog[3];
      if (!rd(prog, 3)) return false;
      if (prog[0] != 0 || prog[1] != 0 || (prog[2] >> 4) != 0) return false;
      al = prog[2] & 0x0F;
      break;
    } else if (m == 0xD9) {
      return false;
    } else {
      uint8_t skip;
      for (uint32_t i = 0; i < segLen; ++i) {
        if (!rd(&skip, 1)) return false;
      }
    }
  }
  if (width == 0 || height == 0 || width > 12000 || height > 12000) return false;

  uint8_t hmax = 1, vmax = 1;
  for (uint8_t c = 0; c < ncomp; ++c) {
    if (comps[c].h > hmax) hmax = comps[c].h;
    if (comps[c].v > vmax) vmax = comps[c].v;
  }
  const Comp& y = comps[0];
  if (y.h != hmax || y.v != vmax) return false;
  const bool interleaved = ns > 1;
  if (!interleaved && scanComp[0] != 0) return false;

  const uint16_t blocksW = static_cast<uint16_t>((width + 7) / 8);
  const uint16_t blocksH = static_cast<uint16_t>((height + 7) / 8);
  const uint16_t mcusX = static_cast<uint16_t>((width + 8 * hmax - 1) / (8 * hmax));
  const uint16_t mcusY = static_cast<uint16_t>((height + 8 * vmax - 1) / (8 * vmax));
  const uint16_t padBlocksW = interleaved ? static_cast<uint16_t>(mcusX * y.h) : blocksW;

  const size_t imgBytes = static_cast<size_t>(blocksW) * blocksH;
  auto* img = static_cast<uint8_t*>(allocBuf(imgBytes));
  auto* rowBuf = static_cast<uint8_t*>(allocBuf(static_cast<size_t>(padBlocksW) * y.v));
  if (!img || !rowBuf) {
    free(img);
    free(rowBuf);
    return false;
  }
  memset(img, 0xFF, imgBytes);

  int32_t pred[4] = {};
  uint32_t sinceRestart = 0;
  const uint16_t q0 = dcQuant[y.tq];
  uint32_t emittedBlockRows = 0;
  const uint32_t rows = interleaved ? mcusY : blocksH;
  const uint32_t cols = interleaved ? mcusX : blocksW;
  bool failed = false;

  for (uint32_t r = 0; r < rows && !failed; ++r) {
    if ((r & 3) == 0) {
      feedWatchdog();
      if (abortCheck && abortCheck()) failed = true;
    }
    for (uint32_t c = 0; c < cols && !failed; ++c) {
      if (restartInterval != 0 && sinceRestart == restartInterval) {
        pj.bitCount = 0;
        int mk = pj.pendingMarker;
        pj.pendingMarker = 0;
        if (mk == 0) {
          int b = pj.rawByte();
          while (b == 0xFF) b = pj.rawByte();
          mk = b;
        }
        if (mk < 0xD0 || mk > 0xD7) {
          failed = true;
          break;
        }
        pred[0] = pred[1] = pred[2] = pred[3] = 0;
        sinceRestart = 0;
      }
      const uint8_t nsc = interleaved ? ns : 1;
      for (uint8_t sci = 0; sci < nsc && !failed; ++sci) {
        const uint8_t ci = scanComp[sci];
        const Comp& comp = comps[ci];
        if (!dcTables[comp.td].ready) {
          failed = true;
          break;
        }
        const uint8_t bh = interleaved ? comp.h : 1;
        const uint8_t bv = interleaved ? comp.v : 1;
        for (uint8_t by = 0; by < bv; ++by) {
          for (uint8_t bx = 0; bx < bh; ++bx) {
            const int t = pj.decodeHuff(dcTables[comp.td]);
            if (t < 0 || t > 15) {
              failed = true;
              break;
            }
            bool ok = true;
            const int32_t diff = pj.receiveExtend(t, &ok);
            if (!ok) {
              failed = true;
              break;
            }
            pred[ci] += diff;
            if (ci == 0) {
              const int32_t dc = (pred[ci] << al) * static_cast<int32_t>(q0);
              int32_t gray = 128 + dc / 8;
              if (gray < 0) gray = 0;
              if (gray > 255) gray = 255;
              const uint32_t px = interleaved ? c * y.h + bx : c;
              if (px < padBlocksW) {
                rowBuf[static_cast<uint32_t>(by) * padBlocksW + px] = static_cast<uint8_t>(gray);
              }
            }
          }
        }
      }
      ++sinceRestart;
    }
    const uint8_t rowsHere = interleaved ? y.v : 1;
    for (uint8_t by = 0; by < rowsHere && emittedBlockRows < blocksH; ++by) {
      memcpy(img + static_cast<size_t>(emittedBlockRows) * blocksW,
             rowBuf + static_cast<uint32_t>(by) * padBlocksW, blocksW);
      ++emittedBlockRows;
    }
  }
  feedWatchdog();

  if (failed || emittedBlockRows == 0) {
    free(img);
    free(rowBuf);
    Serial.println("JPEG: progressive DC scan failed");
    return false;
  }
  ditherGrayToCanvas(img, blocksW, emittedBlockRows);
  free(img);
  free(rowBuf);
  return true;
}

}  // namespace

bool jpgDrawFile(const char* path, JpgAbortCheck abortCheck) {
  if (gJpegFile) gJpegFile.close();
  gJpegFile = SD.open(path, FILE_READ);
  if (!gJpegFile) {
    Serial.printf("JPEG: open failed (%s)\n", path);
    return false;
  }

  if (!gJpeg.open(gJpegFile, jpegDrawBlock)) {
    Serial.printf("JPEG: parse failed (%s) err=%d\n", path, gJpeg.getLastError());
    gJpegFile.close();
    return false;
  }

  const int srcW = gJpeg.getWidth();
  const int srcH = gJpeg.getHeight();
  const bool progressive = gJpeg.getJPEGType() == JPEG_MODE_PROGRESSIVE;
  Serial.printf("JPEG: %s %dx%d progressive=%d\n", path, srcW, srcH, progressive ? 1 : 0);
  if (srcW <= 0 || srcH <= 0 || srcW > 20000 || srcH > 20000) {
    gJpeg.close();
    gJpegFile.close();
    return false;
  }

  // JPEGDEC's progressive path faults on these files. Decode the DC scan ourselves.
  // close() also closes the SD file.
  if (progressive) {
    gJpeg.close();
    gJpegFile = SD.open(path, FILE_READ);
    if (!gJpegFile) return false;
    bool ok = decodeProgressiveSharp(gJpegFile, abortCheck);
    if (!ok && !(abortCheck && abortCheck())) {
      Serial.println("JPEG: sharp decode failed, using preview");
      gJpegFile.seek(0);
      ok = decodeProgressiveDc(gJpegFile, abortCheck);
    }
    gJpegFile.close();
    return ok;
  }

  int outW = 0, outH = 0;
  const int opts = pickScaleOptions(srcW, srcH, false, outW, outH);
  return decodeIntoCanvas(opts, outW, outH, abortCheck);
}
