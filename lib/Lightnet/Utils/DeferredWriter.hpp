#pragma once

#include <stdint.h>

namespace Lightnet {
    // Tracks whether in-memory state has been written to persistent storage.
    // Callers own writeFile()/readFile(); this helper only manages the dirty
    // timestamp and the flush-interval predicate.
    //
    // Usage inside a store:
    //   DeferredWriter writer{5000};
    //
    //   void setBrightness(uint8_t v) {
    //       brightnessValue = v;
    //       writer.markDirty(millis());
    //   }
    //   void tick(uint32_t now) {
    //       if (writer.shouldFlush(now)) { writeFile(); writer.clear(); }
    //   }
    //   void flush() {
    //       if (writer.isDirty()) { writeFile(); writer.clear(); }
    //   }
    class DeferredWriter
    {
        public:
            explicit DeferredWriter(uint32_t flushIntervalMs)
                : flushIntervalMs(flushIntervalMs), dirtyAt(0)
            {
            }

            // Record the current time as the dirty timestamp (if not already dirty).
            void markDirty(uint32_t now)
            {
                if (dirtyAt == 0) {
                    dirtyAt = now ? now : 1; // guard against millis()==0 edge case
                }
            }

            bool isDirty() const
            {
                return dirtyAt != 0;
            }

            // Returns true when the flush interval has elapsed since markDirty was called.
            bool shouldFlush(uint32_t now) const
            {
                return (dirtyAt != 0) && ((uint32_t)(now - dirtyAt) >= flushIntervalMs);
            }

            void clear()
            {
                dirtyAt = 0;
            }

        private:
            uint32_t flushIntervalMs;
            uint32_t dirtyAt;
    };
}  // namespace Lightnet
