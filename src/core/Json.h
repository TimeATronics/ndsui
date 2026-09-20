// Tiny JSON parser (objects, arrays, strings, numbers, bools, null) - just
// enough for the stock Emus/<system>/config.json files.
#pragma once

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ndsui {

struct JsonValue;
using JsonPtr = std::shared_ptr<JsonValue>;

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string str;
  std::vector<JsonPtr> arr;
  std::map<std::string, JsonPtr> obj;

  bool isObject() const { return type == Type::Object; }
  bool isArray() const { return type == Type::Array; }

  std::string getString(const std::string& key, const std::string& def = "") const {
    auto it = obj.find(key);
    if (it == obj.end() || !it->second || it->second->type != Type::String)
      return def;
    return it->second->str;
  }
  const JsonValue* get(const std::string& key) const {
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : it->second.get();
  }
};

// Parses the file; returns nullptr on failure.
JsonPtr jsonParseFile(const std::string& path);
JsonPtr jsonParse(const std::string& text);

}  // namespace ndsui
