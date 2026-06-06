#pragma once

// ============================================================================
// PanelSelectorParser — parse a scene layer's "panels" JSON value into a
// PanelSelector RPN program. Pure C++ (SimpleJson + PanelSelector only) → it is
// unit-testable natively and is delegated to by the Arduino-bound SceneParser.
//
// Grammar (docs/design/scene-portability.md §3.1):
//   value := indexArray | "token" | object
//   indexArray := [ uint, … ]                       → INDICES
//   token  := all|root|leaves|branches|even|odd
//           | depth:N | depth:A-B | subtree:N | neighbors:N
//           | fraction:A-B | first:K | last:K | tag:NAME
//   object := {"any":[value,…]} | {"all":[value,…]}
//           | {"not":value} | {"exclude":[uint,…]}
// ============================================================================

#include <stdint.h>
#include <string.h>
#include "PanelSelector.hpp"
#include "../../Utils/SimpleJson.hpp"

namespace Lightnet {
    inline bool selErr(char *e, size_t n, const char *m)
    {
        if (e && n) {
            strncpy(e, m, n);
            e[n - 1] = '\0';
        }

        return false;
    }

    // Parse a small unsigned int from [s,e), advancing s. False if no digits.
    inline bool selTokUInt(const char *& s, const char *e, long& v)
    {
        if (s >= e || *s < '0' || *s > '9') return false;

        v = 0;

        while (s < e && *s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            s++;

            if (v > 65535) return false;
        }

        return true;
    }

    // Parse a non-negative decimal float from [s,e), advancing s. False if none.
    inline bool selTokFloat(const char *& s, const char *e, float& f)
    {
        if (s >= e || *s < '0' || *s > '9') return false;

        long ip = 0;

        while (s < e && *s >= '0' && *s <= '9') {
            ip = ip * 10 + (*s - '0');
            s++;
        }

        float fr = 0.0f, dv = 10.0f;

        if (s < e && *s == '.') {
            s++;

            while (s < e && *s >= '0' && *s <= '9') {
                fr += (float)(*s - '0') / dv;
                dv *= 10.0f;
                s++;
            }
        }

        f = (float)ip + fr;

        return true;
    }

    // Interpret a string token (mutated in place to split name:arg) and emit its ops.
    inline bool selEmitToken(char *tok, PanelSelector& out, char *err, size_t errLen)
    {
        char *colon = strchr(tok, ':');
        char *arg   = nullptr;

        if (colon) {
            *colon = '\0';
            arg    = colon + 1;
        }

        if (!arg) {
            if (!strcmp(tok, "all"))      return out.emit(SEL_ALL) || selErr(err, errLen, "panels: too complex");

            if (!strcmp(tok, "root"))     return out.emit(SEL_ROOT) || selErr(err, errLen, "panels: too complex");

            if (!strcmp(tok, "leaves"))   return out.emit(SEL_LEAVES) || selErr(err, errLen, "panels: too complex");

            if (!strcmp(tok, "branches")) return out.emit(SEL_BRANCHES) || selErr(err, errLen, "panels: too complex");

            if (!strcmp(tok, "even"))     return out.emit(SEL_EVEN) || selErr(err, errLen, "panels: too complex");

            if (!strcmp(tok, "odd"))      return out.emit(SEL_ODD) || selErr(err, errLen, "panels: too complex");

            return selErr(err, errLen, "panels: unknown selector");
        }

        const char *a  = arg;
        const char *ae = arg + strlen(arg);

        if (!strcmp(tok, "depth")) {
            long lo;

            if (!selTokUInt(a, ae, lo)) return selErr(err, errLen, "depth: bad range");

            long hi = lo;

            if (a < ae && *a == '-') {
                a++;

                if (!selTokUInt(a, ae, hi)) return selErr(err, errLen, "depth: bad range");
            }

            if (lo > 255 || hi > 255) return selErr(err, errLen, "depth: out of range");

            return out.emit(SEL_DEPTH) && out.emit((uint8_t)lo) && out.emit((uint8_t)hi);
        }

        if (!strcmp(tok, "subtree") || !strcmp(tok, "neighbors")) {
            long v;

            if (!selTokUInt(a, ae, v) || v == 0 || v > 255) return selErr(err, errLen, "subtree/neighbors: bad index");

            return out.emit(!strcmp(tok, "subtree") ? SEL_SUBTREE : SEL_NEIGHBORS) && out.emit((uint8_t)v);
        }

        if (!strcmp(tok, "first") || !strcmp(tok, "last")) {
            long k;

            if (!selTokUInt(a, ae, k) || k == 0 || k > 255) return selErr(err, errLen, "first/last: bad count");

            return out.emit(!strcmp(tok, "first") ? SEL_FIRST : SEL_LAST) && out.emit((uint8_t)k);
        }

        if (!strcmp(tok, "fraction")) {
            float lo, hi;

            if (!selTokFloat(a, ae, lo))          return selErr(err, errLen, "fraction: bad range");

            if (a >= ae || *a != '-')             return selErr(err, errLen, "fraction: expected a-b");

            a++;

            if (!selTokFloat(a, ae, hi))          return selErr(err, errLen, "fraction: bad range");

            if (lo < 0.0f) lo = 0.0f;

            if (hi > 1.0f) hi = 1.0f;

            uint8_t lb = (uint8_t)(lo * 255.0f + 0.5f);
            uint8_t hb = (uint8_t)(hi * 255.0f + 0.5f);

            return out.emit(SEL_FRACTION) && out.emit(lb) && out.emit(hb);
        }

        if (!strcmp(tok, "tag")) return selErr(err, errLen, "tag: selectors not supported yet (Phase 3)");

        return selErr(err, errLen, "panels: unknown selector");
    }

    // Append the RPN for one explicit index array (already entered) to `out`,
    // optionally complementing it (for "exclude").
    inline bool selEmitIndexArray(
        const char *&  p,
        const char *   end,
        PanelSelector& out,
        bool           complement,
        char *         err,
        size_t         errLen
    )
    {
        uint8_t tmp[SEL_MAX_INDEX_LIST];
        uint8_t n = 0;

        while (jsonNextElement(p, end)) {
            long v;

            if (!jsonReadUInt(p, end, &v) || v == 0 || v > 255)
                return selErr(err, errLen, "panels[]: invalid index (panels start at 1)");

            if (n >= SEL_MAX_INDEX_LIST) return selErr(err, errLen, "panels[]: too many");

            tmp[n++] = (uint8_t)v;
        }

        if (n == 0) return selErr(err, errLen, "panels[]: empty list");

        if (!out.emit(SEL_INDICES) || !out.emit(n)) return selErr(err, errLen, "panels: too complex");

        for (uint8_t i = 0; i < n; i++) if (!out.emit(tmp[i])) return selErr(err, errLen, "panels: too complex");

        if (complement && !out.emit(SEL_NOT)) return selErr(err, errLen, "panels: too complex");

        return true;
    }

    // Append the RPN for the panels value at `p` to `out` (recursive for composition).
    inline bool selAppend(
        const char *&  p,
        const char *   end,
        PanelSelector& out,
        char *         err,
        size_t         errLen,
        uint8_t        depth
    )
    {
        if (depth > SEL_STACK_MAX) return selErr(err, errLen, "panels: nesting too deep");

        jsonSkipWs(p, end);

        if (p >= end) return selErr(err, errLen, "panels: empty");

        char c = *p;

        if (c == '"') {
            char tok[40];

            if (!jsonReadString(p, end, tok, sizeof(tok))) return selErr(err, errLen, "panels: bad string");

            return selEmitToken(tok, out, err, errLen);
        }

        if (c == '[') {
            if (!jsonEnterArray(p, end)) return selErr(err, errLen, "panels: bad array");

            return selEmitIndexArray(p, end, out, false, err, errLen);
        }

        if (c == '{') {
            if (!jsonEnterObject(p, end)) return selErr(err, errLen, "panels: bad object");

            char key[12];
            bool got = false;

            while (jsonNextKey(p, end, key, sizeof(key))) {
                if (!strcmp(key, "any") || !strcmp(key, "all")) {
                    if (got) return selErr(err, errLen, "panels: one composition key per object");

                    got = true;

                    uint8_t op = !strcmp(key, "any") ? SEL_OR : SEL_AND;

                    if (!jsonEnterArray(p, end)) return selErr(err, errLen, "panels.any/all: expected array");

                    uint8_t cnt = 0;

                    while (jsonNextElement(p, end)) {
                        if (!selAppend(p, end, out, err, errLen, depth + 1)) return false;

                        if (++cnt >= 2 && !out.emit(op)) return selErr(err, errLen, "panels: too complex");
                    }

                    if (cnt == 0) return selErr(err, errLen, "panels.any/all: empty");
                } else if (!strcmp(key, "not")) {
                    if (got) return selErr(err, errLen, "panels: one composition key per object");

                    got = true;

                    if (!selAppend(p, end, out, err, errLen, depth + 1)) return false;

                    if (!out.emit(SEL_NOT)) return selErr(err, errLen, "panels: too complex");
                } else if (!strcmp(key, "exclude")) {
                    if (got) return selErr(err, errLen, "panels: one composition key per object");

                    got = true;

                    if (!jsonEnterArray(p, end)) return selErr(err, errLen, "panels.exclude: expected array");

                    if (!selEmitIndexArray(p, end, out, true, err, errLen)) return false;
                } else {
                    jsonSkipValue(p, end);
                }
            }

            if (!got) return selErr(err, errLen, "panels: empty object (use any/all/not/exclude)");

            return true;
        }

        return selErr(err, errLen, "panels: expected string, array, or object");
    }

    // Public entry point. Parses the panels value at `p` into a fresh selector program.
    inline bool parsePanelSelector(
        const char *&  p,
        const char *   end,
        PanelSelector& out,
        char *         err,
        size_t         errLen
    )
    {
        out.clear();

        return selAppend(p, end, out, err, errLen, 0);
    }
}  // namespace Lightnet
