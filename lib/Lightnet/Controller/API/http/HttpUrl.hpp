#pragma once
// Pure URL/name helpers — no ESPAsyncWebServer or Arduino dependency.
// Split out from HttpHelpers.hpp so they can be exercised by host-side
// unit tests under `pio test -e native`.

#include <stddef.h>
#include <string.h>

namespace Lightnet {
    namespace Http {
        // Returns true if `id` is a safe store key: 8–10 chars, [a-z0-9] only.
        inline bool isSafeId(const char *id)
        {
            if (!id) return false;

            size_t len = strlen(id);

            if (len < 8 || len > 10) return false;

            for (const char *c = id; *c; c++) {
                if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))) return false;
            }

            return true;
        }

        // Returns true if `name` is a safe filesystem identifier:
        //   - non-empty, max 18 characters
        //   - only [a-zA-Z0-9_-]
        inline bool isSafeName(const char *name)
        {
            if (!name || !name[0]) return false;

            for (const char *c = name; *c; c++) {
                if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                      (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) return false;
            }

            return strlen(name) <= 18;
        }

        // Extracts the segment immediately after `prefix` (up to next '/' or end)
        // into `out`. Returns false on prefix mismatch, empty segment, or overflow.
        inline bool nameFromUrl(const char *url, const char *prefix, char *out, size_t outLen)
        {
            if (!url || !prefix || !out || outLen == 0) return false;

            size_t pfxLen = strlen(prefix);

            if (strncmp(url, prefix, pfxLen) != 0) return false;

            const char *start = url + pfxLen;
            const char *slash = strchr(start, '/');
            size_t len = slash ? (size_t)(slash - start) : strlen(start);

            if (len == 0 || len >= outLen) return false;

            memcpy(out, start, len);
            out[len] = '\0';

            return true;
        }

        // Alias for palette/scene id path segments.
        inline bool idFromUrl(const char *url, const char *prefix, char *out, size_t outLen)
        {
            return nameFromUrl(url, prefix, out, outLen);
        }
    } // namespace Http
}  // namespace Lightnet
