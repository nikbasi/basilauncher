/*
 * Direct page fetch, adapted from Babe32 (Alun Morris).
 * Proxy and residential-IP fallbacks are left out: this device fetches
 * the origin itself over TLS, then follows redirects.
 */
#include "fetch.h"

#include "url_utils.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <mbedtls/ssl.h>

#include <cctype>
#include <cstring>

extern "C" size_t tinfl_decompress_mem_to_mem(void* pOut_buf, size_t out_buf_len, const void* pSrc_buf,
                                             size_t src_buf_len, int flags);

namespace {

constexpr size_t kFetchCap = 1024 * 1024;
constexpr uint32_t kIoMs = 10000;

char* gBuf = nullptr;
WiFiClient* gClient = nullptr;
char gHost[128] = {};
bool gHttps = false;

class BrowserTls : public WiFiClientSecure {
 public:
  bool connectChrome(const char* host, uint16_t port) {
    static const int suites[] = {
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
        MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA,
        0,
    };
    setPlainStart();
    if (!WiFiClientSecure::connect(host, port)) return false;
    mbedtls_ssl_conf_ciphersuites(&sslclient->ssl_conf, suites);
    return startTLS() == 1;
  }
};

void closeClient() {
  if (gClient) {
    gClient->stop();
    delete gClient;
    gClient = nullptr;
  }
  gHost[0] = 0;
}

bool ensureBuf() {
  if (gBuf) return true;
  gBuf = static_cast<char*>(heap_caps_malloc(kFetchCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return gBuf != nullptr;
}

bool splitUrl(const char* url, char* host, size_t hostLen, int* port, const char** path, bool* https) {
  if (!url || !host || !port || !path || !https) return false;
  *https = strncmp(url, "https://", 8) == 0;
  const bool http = strncmp(url, "http://", 7) == 0;
  if (!*https && !http) return false;
  const char* start = url + (*https ? 8 : 7);
  const char* slash = strchr(start, '/');
  const char* colon = nullptr;
  for (const char* p = start; *p && p != slash; ++p) {
    if (*p == ':') colon = p;
  }
  const char* hostEnd = colon ? colon : (slash ? slash : start + strlen(start));
  const size_t n = static_cast<size_t>(hostEnd - start);
  if (n == 0 || n >= hostLen) return false;
  memcpy(host, start, n);
  host[n] = 0;
  *port = *https ? 443 : 80;
  if (colon && slash != colon + 1) *port = atoi(colon + 1);
  *path = slash ? slash : "/";
  return true;
}

bool connectTo(const char* host, int port, bool https) {
  if (gClient && gClient->connected() && gHttps == https && strcmp(gHost, host) == 0) return true;
  closeClient();
  if (https) {
    auto* tls = new (std::nothrow) BrowserTls();
    if (!tls) return false;
    tls->setInsecure();
    tls->setTimeout(12);
    if (!tls->connectChrome(host, static_cast<uint16_t>(port))) {
      delete tls;
      tls = new (std::nothrow) BrowserTls();
      if (!tls) return false;
      tls->setInsecure();
      tls->setTimeout(12);
      if (!tls->connect(host, static_cast<uint16_t>(port))) {
        delete tls;
        return false;
      }
    }
    gClient = tls;
  } else {
    auto* plain = new (std::nothrow) WiFiClient();
    if (!plain) return false;
    plain->setTimeout(12);
    if (!plain->connect(host, static_cast<uint16_t>(port))) {
      delete plain;
      return false;
    }
    gClient = plain;
  }
  gHttps = https;
  snprintf(gHost, sizeof(gHost), "%s", host);
  return true;
}

size_t readSome(size_t offset, size_t len) {
  size_t got = 0;
  if (offset >= kFetchCap - 1) return 0;
  if (offset + len > kFetchCap - 1) len = kFetchCap - 1 - offset;
  uint32_t idle = millis();
  while (got < len && (gClient->connected() || gClient->available())) {
    const int avail = gClient->available();
    if (avail > 0) {
      size_t want = static_cast<size_t>(avail);
      if (want > len - got) want = len - got;
      got += gClient->readBytes(gBuf + offset + got, want);
      idle = millis();
    } else if (millis() - idle > kIoMs) {
      break;
    } else {
      delay(1);
    }
  }
  return got;
}

size_t readUntilClose(size_t offset) {
  size_t got = 0;
  uint32_t idle = millis();
  while (offset + got < kFetchCap - 1 && (gClient->connected() || gClient->available())) {
    const int avail = gClient->available();
    if (avail > 0) {
      size_t room = kFetchCap - 1 - (offset + got);
      size_t want = static_cast<size_t>(avail);
      if (want > room) want = room;
      got += gClient->readBytes(gBuf + offset + got, want);
      idle = millis();
    } else if (millis() - idle > kIoMs) {
      break;
    } else {
      delay(1);
    }
  }
  return got;
}

int readChunked(size_t bodyAt) {
  size_t out = bodyAt;
  while (out < kFetchCap - 1) {
    char line[16];
    size_t n = 0;
    uint32_t idle = millis();
    bool ended = false;
    while (n + 1 < sizeof(line)) {
      if (gClient->available()) {
        char c = 0;
        if (gClient->readBytes(reinterpret_cast<uint8_t*>(&c), 1) != 1) break;
        idle = millis();
        if (c == '\n') {
          ended = true;
          break;
        }
        if (c != '\r') line[n++] = c;
      } else if (!gClient->connected() || millis() - idle > kIoMs) {
        break;
      } else {
        delay(1);
      }
    }
    line[n] = 0;
    if (!ended && n == 0) break;
    const unsigned long chunk = strtoul(line, nullptr, 16);
    if (chunk == 0) break;
    size_t want = chunk;
    if (out + want > kFetchCap - 1) want = kFetchCap - 1 - out;
    const size_t got = readSome(out, want);
    out += got;
    char crlf[2];
    gClient->readBytes(reinterpret_cast<uint8_t*>(crlf), 2);
    if (got < chunk) break;
  }
  return static_cast<int>(out - bodyAt);
}

struct HtmlFilter {
  enum class St : uint8_t { Text, Pending, Skip };
  St st = St::Text;
  char pending[24];
  uint8_t pendingN = 0;
  char endTag[12];
  uint8_t endN = 0;
  uint8_t endPos = 0;

  static bool nameIs(const char* name, int n, const char* lit) {
    int i = 0;
    for (; lit[i]; ++i) {
      if (i >= n || tolower(static_cast<unsigned char>(name[i])) != lit[i]) return false;
    }
    return i == n;
  }

  bool nameReady() const {
    if (pendingN < 2 || pending[0] != '<') return pendingN >= 2;
    const unsigned char second = static_cast<unsigned char>(pending[1]);
    if (second == '/' || second == '!' || second == '?') return true;
    int i = 1;
    while (i < pendingN && isalpha(static_cast<unsigned char>(pending[i]))) ++i;
    if (i == pendingN) return pendingN >= 16;
    return true;
  }

  size_t feed(char* dest, size_t cap, const char* src, size_t n) {
    size_t used = 0;
    for (size_t i = 0; i < n && used < cap; ++i) {
      const char c = src[i];
      if (st == St::Skip) {
        if (tolower(static_cast<unsigned char>(c)) == endTag[endPos]) {
          if (++endPos >= endN) {
            st = St::Text;
            endPos = 0;
          }
        } else {
          endPos = (tolower(static_cast<unsigned char>(c)) == endTag[0]) ? 1 : 0;
        }
        continue;
      }
      if (st == St::Text) {
        if (c == '<') {
          pending[0] = '<';
          pendingN = 1;
          st = St::Pending;
        } else {
          dest[used++] = c;
        }
        continue;
      }
      if (pendingN < sizeof(pending)) pending[pendingN++] = c;
      if (!nameReady()) continue;
      int nameAt = 1;
      while (nameAt < pendingN && isalpha(static_cast<unsigned char>(pending[nameAt]))) ++nameAt;
      const int nameLen = nameAt - 1;
      if (nameIs(pending + 1, nameLen, "script") || nameIs(pending + 1, nameLen, "style")) {
        const bool script = nameIs(pending + 1, nameLen, "script");
        const char* end = script ? "</script>" : "</style>";
        endN = static_cast<uint8_t>(strlen(end));
        memcpy(endTag, end, endN);
        endPos = 0;
        st = St::Skip;
        pendingN = 0;
        continue;
      }
      const size_t room = cap - used;
      const size_t copyN = pendingN < room ? pendingN : room;
      memcpy(dest + used, pending, copyN);
      used += copyN;
      pendingN = 0;
      st = St::Text;
    }
    return used;
  }

  size_t finish(char* dest, size_t cap) {
    if (st != St::Pending || pendingN == 0 || cap == 0) return 0;
    size_t n = pendingN;
    if (n > cap) n = cap;
    memcpy(dest, pending, n);
    pendingN = 0;
    st = St::Text;
    return n;
  }
};

size_t readRaw(char* dest, size_t len) {
  size_t got = 0;
  uint32_t idle = millis();
  while (got < len && (gClient->connected() || gClient->available())) {
    const int avail = gClient->available();
    if (avail > 0) {
      size_t want = static_cast<size_t>(avail);
      if (want > len - got) want = len - got;
      got += gClient->readBytes(reinterpret_cast<uint8_t*>(dest + got), want);
      idle = millis();
    } else if (millis() - idle > kIoMs) {
      break;
    } else {
      delay(1);
    }
  }
  return got;
}

size_t readVisible(size_t offset, size_t maxStore, size_t rawLimit, bool limitRaw) {
  HtmlFilter filt;
  size_t stored = 0;
  size_t raw = 0;
  char tmp[512];
  uint32_t idle = millis();
  while (stored < maxStore && (!limitRaw || raw < rawLimit) && (gClient->connected() || gClient->available())) {
    const int avail = gClient->available();
    if (avail > 0) {
      size_t want = sizeof(tmp);
      if (want > static_cast<size_t>(avail)) want = static_cast<size_t>(avail);
      if (limitRaw && raw + want > rawLimit) want = rawLimit - raw;
      if (want == 0) break;
      const size_t got = readRaw(tmp, want);
      if (got == 0) break;
      raw += got;
      stored += filt.feed(gBuf + offset + stored, maxStore - stored, tmp, got);
      idle = millis();
    } else if (millis() - idle > kIoMs) {
      break;
    } else {
      delay(1);
    }
  }
  if (stored < maxStore) stored += filt.finish(gBuf + offset + stored, maxStore - stored);
  return stored;
}

size_t gzipPayload(const uint8_t* p, size_t n) {
  if (n < 18 || p[0] != 0x1f || p[1] != 0x8b || p[2] != 8) return 0;
  size_t off = 10;
  const uint8_t flags = p[3];
  if (flags & 0x04) {
    if (off + 2 > n) return 0;
    const uint16_t extra = static_cast<uint16_t>(p[off] | (p[off + 1] << 8));
    off += 2u + extra;
  }
  if (flags & 0x08) {
    while (off < n && p[off] != 0) ++off;
    ++off;
  }
  if (flags & 0x10) {
    while (off < n && p[off] != 0) ++off;
    ++off;
  }
  if (flags & 0x02) off += 2;
  if (off + 8 >= n) return 0;
  return off;
}

size_t stripScripts(char* dest, size_t cap, const char* src, size_t n) {
  HtmlFilter filt;
  size_t stored = 0;
  size_t off = 0;
  while (off < n && stored < cap) {
    const size_t chunk = n - off > 512 ? 512 : n - off;
    stored += filt.feed(dest + stored, cap - stored, src + off, chunk);
    off += chunk;
  }
  if (stored < cap) stored += filt.finish(dest + stored, cap - stored);
  return stored;
}

bool inflateGzip(char* buf, size_t compLen, size_t* outLen) {
  const size_t payload = gzipPayload(reinterpret_cast<const uint8_t*>(buf), compLen);
  if (!payload || !outLen) return false;
  const size_t srcLen = compLen - payload - 8;
  constexpr size_t kOut = 1536 * 1024;
  char* plain = static_cast<char*>(heap_caps_malloc(kOut, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!plain) return false;
  const size_t n = tinfl_decompress_mem_to_mem(plain, kOut - 1, buf + payload, srcLen, 4);
  if (n == static_cast<size_t>(-1) || n == 0) {
    heap_caps_free(plain);
    return false;
  }
  plain[n] = 0;
  const size_t kept = stripScripts(buf, kFetchCap - 1, plain, n);
  heap_caps_free(plain);
  buf[kept] = 0;
  *outLen = kept;
  return true;
}

struct Reply {
  int body = -1;
  bool redirect = false;
};

Reply oneRequest(const char* url, const char* postBody, char* locationOut, size_t locationLen) {
  Reply reply;
  if (locationOut && locationLen) locationOut[0] = 0;
  char host[128];
  int port = 0;
  const char* path = "/";
  bool https = false;
  if (!splitUrl(url, host, sizeof(host), &port, &path, &https)) return reply;
  if (!connectTo(host, port, https)) return reply;

  char req[1800];
  int reqLen = 0;
  if (postBody) {
    reqLen = snprintf(req, sizeof(req),
                      "POST %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "User-Agent: Mozilla/5.0 (compatible; Basilauncher)\r\n"
                      "Accept: text/html,*/*;q=0.8\r\n"
                      "Accept-Encoding: identity\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n"
                      "Content-Length: %u\r\n"
                      "Connection: close\r\n\r\n",
                      path, host, static_cast<unsigned>(strlen(postBody)));
  } else {
    reqLen = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n"
                      "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                      "Accept-Language: en-GB,en;q=0.9\r\n"
                      "Accept-Encoding: identity\r\n"
                      "Connection: close\r\n\r\n",
                      path, host);
  }
  if (reqLen <= 0 || reqLen >= static_cast<int>(sizeof(req))) return reply;
  gClient->write(reinterpret_cast<const uint8_t*>(req), reqLen);
  if (postBody) gClient->write(reinterpret_cast<const uint8_t*>(postBody), strlen(postBody));

  size_t hdr = 0;
  uint32_t idle = millis();
  bool found = false;
  while (hdr + 1 < kFetchCap && hdr < 8192) {
    if (gClient->available()) {
      gBuf[hdr++] = static_cast<char>(gClient->read());
      idle = millis();
      if (hdr >= 4 && gBuf[hdr - 4] == '\r' && gBuf[hdr - 3] == '\n' && gBuf[hdr - 2] == '\r' &&
          gBuf[hdr - 1] == '\n') {
        found = true;
        break;
      }
    } else if (!gClient->connected() || millis() - idle > kIoMs) {
      break;
    } else {
      delay(1);
    }
  }
  gBuf[hdr] = 0;
  if (!found) {
    closeClient();
    return reply;
  }

  int status = 0;
  sscanf(gBuf, "HTTP/%*d.%*d %d", &status);

  long length = -1;
  bool chunked = false;
  if (char* cl = strcasestr(gBuf, "\r\nContent-Length:")) {
    cl += 17;
    while (*cl == ' ') ++cl;
    length = strtol(cl, nullptr, 10);
  }
  if (char* te = strcasestr(gBuf, "\r\nTransfer-Encoding:")) {
    te += 20;
    while (*te == ' ') ++te;
    if (strncasecmp(te, "chunked", 7) == 0) chunked = true;
  }

  if (status >= 301 && status <= 308 && locationOut && locationLen) {
    if (char* loc = strcasestr(gBuf, "\r\nLocation:")) {
      loc += 11;
      while (*loc == ' ') ++loc;
      char* end = strstr(loc, "\r\n");
      if (end) {
        size_t n = static_cast<size_t>(end - loc);
        if (n >= locationLen) n = locationLen - 1;
        memcpy(locationOut, loc, n);
        locationOut[n] = 0;
      }
    }
    closeClient();
    reply.redirect = true;
    reply.body = status;
    return reply;
  }

  bool gzip = false;
  bool html = true;
  if (char* ce = strcasestr(gBuf, "\r\nContent-Encoding:")) {
    ce += 19;
    while (*ce == ' ' || *ce == '\t') ++ce;
    if (strncasecmp(ce, "gzip", 4) == 0) gzip = true;
  }
  if (char* ct = strcasestr(gBuf, "\r\nContent-Type:")) {
    ct += 15;
    while (*ct == ' ' || *ct == '\t') ++ct;
    char kind[80];
    size_t ki = 0;
    while (ct[ki] && ct[ki] != '\r' && ct[ki] != '\n' && ki + 1 < sizeof(kind)) {
      kind[ki] = ct[ki];
      ++ki;
    }
    kind[ki] = 0;
    html = strcasestr(kind, "html") != nullptr || strcasestr(kind, "xml") != nullptr ||
           strncasecmp(kind, "text/", 5) == 0;
  }
  const bool filter = html && !gzip;
  const size_t room = kFetchCap - 1 - hdr;

  size_t body = 0;
  if (length >= 0) {
    if (filter) body = readVisible(hdr, room, static_cast<size_t>(length), true);
    else body = readSome(hdr, static_cast<size_t>(length));
  } else if (chunked) {
    body = static_cast<size_t>(readChunked(hdr));
  } else if (filter) {
    body = readVisible(hdr, room, 0, false);
  } else {
    body = readUntilClose(hdr);
  }
  closeClient();

  const size_t total = hdr + body;
  gBuf[total] = 0;
  char* raw = strstr(gBuf, "\r\n\r\n");
  if (!raw) return reply;
  raw += 4;
  size_t bodyLen = total - static_cast<size_t>(raw - gBuf);
  memmove(gBuf, raw, bodyLen);
  gBuf[bodyLen] = 0;
  if (gzip) {
    size_t plain = 0;
    if (inflateGzip(gBuf, bodyLen, &plain)) bodyLen = plain;
  } else if (html && chunked) {
    char* compact = static_cast<char*>(heap_caps_malloc(bodyLen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (compact) {
      memcpy(compact, gBuf, bodyLen);
      bodyLen = stripScripts(gBuf, kFetchCap - 1, compact, bodyLen);
      gBuf[bodyLen] = 0;
      heap_caps_free(compact);
    }
  }
  if ((status >= 200 && status < 300) || bodyLen > 0) reply.body = static_cast<int>(bodyLen);
  return reply;
}

int fetchImpl(const char* url, const char* postBody, char** bufOut) {
  if (!ensureBuf()) {
    if (bufOut) *bufOut = nullptr;
    return -1;
  }
  if (bufOut) *bufOut = gBuf;
  char cur[512];
  snprintf(cur, sizeof(cur), "%s", url ? url : "");
  const char* body = postBody;
  for (int hop = 0; hop < 5; ++hop) {
    char next[512];
    next[0] = 0;
    const Reply reply = oneRequest(cur, body, next, sizeof(next));
    body = nullptr;
    if (reply.redirect) {
      if (!next[0]) return -1;
      char resolved[512];
      if (!url_resolve(cur, next, resolved, sizeof(resolved))) return -1;
      snprintf(cur, sizeof(cur), "%s", resolved);
      continue;
    }
    return reply.body;
  }
  return -1;
}

}  // namespace

int fetch_page(const char* url, char** bufOut) { return fetchImpl(url, nullptr, bufOut); }

int fetch_page_post(const char* url, const char* postBody, char** bufOut) {
  return fetchImpl(url, postBody, bufOut);
}

void fetch_disconnect() { closeClient(); }
