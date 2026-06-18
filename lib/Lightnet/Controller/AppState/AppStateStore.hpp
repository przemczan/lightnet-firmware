#pragma once

#include <stdint.h>
#include "../../Utils/DeferredWriter.hpp"
#include "../../Utils/EntryId.hpp"

namespace Lightnet {
    //   { "schemaVersion": 1, "isOn": true, "lastPlayedSceneId": "x9y8z7w6", "lastPlayedSceneIsStored": true }
    class AppStateStore
    {
        public:
            AppStateStore();

            void load();
            void tick(uint32_t now);
            void flush();

            bool isOn() const
            {
                return _isOn;
            }

            bool setIsOn(bool value);

            const char * lastPlayedSceneId() const
            {
                return _lastPlayedSceneId;
            }

            bool lastPlayedSceneIsStored() const
            {
                return _lastPlayedSceneIsStored;
            }

            bool setLastPlayedSceneId(const char *id, bool isStored);

        private:
            bool _isOn = true;
            char _lastPlayedSceneId[ENTRY_ID_MAX + 1] = { 0 };
            bool _lastPlayedSceneIsStored = true;
            DeferredWriter writer{ 5000 };

            bool readFile();
            void writeFile();
    };
}  // namespace Lightnet
