#pragma once

// ============================================================================
// TopologyConfigStore — per-device topology configuration, persisted to
// /config/topology.json:
//
//   { "schemaVersion": 1,
//     "logicalRoot": 5,
//     "tags": { "1": ["accent","left"], "5": ["accent"] } }
//
// Holds the logical root (§4.1) and the panel→tags map, and implements
// ITagResolver so the scene engine can resolve `tag:` selectors. Writes are
// immediate (config actions are rare) via tmp+rename.
// ============================================================================

#include <stdint.h>
#include <stddef.h>
#include "TopologyIndex.hpp"
#include "TagResolver.hpp"

namespace Lightnet {
    class TopologyConfigStore : public ITagResolver
    {
        public:
            TopologyConfigStore();

            // Read the file (or create defaults) into memory. Call once on boot.
            void load();

            uint8_t logicalRoot() const
            {
                return _logicalRoot;
            }

            // Persist a new logical root (0 → physical root 1). Returns true if it changed.
            bool setLogicalRoot(uint8_t panelIndex);

            // Replace the entire tag map from a JSON body ({ "1":["accent"], … }), validate,
            // and persist. Whole-map replace. Returns false (with errMsg) on malformed input.
            bool replaceTags(const char *body, size_t len, char *errMsg, size_t errLen);

            // Serialize the full config ({ logicalRoot, tags }) into the caller's buffer.
            void writeJson(char *buf, size_t bufLen) const;

            // Serialize just the tag map ({ "1":["accent"], … }).
            void writeTagsJson(char *buf, size_t bufLen) const;

            // ITagResolver: add panels carrying `name` to `out` (slots).
            void panelsForTag(const char *name, const TopologyIndex& topo, PanelSet& out) const override;

        private:
            struct Entry {
                char    tag[TAG_NAME_MAX + 1];
                uint8_t panel; // 1-based panel index
            };

            static const uint8_t MAX_ENTRIES = 64;

            uint8_t _logicalRoot;
            Entry entries[MAX_ENTRIES];
            uint8_t count;

            bool readFile();
            void writeFile();

            // Parse a tag map (object of "panel":[tags]) at the cursor into a scratch array.
            bool parseTags(
                const char *& p,
                const char *  end,
                Entry *       out,
                uint8_t&      outCount,
                char *        errMsg,
                size_t        errLen
            );

            // Append the tag map (without surrounding braces of the parent) to buf at pos.
            size_t appendTagsMap(char *buf, size_t bufLen, size_t pos) const;
    };
}  // namespace Lightnet
