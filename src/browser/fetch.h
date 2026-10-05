#pragma once
#include <stddef.h>

// Direct HTTPS/HTTP fetch. Babe32's proxy fallback is not used.
// Returns the body length, or -1. *buf_out points at a reused PSRAM buffer.
int fetch_page(const char* url, char** buf_out);
int fetch_page_post(const char* url, const char* post_body, char** buf_out);

void fetch_disconnect();
