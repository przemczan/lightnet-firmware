#pragma once

#include <stdint.h>

#if defined(ARDUINO_ARCH_ESP32)
    #include <Arduino.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/semphr.h>
#elif !defined(NATIVE_TEST)
    #include <mutex>
#endif

namespace Lightnet {
    class StoreLock
    {
        public:
            class Guard
            {
                public:
                    explicit Guard(StoreLock& lock) : _lock(lock)
                    {
                        _lock.acquire();
                    }

                    ~Guard()
                    {
                        _lock.release();
                    }

                private:
                    StoreLock& _lock;
            };

            StoreLock()
            {
                #if defined(ARDUINO_ARCH_ESP32)
                    _mutex = xSemaphoreCreateMutex();
                #endif
            }

            ~StoreLock()
            {
                #if defined(ARDUINO_ARCH_ESP32)

                    if (_mutex) {
                        vSemaphoreDelete(_mutex);
                        _mutex = nullptr;
                    }

                #endif
            }

            void acquire()
            {
                #if defined(ARDUINO_ARCH_ESP32)

                    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);

                #else
                    _stdMutex.lock();
                #endif
            }

            void release()
            {
                #if defined(ARDUINO_ARCH_ESP32)

                    if (_mutex) xSemaphoreGive(_mutex);

                #else
                    _stdMutex.unlock();
                #endif
            }

        private:
            #if defined(ARDUINO_ARCH_ESP32)
                SemaphoreHandle_t _mutex = nullptr;

            #else
                std::mutex _stdMutex;
            #endif
    };
}  // namespace Lightnet
