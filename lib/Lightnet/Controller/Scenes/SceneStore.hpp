#pragma once
// SceneStore owns filesystem persistence for scene files at /scenes/<name>.json.
// Raw JSON bytes are stored verbatim so GET is a zero-parse file passthrough;
// there is no separate binary or re-encoded representation.

#include <stdint.h>
#include <stddef.h>

namespace Lightnet {
    class SceneStore
    {
        public:
            // Atomically write `len` bytes of JSON to /scenes/<name>.json.
            // Caller must already have validated the content via parseScene().
            // Returns false if the filesystem write fails.
            bool save(const char *name, const char *json, size_t len) const;

            // Heap-allocate a null-terminated copy of /scenes/<name>.json.
            // Returns nullptr on failure (file missing or OOM). Caller must free().
            // Sets `outLen` to the byte count (excluding the null terminator).
            char * load(const char *name, size_t& outLen) const;

            bool exists(const char *name) const;
            bool del(const char *name) const;

            // Write a JSON array of {name, size} objects into buf.
            void listJson(char *buf, size_t bufLen) const;

            // Maximum body size accepted for scene files.
            static constexpr size_t MAX_SCENE_BYTES = 4096;

            // Reserved name for the scene most recently played via
            // POST /api/scenes/play (inline body): it is stored under this name
            // and then played by name, so there is a single play-by-name path.
            // User-named scenes may not use this name (see isReservedName).
            static const char * reservedName()
            {
                return "Current";
            }

            static bool isReservedName(const char *name);
    };
}  // namespace Lightnet
