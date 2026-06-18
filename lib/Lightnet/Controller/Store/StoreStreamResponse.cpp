#include "StoreStreamResponse.hpp"

namespace Lightnet {
    void sendLockedContent(AsyncWebServerRequest *req, StoreLock& lock, IContentReader *reader)
    {
        if (!reader) {
            lock.release();
            req->send(500, "application/json", "{\"error\":\"read_failed\"}");

            return;
        }

        AsyncResponseStream *res = req->beginResponseStream("application/json");
        uint8_t buf[512];

        for (;;) {
            int n = reader->read(buf, sizeof(buf));

            if (n < 0) {
                delete reader;
                lock.release();
                delete res;
                req->send(500, "application/json", "{\"error\":\"read_failed\"}");

                return;
            }

            if (n == 0) break;

            res->write(buf, (size_t)n);
        }

        delete reader;

        StoreLock *lockPtr = &lock;

        req->onDisconnect([lockPtr]() {
            lockPtr->release();
        });

        req->send(res);
    }

    void sendLockedJson(AsyncWebServerRequest *req, StoreLock& lock, const char *json)
    {
        StoreLock *lockPtr = &lock;

        req->onDisconnect([lockPtr]() {
            lockPtr->release();
        });

        req->send(200, "application/json", json ? json : "{}");
    }
}  // namespace Lightnet
