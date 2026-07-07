#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

namespace Lightnet {
    // Thin facade over the platform LittleFS global. Implementation lives in Fs_esp32.cpp.
    struct Fs {
        static bool   begin();
        static bool   exists(const char *path);
        static File   open(const char *path, const char *mode = "r");
        static bool   deleteFile(const char *path);
        static bool   rename(const char *from, const char *to);
        static bool   mkdir(const char *path);

        // Underlying FS, e.g. for AsyncWebServerRequest::send(Fs::raw(), path, type).
        static fs::FS &raw();
    };

    // Directory iteration over File/openNextFile.
    class FsDir
    {
        public:
            explicit FsDir(const char *path);

            bool   next();             // advance; false when no more entries
            String fileName() const;   // full path, e.g. "/scenes/foo.json"
            size_t fileSize() const;

        private:
            File _dir;
            mutable File _entry;
    };
}  // namespace Lightnet
