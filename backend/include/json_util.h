// ============================================================================
//  json_util.h — Lightweight JSON Builder & Parser for BidMatrix
// ============================================================================
//  Hand-written JSON utility (no external dependency) providing:
//    • JsonObject — fluent builder for JSON objects
//    • JsonArray  — fluent builder for JSON arrays
//    • parseJson()— simple flat-object parser for POST body payloads
// ============================================================================

#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <cstdio>
#include <cstdlib>

// Forward declaration
class JsonArray;

// ╔═══════════════════════════════════════════════════════════════════════════╗
// ║  JsonObject — Fluent JSON Object Builder                                ║
// ╚═══════════════════════════════════════════════════════════════════════════╝
class JsonObject {
    struct Field {
        std::string key;
        std::string jsonValue;   // pre-formatted JSON value
    };
    std::vector<Field> fields;

public:
    // ── String value ──────────────────────────────────────────────────────
    JsonObject& set(const std::string& key, const std::string& val) {
        std::ostringstream ss;
        ss << "\"";
        for (size_t i = 0; i < val.size(); i++) {
            char c = val[i];
            if (c == '"')       ss << "\\\"";
            else if (c == '\\') ss << "\\\\";
            else if (c == '\n') ss << "\\n";
            else if (c == '\r') ss << "\\r";
            else if (c == '\t') ss << "\\t";
            else                ss << c;
        }
        ss << "\"";
        Field f; f.key = key; f.jsonValue = ss.str();
        fields.push_back(f);
        return *this;
    }

    JsonObject& set(const std::string& key, const char* val) {
        return set(key, std::string(val));
    }

    // ── Integer value ─────────────────────────────────────────────────────
    JsonObject& set(const std::string& key, int val) {
        Field f; f.key = key; f.jsonValue = std::to_string(val);
        fields.push_back(f);
        return *this;
    }

    // ── Long long value (for timestamps) ──────────────────────────────────
    JsonObject& set(const std::string& key, long long val) {
        Field f; f.key = key; f.jsonValue = std::to_string(val);
        fields.push_back(f);
        return *this;
    }

    // ── Double value (for money) ──────────────────────────────────────────
    JsonObject& set(const std::string& key, double val) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f", val);
        Field f; f.key = key; f.jsonValue = std::string(buf);
        fields.push_back(f);
        return *this;
    }

    // ── Boolean value ─────────────────────────────────────────────────────
    JsonObject& set(const std::string& key, bool val) {
        Field f; f.key = key; f.jsonValue = (val ? "true" : "false");
        fields.push_back(f);
        return *this;
    }

    // ── Raw JSON value (for nested objects / arrays) ──────────────────────
    JsonObject& setRaw(const std::string& key, const std::string& rawJson) {
        Field f; f.key = key; f.jsonValue = rawJson;
        fields.push_back(f);
        return *this;
    }

    // ── Serialize to JSON string ──────────────────────────────────────────
    std::string toString() const {
        std::ostringstream ss;
        ss << "{";
        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) ss << ",";
            ss << "\"" << fields[i].key << "\":" << fields[i].jsonValue;
        }
        ss << "}";
        return ss.str();
    }
};


// ╔═══════════════════════════════════════════════════════════════════════════╗
// ║  JsonArray — Fluent JSON Array Builder                                  ║
// ╚═══════════════════════════════════════════════════════════════════════════╝
class JsonArray {
    std::vector<std::string> items;   // pre-formatted JSON items

public:
    JsonArray& add(const JsonObject& obj) {
        items.push_back(obj.toString());
        return *this;
    }

    JsonArray& add(const std::string& val) {
        std::ostringstream ss;
        ss << "\"" << val << "\"";
        items.push_back(ss.str());
        return *this;
    }

    JsonArray& add(int val) {
        items.push_back(std::to_string(val));
        return *this;
    }

    JsonArray& addRaw(const std::string& rawJson) {
        items.push_back(rawJson);
        return *this;
    }

    std::string toString() const {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < items.size(); i++) {
            if (i > 0) ss << ",";
            ss << items[i];
        }
        ss << "]";
        return ss.str();
    }
};


// ╔═══════════════════════════════════════════════════════════════════════════╗
// ║  parseJson() — Simple flat JSON object parser                           ║
// ║  Handles: {"key":"str", "num":123, "dec":4.5, "flag":true}              ║
// ║  Returns all values as strings — caller converts as needed.             ║
// ╚═══════════════════════════════════════════════════════════════════════════╝
inline std::map<std::string, std::string> parseJson(const std::string& json) {
    std::map<std::string, std::string> result;
    size_t i = 0;
    size_t len = json.size();

    // Helper: skip whitespace
    auto skipWS = [&]() {
        while (i < len && (json[i] == ' ' || json[i] == '\t' ||
                           json[i] == '\n' || json[i] == '\r'))
            i++;
    };

    // Helper: parse a quoted string (advances i past closing quote)
    auto parseString = [&]() -> std::string {
        std::string s;
        i++;  // skip opening "
        while (i < len && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < len) {
                i++;
                if      (json[i] == '"')  s += '"';
                else if (json[i] == '\\') s += '\\';
                else if (json[i] == 'n')  s += '\n';
                else if (json[i] == 'r')  s += '\r';
                else if (json[i] == 't')  s += '\t';
                else                      s += json[i];
            } else {
                s += json[i];
            }
            i++;
        }
        if (i < len) i++;  // skip closing "
        return s;
    };

    // Helper: parse a non-string value token
    auto parseToken = [&]() -> std::string {
        std::string val;
        while (i < len && json[i] != ',' && json[i] != '}' &&
               json[i] != ' ' && json[i] != '\t' &&
               json[i] != '\n' && json[i] != '\r') {
            val += json[i];
            i++;
        }
        return val;
    };

    skipWS();
    if (i >= len || json[i] != '{') return result;
    i++;  // skip {

    while (i < len) {
        skipWS();
        if (i >= len || json[i] == '}') break;
        if (json[i] == ',') { i++; continue; }

        // Expect a quoted key
        if (json[i] != '"') break;
        std::string key = parseString();

        skipWS();
        if (i >= len || json[i] != ':') break;
        i++;  // skip :
        skipWS();

        // Parse value (string or token)
        std::string value;
        if (i < len && json[i] == '"')
            value = parseString();
        else
            value = parseToken();

        result[key] = value;
    }

    return result;
}

#endif // JSON_UTIL_H
