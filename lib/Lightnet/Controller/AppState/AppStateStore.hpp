#pragma once

#include <stdint.h>
#include "../../Utils/DeferredWriter.hpp"

namespace Lightnet {
    // Owns `/config/app_state.json` — runtime state that should survive reboots
    // when configured to do so (see ConfigurationStore::powerStateOnBoot).
    //
    // filesystem layout:
    //   /config/app_state.json
    //   { "schemaVersion": 1, "isOn": true }
    //
    // Written atomically via tmp+rename with a 5-second deferred-write window.
    class AppStateStore
    {
        public:
            AppStateStore();

            // Read the file (or create defaults). Does NOT apply side-effects —
            // callers derive the boot isOn from ConfigurationStore policy.
            void load();

            // Call from the main loop to drive deferred filesystem writes.
            void tick(uint32_t now);

            // Flush immediately. Call before any graceful reboot.
            void flush();

            bool isOn() const
            {
                return _isOn;
            }

            // Update in-memory state and mark dirty (no panel side-effects —
            // those are handled by StateServer). Returns false if value unchanged.
            bool setIsOn(bool value);

        private:
            bool _isOn = true;
            DeferredWriter writer{ 5000 };

            bool readFile();
            void writeFile();
    };
}  // namespace Lightnet
