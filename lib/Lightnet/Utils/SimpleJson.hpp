#pragma once
// Thin, read-only helper for small, known-shape JSON objects (HTTP bodies and
// config files). Wraps the "find key then read value" pattern that would otherwise
// be copy-pasted into every handler and store.
//
// Not a full JSON parser. Suitable for flat or shallow objects where every field
// value fits in a scalar (string, integer) or is accessed via rawValue().
//
// Depth-aware key scan: skips over nested objects/arrays so it doesn't
// accidentally match a key that is inside a nested structure.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Lightnet {
// ============================================================================
// Free helpers (reusable without the class)
// ============================================================================

// Returns pointer to the value start (after `key:` + whitespace) of the first
// top-level occurrence of `key` in the JSON object body, or nullptr.
    inline const char * jsonFindKey(const char *body, size_t len, const char *key)
    {
        const char *end  = body + len;
        size_t klen = strlen(key);
        int depth = 0;

        for (const char *p = body; p + klen + 2 <= end; p++) {
            if (*p == '{' || *p == '[') {
                depth++;
                continue;
            }

            if (*p == '}' || *p == ']') {
                depth--;
                continue;
            }

            if (depth != 0 || *p != '"') continue;

            if (strncmp(p + 1, key, klen) != 0) continue;

            if (p[1 + klen] != '"') continue;

            const char *q = p + klen + 2;

            while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;

            if (q >= end || *q != ':') continue;

            q++;

            while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;

            return q;
        }

        return nullptr;
    }

// Parse a non-negative integer starting at p. Returns -1 if not a digit sequence.
    inline long jsonParseUInt(const char *p, const char *end)
    {
        if (!p || p >= end || *p < '0' || *p > '9') return -1;

        long v = 0;

        while (p < end && *p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;

            if (v > 65535) return -1;
        }

        return v;
    }

// Parse a quoted JSON string. Returns false if `p` isn't a `"`, value overflows,
// or there's no closing quote.
    inline bool jsonParseString(const char *p, const char *end, char *out, size_t outLen)
    {
        if (!p || p >= end || *p != '"') return false;

        p++;

        size_t i = 0;

        while (p < end && *p != '"' && i + 1 < outLen) {
            if (*p == '\\') {
                p++;

                if (p >= end) return false;
            }

            out[i++] = *p++;
        }

        if (p >= end || *p != '"') return false;

        out[i] = '\0';

        return true;
    }

// Parse a `#RRGGBB` hex colour into r/g/b bytes. Returns false on malformed input.
    inline bool jsonParseHexColor(const char *s, size_t len, uint8_t *r, uint8_t *g, uint8_t *b)
    {
        if (len != 7 || s[0] != '#') return false;

        auto h = [](char c) -> int {
                     if (c >= '0' && c <= '9') return c - '0';

                     if (c >= 'a' && c <= 'f') return c - 'a' + 10;

                     if (c >= 'A' && c <= 'F') return c - 'A' + 10;

                     return -1;
                 };
        int r1 = h(s[1]), r0 = h(s[2]), g1 = h(s[3]), g0 = h(s[4]), b1 = h(s[5]), b0 = h(s[6]);

        if (r1 < 0 || r0 < 0 || g1 < 0 || g0 < 0 || b1 < 0 || b0 < 0) return false;

        *r = (uint8_t)((r1 << 4) | r0);
        *g = (uint8_t)((g1 << 4) | g0);
        *b = (uint8_t)((b1 << 4) | b0);

        return true;
    }

// Format RGB as `#RRGGBB` into out (must be at least 8 bytes).
    inline void jsonFormatHex(uint8_t r, uint8_t g, uint8_t b, char *out)
    {
        static const char hx[] = "0123456789ABCDEF";

        out[0] = '#';
        out[1] = hx[r >> 4];
        out[2] = hx[r & 0xF];
        out[3] = hx[g >> 4];
        out[4] = hx[g & 0xF];
        out[5] = hx[b >> 4];
        out[6] = hx[b & 0xF];
        out[7] = '\0';
    }

// ============================================================================
// Cursor-based primitives — for streaming parsers (SceneParser, PaletteStore)
//
// Every function takes the cursor by reference and advances it past whatever
// it consumed. `jsonRead*` returns false on malformed input. Use these together
// with the iterators below to walk arbitrary JSON without hand-rolling the same
// whitespace / comma / quote bookkeeping in every parser.
//
// Difference from the by-value `jsonParseString` above: cursor `jsonReadString`
// truncates silently if `outLen` is too small but still advances past the
// closing quote (so the surrounding parser stays in sync). The by-value form
// rejects on overflow.
// ============================================================================

    inline void jsonSkipWs(const char *& p, const char *end)
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }

    inline bool jsonReadString(const char *& p, const char *end, char *out, size_t outLen)
    {
        jsonSkipWs(p, end);

        if (p >= end || *p != '"') return false;

        p++;

        size_t i = 0;

        while (p < end && *p != '"') {
            if (*p == '\\') {
                p++;

                if (p >= end) return false;
            }

            if (i + 1 < outLen) out[i++] = *p;

            p++;
        }

        if (p >= end) return false;

        out[i] = '\0';
        p++;

        return true;
    }

    inline bool jsonReadUInt(const char *& p, const char *end, long *out)
    {
        jsonSkipWs(p, end);

        if (p >= end || *p < '0' || *p > '9') return false;

        long v = 0;

        while (p < end && *p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;

            if (v > 65535) return false;
        }

        *out = v;

        return true;
    }

    inline bool jsonReadFloat(const char *& p, const char *end, float *out)
    {
        jsonSkipWs(p, end);

        if (p >= end) return false;

        bool negative = false;

        if (*p == '-') { negative = true; p++; }

        if (p >= end || *p < '0' || *p > '9') return false;

        long intPart = 0;

        while (p < end && *p >= '0' && *p <= '9') {
            intPart = intPart * 10 + (*p - '0');
            p++;
        }

        float frac = 0.0f;

        if (p < end && *p == '.') {
            p++;
            float divisor = 10.0f;

            while (p < end && *p >= '0' && *p <= '9') {
                frac += (float)(*p - '0') / divisor;
                divisor *= 10.0f;
                p++;
            }
        }

        *out = (float)intPart + frac;
        if (negative) *out = -*out;

        return true;
    }

    inline bool jsonReadBool(const char *& p, const char *end, bool *out)
    {
        jsonSkipWs(p, end);

        if (end - p >= 4 && strncmp(p, "true", 4) == 0) {
            *out = true;
            p += 4;

            return true;
        }

        if (end - p >= 5 && strncmp(p, "false", 5) == 0) {
            *out = false;
            p += 5;

            return true;
        }

        return false;
    }

// Skip any JSON value (string, number, bool, null, object, array).
    inline bool jsonSkipValue(const char *& p, const char *end);

    inline bool jsonSkipObject(const char *& p, const char *end)
    {
        // p is just past the opening '{'
        while (p < end) {
            jsonSkipWs(p, end);

            if (*p == '}') {
                p++;

                return true;
            }

            if (*p == ',') {
                p++;
                continue;
            }

            if (*p == '"') {
                p++;

                while (p < end && *p != '"') {
                    if (*p == '\\') p++;

                    p++;
                }

                if (p >= end) return false;

                p++;
            }

            jsonSkipWs(p, end);

            if (p >= end || *p != ':') return false;

            p++;

            if (!jsonSkipValue(p, end)) return false;
        }

        return false;
    }

    inline bool jsonSkipArray(const char *& p, const char *end)
    {
        while (p < end) {
            jsonSkipWs(p, end);

            if (*p == ']') {
                p++;

                return true;
            }

            if (*p == ',') {
                p++;
                continue;
            }

            if (!jsonSkipValue(p, end)) return false;
        }

        return false;
    }

    inline bool jsonSkipValue(const char *& p, const char *end)
    {
        jsonSkipWs(p, end);

        if (p >= end) return false;

        if (*p == '"') {
            p++;

            while (p < end && *p != '"') {
                if (*p == '\\') p++;

                p++;
            }

            if (p >= end) return false;

            p++;

            return true;
        }

        if (*p == '{') {
            p++;

            return jsonSkipObject(p, end);
        }

        if (*p == '[') {
            p++;

            return jsonSkipArray(p, end);
        }

        while (p < end && *p != ',' && *p != '}' && *p != ']' &&
               *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;

        return true;
    }

// ============================================================================
// Container iterators
//
// Pattern for objects:
//     if (!jsonEnterObject(p, end)) return false;
//     char key[N];
//     while (jsonNextKey(p, end, key, sizeof(key))) {
//         if      (strcmp(key, "foo") == 0) { ... read foo value ... }
//         else if (strcmp(key, "bar") == 0) { ... read bar value ... }
//         else jsonSkipValue(p, end);
//     }
//
// Pattern for arrays:
//     if (!jsonEnterArray(p, end)) return false;
//     while (jsonNextElement(p, end)) {
//         ... read element value ...   // p points at the element
//     }
//
// `jsonNextKey` returning true leaves p at the value start (past `:` + ws).
// `jsonNextElement` returning true leaves p at the element start.
// Both consume the closing `}`/`]` when they return false.
// ============================================================================

    inline bool jsonEnterObject(const char *& p, const char *end)
    {
        jsonSkipWs(p, end);

        if (p >= end || *p != '{') return false;

        p++;

        return true;
    }

    inline bool jsonEnterArray(const char *& p, const char *end)
    {
        jsonSkipWs(p, end);

        if (p >= end || *p != '[') return false;

        p++;

        return true;
    }

    inline bool jsonNextKey(const char *& p, const char *end, char *keyOut, size_t keyLen)
    {
        for (;;) {
            jsonSkipWs(p, end);

            if (p >= end) return false;

            if (*p == '}') {
                p++;

                return false;
            }

            if (*p == ',') {
                p++;
                continue;
            }

            break;
        }

        if (!jsonReadString(p, end, keyOut, keyLen)) return false;

        jsonSkipWs(p, end);

        if (p >= end || *p != ':') return false;

        p++;
        jsonSkipWs(p, end);

        return true;
    }

    inline bool jsonNextElement(const char *& p, const char *end)
    {
        for (;;) {
            jsonSkipWs(p, end);

            if (p >= end) return false;

            if (*p == ']') {
                p++;

                return false;
            }

            if (*p == ',') {
                p++;
                continue;
            }

            return true;
        }
    }

// ============================================================================
// SimpleJson — read accessor for a known-shape JSON object body
// ============================================================================

    class SimpleJson
    {
        public:
            SimpleJson(const char *body, size_t len) : _body(body), _len(len)
            {
            }

            SimpleJson(const uint8_t *body, size_t len) : _body((const char *)body), _len(len)
            {
            }

            // Return integer value of `key`, or -1 if missing / not a non-negative integer.
            long getInt(const char *key) const
            {
                return jsonParseUInt(jsonFindKey(_body, _len, key), _body + _len);
            }

            // Copy the string value of `key` into out. Returns false if missing or not a string.
            bool getString(const char *key, char *out, size_t outLen) const
            {
                return jsonParseString(jsonFindKey(_body, _len, key), _body + _len, out, outLen);
            }

            // Returns true if `key` is present.
            bool hasKey(const char *key) const
            {
                return jsonFindKey(_body, _len, key) != nullptr;
            }

            // Returns a pointer to the raw value start (for complex values: arrays, objects).
            // Returns nullptr if key is absent.
            const char * rawValue(const char *key) const
            {
                return jsonFindKey(_body, _len, key);
            }

            const char * end()  const
            {
                return _body + _len;
            }

            const char * body() const
            {
                return _body;
            }

            size_t      len()  const
            {
                return _len;
            }

        private:
            const char *_body;
            size_t _len;
    };
}  // namespace Lightnet
