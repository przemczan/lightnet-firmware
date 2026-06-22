#pragma once
//
// MainLoopQueue — generic "run this on the main loop" deferred-execution queue.
//
// Why it exists:
//   HTTP handlers run on the AsyncTCP task; several of them emit I2C packets, and
//   PacketMirror::capture() assumes packets are only ever emitted from the main-loop
//   task (it self-flushes on overflow, touching the WS client list). This queue lets
//   a handler package its work as a function pointer + a small POD argument blob and
//   hand it to the main loop, where drain() runs it. That keeps all packet emission
//   single-threaded — the precondition that makes PacketMirror's lock-free
//   flush-on-overflow safe.
//
// Producer/consumer model:
//   Storage is a SpscByteQueue, but BOTH push and pop are wrapped in the same brief
//   critical section (the primitive WebsocketServer uses for its WS command queue).
//   That supplies the memory barrier a multi-core ESP32 needs — SpscByteQueue alone
//   is only lock-free-safe on a single in-order core — and serialises producer vs
//   consumer, so it is robust even if a task posts from the main loop. The task fn()
//   itself runs OUTSIDE the lock (its record is copied out first), so a slow or
//   packet-emitting task never blocks the producer.
//
// Args contract:
//   Args are copied into the ring by value, so they must be self-contained POD with
//   no pointers into request-scoped memory. Scene play defers by scene id; the main
//   loop reloads from SceneStore before emitting packets.

#include <stdint.h>
#include <string.h>
#include "../Core/Common/SpscByteQueue.hpp"

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    #include <Arduino.h>
#endif

namespace Lightnet {
    class MainLoopQueue
    {
        public:
            typedef void (*TaskFn)(const uint8_t *args, uint16_t len);

            static const uint16_t CAPACITY = 512; // ring bytes; records are fn-ptr + small args
            static const uint16_t MAX_ARGS = 64;   // per-task argument cap

            // Producer (AsyncTCP, or any task). Copies [fn][args] into the ring. Returns
            // false if the queue is full or argLen exceeds MAX_ARGS — the caller should
            // surface that as HTTP 503 rather than silently dropping work.
            bool post(TaskFn fn, const void *args, uint16_t argLen)
            {
                if (fn == nullptr || argLen > MAX_ARGS) {
                    return false;
                }

                uint8_t blob[sizeof(TaskFn) + argLen];

                memcpy(blob, &fn, sizeof(TaskFn));

                if (argLen) {
                    memcpy(blob + sizeof(TaskFn), args, argLen);
                }

                lock();

                bool ok = ring.push(blob, (uint16_t)(sizeof(TaskFn) + argLen));

                unlock();

                return ok;
            }

            // Consumer (main loop). Pops and runs every queued task in FIFO order. Each
            // record is copied out under the lock; the task runs unlocked so it may take
            // time / emit packets without blocking the producer.
            void drain()
            {
                uint8_t blob[sizeof(TaskFn) + MAX_ARGS];

                for (;;) {
                    lock();

                    uint16_t n = ring.pop(blob, sizeof(blob));

                    unlock();

                    if (n == 0) {
                        break;             // ring empty
                    }

                    if (n < sizeof(TaskFn)) {
                        continue;          // malformed record — skip defensively
                    }

                    TaskFn fn;

                    memcpy(&fn, blob, sizeof(TaskFn));
                    fn(blob + sizeof(TaskFn), (uint16_t)(n - sizeof(TaskFn)));
                }
            }

        private:
            SpscByteQueue<CAPACITY> ring;

            #ifdef ARDUINO_ARCH_ESP32
                portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
            #endif

            inline void lock()
            {
                #if defined(ARDUINO_ARCH_ESP32)
                    portENTER_CRITICAL(&mux);
                #elif defined(ARDUINO_ARCH_ESP8266)
                    noInterrupts();
                #endif
            }

            inline void unlock()
            {
                #if defined(ARDUINO_ARCH_ESP32)
                    portEXIT_CRITICAL(&mux);
                #elif defined(ARDUINO_ARCH_ESP8266)
                    interrupts();
                #endif
            }
    };
}  // namespace Lightnet
