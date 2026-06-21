#pragma once

// ============================================================================
// TopologyConfigStore — per-device topology configuration, persisted to
// /config/topology.json:
//
//   { "schemaVersion": 1,
//     "logicalRoot": 5 }
//
// Holds the logical root (§4.1). Writes are immediate (config actions are
// rare) via tmp+rename.
// ============================================================================

#include <stdint.h>
#include <stddef.h>
#include "../../Core/Controller/TopologyIndex.hpp"

namespace Lightnet {
    class TopologyConfigStore
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

        private:
            uint8_t _logicalRoot;

            bool readFile();
            void writeFile();
    };
}  // namespace Lightnet
