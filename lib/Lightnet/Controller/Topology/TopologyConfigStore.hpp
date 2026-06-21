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
#include "../../Core/Controller/TopologyIndex.hpp"
#include "../../Core/Controller/TagResolver.hpp"

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

            // Same as replaceTags, but reads the tag map object at the JSON cursor.
            bool replaceTagsAt(const char *& p, const char *end, char *errMsg, size_t errLen);

            // Serialize the full config ({ logicalRoot, tags }) into the caller's buffer.
            void writeJson(char *buf, size_t bufLen) const;

            // Cursor-based access to the raw (panel, tag) entries, in storage order, for
            // chunked HTTP serialization without a large contiguous buffer. Entries for a
            // given panel are always contiguous (parseTags writes them per-key). Returns
            // false once `index` is out of range.
            size_t tagEntryCount() const
            {
                return count;
            }

            bool tagEntryAt(size_t index, uint8_t& panel, const char *& tag) const
            {
                if (index >= count) return false;

                panel = entries[index].panel;
                tag   = entries[index].tag;

                return true;
            }

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
