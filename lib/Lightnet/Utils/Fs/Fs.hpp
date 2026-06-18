#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>   // present on both ESP8266 and ESP32 cores

namespace Lightnet {
    // Thin facade over the platform LittleFS global. Hides the per-architecture
    // mount signature; everything else forwards 1:1 to the shared fs::FS base.
    // Implementations live in Fs_esp8266.cpp / Fs_esp32.cpp.
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

    // Portable directory iteration. ESP8266 uses Dir/openDir; ESP32 uses
    // File/openNextFile. Both expose the same minimal interface here.
    class FsDir
    {
        public:
            explicit FsDir(const char *path);

            bool   next();             // advance; false when no more entries
            String fileName() const;   // full path, e.g. "/scenes/foo.json"
            size_t fileSize() const;

        private:
            #ifdef ARDUINO_ARCH_ESP32
                File _dir;
                mutable File _entry;

            #else
                mutable Dir _dir;
            #endif
    };
}  // namespace Lightnet
