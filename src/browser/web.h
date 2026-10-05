#pragma once

#include <stdint.h>

// Text browser. The HTML tokenizer is Babe32's; this file draws it on e-ink.

enum class WebAction : uint8_t {
  None,
  Shade,
  Close,
  HistBack,
  HistFwd,
  Address,
  Reload,
  Link,
  Field,
  Submit
};

struct WebHit {
  WebAction action = WebAction::None;
  int index = -1;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

enum class WebPaint : uint8_t { Page, Follow };

void webDraw(WebPaint paint = WebPaint::Page);
WebHit webHit(int x, int y);
bool webContentContains(int x, int y);

// Blocking fetch and parse. record adds the URL to back/forward history.
bool webLoad(const char* url, bool record);
bool webGoBack();
bool webGoForward();
void webReload();
const char* webCurrentUrl();
// Plain HTML search. No scripts, so the results are real links.
const char* webHomeUrl();
const char* webLinkUrl(int index);
const char* webFieldValue(int index);
void webSetField(int index, const char* text);
// Send the form that owns this submit button, then replace the page.
bool webSubmit(int index);

void webScrollBy(int dy);
