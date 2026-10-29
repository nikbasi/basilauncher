#include "app_names.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

bool isBoardTag(const std::string& token) {
  static const char* kTags[] = {"t5s3",  "t5",    "s3",     "esp32s3", "esp32-s3", "esp32",
                                "lilygo", "t5pro", "epaper", "firmware", "fw",      "pro"};
  std::string lower;
  for (char c : token) lower.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
  for (const char* tag : kTags) {
    if (lower == tag) return true;
  }
  return false;
}

// "1.5.33", "v1.2", "2.6", "1.0.0rc2". A bare number only counts with a dot,
// so "Reader-2026" keeps its year in the title.
bool isVersionToken(const std::string& token, std::string& out) {
  size_t i = 0;
  if (i < token.size() && (token[i] == 'v' || token[i] == 'V')) ++i;
  if (i >= token.size() || !isdigit(static_cast<unsigned char>(token[i]))) return false;
  bool dot = false;
  for (size_t k = i; k < token.size(); ++k) {
    const char c = token[k];
    if (c == '.') dot = true;
    else if (!isalnum(static_cast<unsigned char>(c))) return false;
  }
  if (!dot && i == 0) return false;
  out = token.substr(i);
  return true;
}

std::string capitalise(std::string s) {
  if (!s.empty()) s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
  return s;
}

}  // namespace

void appsTitleFromFileName(const char* fileName, std::string& title, std::string& version) {
  title.clear();
  version.clear();
  if (!fileName) return;
  std::string base = fileName;
  const size_t slash = base.find_last_of('/');
  if (slash != std::string::npos) base = base.substr(slash + 1);
  if (base.size() > 4 && strcasecmp(base.c_str() + base.size() - 4, ".bin") == 0) {
    base.resize(base.size() - 4);
  }

  std::vector<std::string> tokens;
  std::string cur;
  for (char c : base) {
    if (c == '-' || c == '_' || c == ' ') {
      if (!cur.empty()) tokens.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) tokens.push_back(cur);

  size_t versionAt = tokens.size();
  for (size_t i = 0; i < tokens.size(); ++i) {
    std::string v;
    if (isVersionToken(tokens[i], v)) {
      version = v;
      versionAt = i;
      break;
    }
  }

  std::vector<std::string> words;
  for (size_t i = 0; i < versionAt; ++i) {
    if (!isBoardTag(tokens[i])) words.push_back(tokens[i]);
  }
  if (words.empty()) {
    for (size_t i = 0; i < versionAt; ++i) words.push_back(tokens[i]);
  }
  if (words.empty()) {
    title = base;
    return;
  }
  for (size_t i = 0; i < words.size(); ++i) {
    if (i) title.push_back(' ');
    title += i == 0 ? capitalise(words[i]) : words[i];
  }
}

bool appsDescNameUseful(const char* projectName) {
  if (!projectName || !projectName[0]) return false;
  static const char* kGeneric[] = {"arduino-lib-builder", "app-template", "main", "esp-idf", "app"};
  for (const char* g : kGeneric) {
    if (strcmp(projectName, g) == 0) return false;
  }
  return true;
}

bool appsDescVersionUseful(const char* version) {
  if (!version || !version[0]) return false;
  // Builds without a version report the IDF's git describe (v5.3.2-...) or "1".
  if (strncmp(version, "esp-idf", 7) == 0) return false;
  if (strcmp(version, "1") == 0) return false;
  if (version[0] == 'v' && strchr(version, '-')) return false;
  return isdigit(static_cast<unsigned char>(version[0])) ||
         (version[0] == 'v' && isdigit(static_cast<unsigned char>(version[1])));
}

std::string appsPrettyName(const char* projectName) {
  std::string out;
  if (!projectName) return out;
  for (const char* p = projectName; *p; ++p) out.push_back(*p == '-' || *p == '_' ? ' ' : *p);
  return capitalise(out);
}

std::string appsMatchKey(const std::string& title) {
  std::string key;
  for (char c : title) {
    if (isalnum(static_cast<unsigned char>(c))) key.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
  }
  return key;
}

int appsCompareVersions(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return 0;
  const char* pa = a.c_str();
  const char* pb = b.c_str();
  if (*pa == 'v' || *pa == 'V') ++pa;
  if (*pb == 'v' || *pb == 'V') ++pb;
  while (*pa || *pb) {
    char* ea = nullptr;
    char* eb = nullptr;
    const long na = strtol(pa, &ea, 10);
    const long nb = strtol(pb, &eb, 10);
    if (na != nb) return na < nb ? -1 : 1;
    pa = ea;
    pb = eb;
    // A letter suffix marks a pre-release: 1.0.0rc2 comes before 1.0.0.
    const bool preA = isalpha(static_cast<unsigned char>(*pa));
    const bool preB = isalpha(static_cast<unsigned char>(*pb));
    if (preA != preB) return preA ? -1 : 1;
    // Skip to the next numeric component.
    while (*pa && !isdigit(static_cast<unsigned char>(*pa))) ++pa;
    while (*pb && !isdigit(static_cast<unsigned char>(*pb))) ++pb;
  }
  return 0;
}
