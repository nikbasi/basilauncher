/*
 * E-ink page view for Babe32's HTML tokenizer.
 * Text, headings, and links. No images, script, or layout engine.
 */
#include "web.h"

#include "canvas.h"
#include "fetch.h"
#include "history.h"
#include "html_parser.h"
#include "url_utils.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kPad = 16;
constexpr int kBtnH = 48;

ParseResult* gPage = nullptr;
bool gHistory = false;
int gScroll = 0;
int gDocH = 0;
char gUrl[512] = "https://html.duckduckgo.com/html/";
char gStatus[64] = "Tap URL, then Done";

struct LinkBox {
  int x, y, w, h;
  char href[180];
};
LinkBox gLinks[48];
int gLinkN = 0;

struct CtrlBox {
  int elem;
  int x, y, w, h;
  bool submit;
};
CtrlBox gCtrls[12];
int gCtrlN = 0;

struct FieldEdit {
  int index;
  char value[240];
};
FieldEdit gEdits[6];
int gEditN = 0;

const char* editedValue(int index) {
  for (int i = 0; i < gEditN; ++i) {
    if (gEdits[i].index == index) return gEdits[i].value;
  }
  return nullptr;
}

void clearEdits() { gEditN = 0; }

bool shortLink(const PageElement& el) {
  if (el.type != ELEM_LINK || !el.text || !el.text[0] || !el.href || !el.href[0]) return false;
  if (strncmp(el.href, "javascript:", 11) == 0) return false;
  return strlen(el.text) <= 18;
}

int gContentTop = 0;
int gContentBot = 0;
int gDockY = 0;
int gCloseX = 0;
int gBackX = 0;
int gFwdX = 0;
int gUrlX = 0;
int gUrlW = 0;
int gReloadX = 0;

void addLink(int x, int y, int w, int h, const char* href) {
  if (!href || !href[0] || gLinkN >= static_cast<int>(sizeof(gLinks) / sizeof(gLinks[0]))) return;
  if (strncmp(href, "javascript:", 11) == 0 || href[0] == '#') return;
  LinkBox& box = gLinks[gLinkN++];
  box.x = x;
  box.y = y;
  box.w = w;
  box.h = h;
  snprintf(box.href, sizeof(box.href), "%s", href);
}

void drawLine(const char* text, int scale, bool bold, bool link, const char* href, int docY) {
  const int lineH = canvasTextHeight(scale) + 6;
  const int screenY = gContentTop + (docY - gScroll);
  if (screenY + lineH > gContentTop && screenY < gContentBot && text && text[0]) {
    canvasDrawString(kPad, screenY, text, true, scale, bold);
    const int tw = canvasTextWidth(text, scale);
    if (link) {
      canvasDrawLine(kPad, screenY + canvasTextHeight(scale) + 1, kPad + tw, screenY + canvasTextHeight(scale) + 1,
                     true);
      addLink(kPad, screenY, tw, lineH, href);
    }
  }
}

int wrapText(const char* text, int scale, bool bold, bool link, const char* href, int docY) {
  if (!text || !text[0]) return docY;
  const int maxW = kScreenW - 2 * kPad;
  const int lineH = canvasTextHeight(scale) + 6;
  char line[96];
  size_t used = 0;
  line[0] = 0;
  const char* p = text;
  while (*p) {
    while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') ++p;
    if (!*p) break;
    const char* word = p;
    while (*p && *p != ' ' && *p != '\n' && *p != '\t' && *p != '\r') ++p;
    const size_t wordLen = static_cast<size_t>(p - word);
    char trial[96];
    if (used == 0) {
      const size_t n = wordLen < sizeof(line) - 1 ? wordLen : sizeof(line) - 1;
      memcpy(line, word, n);
      line[n] = 0;
      used = n;
    } else {
      snprintf(trial, sizeof(trial), "%s ", line);
      const size_t base = strlen(trial);
      const size_t n = wordLen < sizeof(trial) - base - 1 ? wordLen : sizeof(trial) - base - 1;
      memcpy(trial + base, word, n);
      trial[base + n] = 0;
      if (canvasTextWidth(trial, scale) <= maxW) {
        snprintf(line, sizeof(line), "%s", trial);
        used = strlen(line);
      } else {
        drawLine(line, scale, bold, link, href, docY);
        docY += lineH;
        const size_t n = wordLen < sizeof(line) - 1 ? wordLen : sizeof(line) - 1;
        memcpy(line, word, n);
        line[n] = 0;
        used = n;
      }
    }
  }
  if (used) {
    drawLine(line, scale, bold, link, href, docY);
    docY += lineH;
  }
  return docY;
}

bool normalizeUrl(const char* in, char* out, size_t outLen) {
  if (!in || !in[0] || !out || outLen < 12) return false;
  while (*in == ' ') ++in;
  if (strncmp(in, "https://", 8) == 0 || strncmp(in, "http://", 7) == 0) {
    snprintf(out, outLen, "%s", in);
    return true;
  }
  snprintf(out, outLen, "https://%s", in);
  return true;
}

}  // namespace

const char* webHomeUrl() { return "https://html.duckduckgo.com/html/"; }

const char* webCurrentUrl() { return gUrl; }

const char* webLinkUrl(int index) {
  if (index < 0 || index >= gLinkN) return nullptr;
  return gLinks[index].href;
}

bool webContentContains(int x, int y) {
  (void)x;
  return y >= gContentTop && y < gContentBot;
}

void webScrollBy(int dy) {
  gScroll -= dy;
  const int viewH = gContentBot - gContentTop;
  const int maxScroll = gDocH > viewH ? gDocH - viewH : 0;
  if (gScroll < 0) gScroll = 0;
  if (gScroll > maxScroll) gScroll = maxScroll;
}

bool googleHost(const char* host) {
  if (strncmp(host, "www.", 4) == 0) host += 4;
  if (strncmp(host, "google.", 7) != 0) return false;
  const char* rest = host + 7;
  const size_t n = strlen(rest);
  if (strcmp(rest, "com") == 0) return true;
  if (n == 6 && strncmp(rest, "com.", 4) == 0) return true;
  if (n == 5 && strncmp(rest, "co.", 3) == 0) return true;
  return n == 2 && isalpha(static_cast<unsigned char>(rest[0])) && isalpha(static_cast<unsigned char>(rest[1]));
}

// Google's scripted results page has no text this tokenizer can keep.
// gbv=1 is the basic HTML results page.
void preferReadableGoogle(char* url, size_t cap) {
  const char* scheme = strstr(url, "://");
  if (!scheme) return;
  const char* host = scheme + 3;
  const char* slash = strchr(host, '/');
  const char* hostEnd = slash ? slash : host + strlen(host);
  for (const char* p = host; p < hostEnd; ++p) {
    if (*p == ':') {
      hostEnd = p;
      break;
    }
  }
  char hostBuf[80];
  const size_t hostLen = static_cast<size_t>(hostEnd - host);
  if (hostLen == 0 || hostLen >= sizeof(hostBuf)) return;
  memcpy(hostBuf, host, hostLen);
  hostBuf[hostLen] = 0;
  if (!googleHost(hostBuf)) return;

  const char* path = slash ? slash : "/";
  const char* query = strchr(path, '?');
  const char* hash = strchr(path, '#');
  const char* pathEnd = query ? query : (hash ? hash : path + strlen(path));
  const size_t pathLen = static_cast<size_t>(pathEnd - path);
  const bool home = pathLen == 0 || (pathLen == 1 && path[0] == '/');
  const bool search = pathLen == 7 && strncmp(path, "/search", 7) == 0;
  const bool webhp = pathLen == 6 && strncmp(path, "/webhp", 6) == 0;
  if (!home && !search && !webhp) return;
  if (query && strstr(query, "gbv=")) return;

  char frag[48];
  frag[0] = 0;
  if (hash) {
    snprintf(frag, sizeof(frag), "%s", hash);
    url[hash - url] = 0;
  }
  const size_t n = strlen(url);
  const char* join = strchr(url, '?') ? "&gbv=1" : "?gbv=1";
  if (n + strlen(join) + strlen(frag) >= cap) {
    if (frag[0]) snprintf(url + n, cap - n, "%s", frag);
    return;
  }
  snprintf(url + n, cap - n, "%s%s", join, frag);
}

bool webLoad(const char* url, bool record) {
  char abs[512];
  if (!normalizeUrl(url, abs, sizeof(abs))) {
    snprintf(gStatus, sizeof(gStatus), "Type a web address");
    return false;
  }
  preferReadableGoogle(abs, sizeof(abs));
  char* body = nullptr;
  const int n = fetch_page(abs, &body);
  if (n < 0 || !body) {
    snprintf(gStatus, sizeof(gStatus), "Could not open page");
    return false;
  }
  if (!gPage) gPage = parse_result_alloc();
  if (!gPage) {
    snprintf(gStatus, sizeof(gStatus), "Out of memory");
    return false;
  }
  memset(gPage, 0, sizeof(*gPage));
  clearEdits();
  html_parse(body, abs, gPage, n + 64 >= static_cast<int>(1024 * 1024));
  if (!gHistory) {
    history_init();
    gHistory = true;
  }
  if (record) history_push(abs);
  snprintf(gUrl, sizeof(gUrl), "%s", abs);
  gScroll = 0;
  if (gPage->count <= 0) snprintf(gStatus, sizeof(gStatus), "No readable text");
  else gStatus[0] = 0;
  return true;
}

const char* webFieldValue(int index) {
  if (!gPage || index < 0 || index >= gPage->count) return "";
  if (const char* edited = editedValue(index)) return edited;
  const char* value = gPage->elems[index].value;
  return value ? value : "";
}

void webSetField(int index, const char* text) {
  if (!gPage || index < 0 || index >= gPage->count) return;
  for (int i = 0; i < gEditN; ++i) {
    if (gEdits[i].index == index) {
      snprintf(gEdits[i].value, sizeof(gEdits[i].value), "%s", text ? text : "");
      return;
    }
  }
  if (gEditN >= static_cast<int>(sizeof(gEdits) / sizeof(gEdits[0]))) return;
  gEdits[gEditN].index = index;
  snprintf(gEdits[gEditN].value, sizeof(gEdits[gEditN].value), "%s", text ? text : "");
  gEditN++;
}

bool appendQuery(char* query, size_t cap, const char* name, const char* value) {
  if (!name || !name[0]) return true;
  char encName[120];
  char encValue[300];
  url_encode(name, encName, sizeof(encName));
  url_encode(value ? value : "", encValue, sizeof(encValue));
  const size_t used = strlen(query);
  const size_t need = strlen(encName) + strlen(encValue) + (used ? 1 : 0) + 1;
  if (used + need >= cap) return false;
  if (used) strcat(query, "&");
  strcat(query, encName);
  strcat(query, "=");
  strcat(query, encValue);
  return true;
}

bool webSubmit(int index) {
  if (!gPage || index < 0 || index >= gPage->count) return false;
  const PageElement& button = gPage->elems[index];
  if (button.type != ELEM_SUBMIT || gPage->form_count <= 0) return false;
  int form = button.form_id;
  if (form < 0 || form >= gPage->form_count) form = 0;
  const FormInfo& info = gPage->forms[form];
  if (!info.action[0]) return false;

  char query[900];
  query[0] = 0;
  for (int i = 0; i < gPage->count; ++i) {
    const PageElement& el = gPage->elems[i];
    if (el.form_id != form) continue;
    if (el.type == ELEM_HIDDEN) {
      if (!el.value || strlen(el.value) > 48) continue;
      appendQuery(query, sizeof(query), el.name, el.value);
    } else if (el.type == ELEM_INPUT) {
      appendQuery(query, sizeof(query), el.name, webFieldValue(i));
    }
  }
  appendQuery(query, sizeof(query), button.name, button.value);

  if (info.is_post) {
    char* body = nullptr;
    const int n = fetch_page_post(info.action, query, &body);
    if (n < 0 || !body) {
      snprintf(gStatus, sizeof(gStatus), "Could not send the form");
      return false;
    }
    if (!gPage) gPage = parse_result_alloc();
    if (!gPage) return false;
    memset(gPage, 0, sizeof(*gPage));
    clearEdits();
    html_parse(body, info.action, gPage, false);
    snprintf(gUrl, sizeof(gUrl), "%s", info.action);
    gScroll = 0;
    gStatus[0] = 0;
    if (!gHistory) {
      history_init();
      gHistory = true;
    }
    history_push(info.action);
    return true;
  }

  char url[512];
  const char join = strchr(info.action, '?') ? '&' : '?';
  const int wrote = snprintf(url, sizeof(url), "%s%c%s", info.action, join, query);
  if (wrote < 0 || wrote >= static_cast<int>(sizeof(url))) {
    query[0] = 0;
    for (int i = 0; i < gPage->count; ++i) {
      const PageElement& el = gPage->elems[i];
      if (el.form_id == form && el.type == ELEM_INPUT)
        appendQuery(query, sizeof(query), el.name, webFieldValue(i));
    }
    snprintf(url, sizeof(url), "%s%c%s", info.action, join, query);
  }
  return webLoad(url, true);
}

bool webGoBack() {
  if (!history_can_back()) return false;
  const char* url = history_back();
  return url && webLoad(url, false);
}

bool webGoForward() {
  if (!history_can_forward()) return false;
  const char* url = history_forward();
  return url && webLoad(url, false);
}

void webReload() { webLoad(gUrl, false); }

void addCtrl(int elem, int x, int y, int w, int h, bool submit) {
  if (gCtrlN >= static_cast<int>(sizeof(gCtrls) / sizeof(gCtrls[0]))) return;
  if (y + h <= gContentTop || y >= gContentBot) return;
  CtrlBox& box = gCtrls[gCtrlN++];
  box.elem = elem;
  box.x = x;
  box.y = y;
  box.w = w;
  box.h = h;
  box.submit = submit;
}

void drawChip(const char* text, const char* href, int x, int screenY, int w, int h) {
  canvasDrawRoundRect(x, screenY, w, h, 8, true);
  canvasDrawString(x + 8, screenY + (h - canvasTextHeight(1)) / 2, text, true, 1);
  addLink(x, screenY, w, h, href);
}

void webDraw(WebPaint paint) {
  canvasClear();
  gLinkN = 0;
  gCtrlN = 0;
  int y = 8;
  const int maxW = kScreenW - 2 * kPad;
  const char* shown = gUrl;
  while (*shown && canvasTextWidth(shown, 1) > maxW) ++shown;
  canvasDrawString(kPad, y, shown[0] ? shown : "Web", true, 1, true);
  y += canvasTextHeight(1) + 2;
  if (gStatus[0]) {
    canvasDrawString(kPad, y, gStatus, true, 1);
    y += canvasTextHeight(1) + 2;
  }
  canvasDrawLine(0, y, kScreenW - 1, y, true);
  gContentTop = y + 8;

  gDockY = kScreenH - 12 - kBtnH;
  gContentBot = gDockY - 8;

  int docY = 0;
  bool prevBreak = false;
  if (gPage) {
    const int lineH = canvasTextHeight(1) + 4;
    const int chipH = canvasTextHeight(1) + 16;
    for (int i = 0; i < gPage->count;) {
      const PageElement& el = gPage->elems[i];
      if (el.type == ELEM_HIDDEN || el.type == ELEM_IMAGE || el.type == ELEM_SELECT) {
        ++i;
        continue;
      }
      if (el.type == ELEM_LINEBREAK) {
        if (!prevBreak) docY += lineH / 2;
        prevBreak = true;
        ++i;
        continue;
      }
      if (el.type == ELEM_HR) {
        const int screenY = gContentTop + (docY - gScroll);
        if (screenY >= gContentTop && screenY < gContentBot) {
          canvasDrawLine(kPad, screenY + 6, kScreenW - kPad, screenY + 6, true);
        }
        docY += 16;
        prevBreak = false;
        ++i;
        continue;
      }
      if (el.type == ELEM_INPUT || el.type == ELEM_SUBMIT) {
        const int boxH = 48;
        const int screenY = gContentTop + (docY - gScroll);
        const int boxW = kScreenW - 2 * kPad;
        if (screenY + boxH > gContentTop && screenY < gContentBot) {
          const bool submit = el.type == ELEM_SUBMIT;
          if (submit) canvasFillRoundRect(kPad, screenY, boxW, boxH, 10, true);
          else canvasDrawRoundRect(kPad, screenY, boxW, boxH, 10, true);
          const char* label = submit ? (el.text && el.text[0] ? el.text : "Submit") : webFieldValue(i);
          if (!submit && (!label || !label[0])) label = (el.text && el.text[0]) ? el.text : "Search";
          char shownLabel[48];
          snprintf(shownLabel, sizeof(shownLabel), "%s", label);
          while (shownLabel[0] && canvasTextWidth(shownLabel, 1) > boxW - 24) {
            shownLabel[strlen(shownLabel) - 1] = 0;
          }
          const int textY = screenY + (boxH - canvasTextHeight(1)) / 2;
          canvasDrawString(kPad + 12, textY, shownLabel, !submit, 1, true);
          addCtrl(i, kPad, screenY, boxW, boxH, submit);
        }
        docY += boxH + 8;
        prevBreak = false;
        ++i;
        continue;
      }
      if (shortLink(el)) {
        int x = kPad;
        const int rowY = docY;
        while (i < gPage->count && shortLink(gPage->elems[i])) {
          const PageElement& link = gPage->elems[i];
          int chipW = canvasTextWidth(link.text, 1) + 16;
          if (chipW < 44) chipW = 44;
          if (chipW > maxW) chipW = maxW;
          if (x > kPad && x + chipW > kPad + maxW) break;
          const int screenY = gContentTop + (rowY - gScroll);
          if (screenY + chipH > gContentTop && screenY < gContentBot) {
            drawChip(link.text, link.href, x, screenY, chipW, chipH);
          }
          x += chipW + 8;
          ++i;
        }
        docY += chipH + 8;
        prevBreak = false;
        continue;
      }
      if (el.type == ELEM_LINK && el.href && strncmp(el.href, "javascript:", 11) == 0) {
        ++i;
        continue;
      }
      const bool heading = el.type == ELEM_HEADING;
      const bool link = el.type == ELEM_LINK && el.href && el.href[0];
      const int scale = heading && el.level <= 2 ? 2 : 1;
      if (heading) docY += 8;
      docY = wrapText(el.text, scale, heading || el.bold, link, el.href, docY);
      if (heading || (el.text && strlen(el.text) > 40)) docY += 8;
      else docY += 2;
      prevBreak = false;
      ++i;
    }
  } else {
    docY = wrapText("Join Wi-Fi, tap URL, and open a page.", 1, false, false, nullptr, 0);
  }
  gDocH = docY;

  const int viewH = gContentBot - gContentTop;
  const int maxScroll = gDocH > viewH ? gDocH - viewH : 0;
  if (gScroll > maxScroll) gScroll = maxScroll;

  gCloseX = kPad;
  gBackX = gCloseX + 88 + 8;
  gFwdX = gBackX + 48 + 8;
  gReloadX = kScreenW - kPad - 96;
  gUrlX = gFwdX + 48 + 8;
  gUrlW = gReloadX - 8 - gUrlX;
  auto btn = [](int x, int w, const char* label, bool filled) {
    if (filled) canvasFillRoundRect(x, gDockY, w, kBtnH, 12, true);
    else canvasDrawRoundRect(x, gDockY, w, kBtnH, 12, true);
    const int tw = canvasTextWidth(label, 1);
    canvasDrawString(x + (w - tw) / 2, gDockY + (kBtnH - canvasTextHeight(1)) / 2, label, !filled, 1, true);
  };
  btn(gCloseX, 88, "Close", false);
  btn(gBackX, 48, "<", false);
  btn(gFwdX, 48, ">", false);
  btn(gUrlX, gUrlW, "URL", true);
  btn(gReloadX, 96, "Reload", false);
  const int contentH = gContentBot > gContentTop ? gContentBot - gContentTop : 1;
  const CanvasRect content{0, gContentTop, kScreenW, contentH};
  if (paint == WebPaint::Follow) {
    canvasPresentWindowFast(content);
    canvasArmFullClean(600);
  } else {
    canvasPresentFor(CanvasRefreshIntent::Navigation);
  }
}

WebHit webHit(int x, int y) {
  WebHit hit;
  if (y < 28) {
    hit.action = WebAction::Shade;
    return hit;
  }
  auto take = [&](WebAction action, int bx, int bw) {
    if (y < gDockY || y >= gDockY + kBtnH || x < bx || x >= bx + bw) return false;
    hit.action = action;
    hit.x = bx;
    hit.y = gDockY;
    hit.w = bw;
    hit.h = kBtnH;
    return true;
  };
  if (take(WebAction::Close, gCloseX, 88)) return hit;
  if (take(WebAction::HistBack, gBackX, 48)) return hit;
  if (take(WebAction::HistFwd, gFwdX, 48)) return hit;
  if (take(WebAction::Address, gUrlX, gUrlW)) return hit;
  if (take(WebAction::Reload, gReloadX, 96)) return hit;
  for (int i = gCtrlN - 1; i >= 0; --i) {
    const CtrlBox& box = gCtrls[i];
    if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h) {
      hit.action = box.submit ? WebAction::Submit : WebAction::Field;
      hit.index = box.elem;
      hit.x = box.x;
      hit.y = box.y;
      hit.w = box.w;
      hit.h = box.h;
      return hit;
    }
  }
  for (int i = gLinkN - 1; i >= 0; --i) {
    const LinkBox& box = gLinks[i];
    if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h) {
      hit.action = WebAction::Link;
      hit.index = i;
      hit.x = box.x;
      hit.y = box.y;
      hit.w = box.w;
      hit.h = box.h;
      return hit;
    }
  }
  return hit;
}
