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

// Scale the grayscale image to cover the panel (crop the longer axis), then dither.
void ditherGrayToCanvas(const uint8_t* img, int srcW, int srcH) {
  if (!img || srcW < 1 || srcH < 1) return;
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
    const bool ok = decodeProgressiveDc(gJpegFile, abortCheck);
    gJpegFile.close();
    return ok;
  }

  int outW = 0, outH = 0;
  const int opts = pickScaleOptions(srcW, srcH, false, outW, outH);
  return decodeIntoCanvas(opts, outW, outH, abortCheck);
}
