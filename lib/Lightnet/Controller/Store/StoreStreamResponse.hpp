#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Utils/StoreLock.hpp"
#include "../Store/IContentReader.hpp"

namespace Lightnet {
    // Stream JSON from an IContentReader; releases `lock` when the TCP response completes.
    void sendLockedContent(
        AsyncWebServerRequest *req,
        StoreLock&             lock,
        IContentReader *       reader
    );

    // Send a small in-memory JSON body under the same lock lifetime rules.
    void sendLockedJson(
        AsyncWebServerRequest *req,
        StoreLock&             lock,
        const char *           json
    );
}  // namespace Lightnet
