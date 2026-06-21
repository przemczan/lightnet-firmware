#pragma once

#include <stdint.h>
#include "../../Utils/DeferredWriter.hpp"
#include "../../Common/Database/SingleRecordStore.hpp"
#include "Store/AppStateCodec.hpp"

namespace Lightnet {
    // Owns `/config/app_state.db` — last-known runtime state (power + last scene), stored as
    // a single binary Database record.
    class AppStateStore
    {
        public:
            AppStateStore();

            void load();
            void tick(uint32_t now);
            void flush();

            bool isOn() const
            {
                return _record.isOn != 0;
            }

            bool setIsOn(bool value);

            const char * lastPlayedSceneId() const
            {
                return _record.lastPlayedSceneId;
            }

            bool lastPlayedSceneIsStored() const
            {
                return _record.lastPlayedSceneIsStored != 0;
            }

            bool setLastPlayedSceneId(const char *id, bool isStored);

        private:
            static constexpr const char *APP_STATE_DATABASE_PATH = "/config/app_state.db";
            static constexpr const char *APP_STATE_DATA_DIR      = "/config";

            AppStateRecord _record{ 1, { 0 }, 1 };
            DeferredWriter writer{ 5000 };

            SingleRecordStore<AppStateCodec> _store{
                APP_STATE_DATABASE_PATH, APP_STATE_DATA_DIR
            };

            void writeFile();
    };
}  // namespace Lightnet
