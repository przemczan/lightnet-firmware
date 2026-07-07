#pragma once

// Controller-only (WebsocketServer/WebsocketHandler) — the panel build has no Arduino.h at all.
#ifdef LIGHTNET_TARGET_CONTROLLER

    #include <Arduino.h>
    #include "Debug.hpp"

    void memcpyToVolatile(volatile uint8_t *dest, uint8_t *src, int size);

    void memcpyFromVolatile(uint8_t *dest, volatile uint8_t *src, int size);

    void dumpMem(uint8_t *mem, size_t size, uint8_t width = 20);

#endif  // LIGHTNET_TARGET_CONTROLLER
