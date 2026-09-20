#include "core/Json.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace ndsui {

namespace {

struct Parser {
  const char* p;
  const char* end;

  void skipWs() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }
  bool eof() const { return p >= end; }

  JsonPtr parseValue() {
    skipWs();
    if (eof()) return nullptr;
    char c = *p;
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') {
      auto v = std::make_shared<JsonValue>();
      v->type = JsonValue::Type::String;
      if (!parseString(v->str)) return nullptr;
      return v;
    }
    if (c == 't' || c == 'f') return parseBool();
    if (c == 'n') {
      if (end - p >= 4 && strncmp(p, "null", 4) == 0) {
        p += 4;
        return std::make_shared<JsonValue>();
      }
      return nullptr;
    }
    return parseNumber();
  }

  JsonPtr parseObject() {
    auto v = std::make_shared<JsonValue>();
    v->type = JsonValue::Type::Object;
    ++p;  // {
    skipWs();
    if (p < end && *p == '}') {
      ++p;
      return v;
    }
    while (p < end) {
      skipWs();
      std::string key;
      if (!parseString(key)) return nullptr;
      skipWs();
      if (p >= end || *p != ':') return nullptr;
      ++p;
      JsonPtr val = parseValue();
      if (!val) return nullptr;
      v->obj[key] = val;
      skipWs();
      if (p < end && *p == ',') {
        ++p;
        continue;
      }
      if (p < end && *p == '}') {
        ++p;
        return v;
      }
      return nullptr;
    }
    return nullptr;
  }

  JsonPtr parseArray() {
    auto v = std::make_shared<JsonValue>();
    v->type = JsonValue::Type::Array;
    ++p;  // [
    skipWs();
    if (p < end && *p == ']') {
      ++p;
      return v;
    }
    while (p < end) {
      JsonPtr val = parseValue();
      if (!val) return nullptr;
      v->arr.push_back(val);
      skipWs();
      if (p < end && *p == ',') {
        ++p;
        continue;
      }
      if (p < end && *p == ']') {
        ++p;
        return v;
      }
      return nullptr;
    }
    return nullptr;
  }

  bool parseString(std::string& out) {
    if (p >= end || *p != '"') return false;
    ++p;
    out.clear();
    while (p < end) {
      char c = *p++;
      if (c == '"') return true;
      if (c == '\\') {
        if (p >= end) return false;
        char e = *p++;
        switch (e) {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'u': {
            // decode BMP escapes to UTF-8
            if (end - p < 4) return false;
            unsigned cp = 0;
            for (int i = 0; i < 4; ++i) {
              char h = *p++;
              cp <<= 4;
              if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
              else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
              else return false;
            }
            if (cp < 0x80) {
              out += (char)cp;
            } else if (cp < 0x800) {
              out += (char)(0xC0 | (cp >> 6));
              out += (char)(0x80 | (cp & 0x3F));
            } else {
              out += (char)(0xE0 | (cp >> 12));
              out += (char)(0x80 | ((cp >> 6) & 0x3F));
              out += (char)(0x80 | (cp & 0x3F));
            }
            break;
          }
          default: out += e; break;
        }
      } else {
        out += c;
      }
    }
    return false;
  }

  JsonPtr parseBool() {
    if (end - p >= 4 && strncmp(p, "true", 4) == 0) {
      p += 4;
      auto v = std::make_shared<JsonValue>();
      v->type = JsonValue::Type::Bool;
      v->boolean = true;
      return v;
    }
    if (end - p >= 5 && strncmp(p, "false", 5) == 0) {
      p += 5;
      auto v = std::make_shared<JsonValue>();
      v->type = JsonValue::Type::Bool;
      v->boolean = false;
      return v;
    }
    return nullptr;
  }

  JsonPtr parseNumber() {
    char* endp = nullptr;
    double d = strtod(p, &endp);
    if (endp == p) return nullptr;
    p = endp;
    auto v = std::make_shared<JsonValue>();
    v->type = JsonValue::Type::Number;
    v->number = d;
    return v;
  }
};

}  // namespace

JsonPtr jsonParse(const std::string& text) {
  Parser parser{text.c_str(), text.c_str() + text.size()};
  JsonPtr v = parser.parseValue();
  return v;
}

JsonPtr jsonParseFile(const std::string& path) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) return nullptr;
  std::string text;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, n);
  fclose(fp);
  return jsonParse(text);
}

}  // namespace ndsui
