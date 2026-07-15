#pragma once

#include <stdint.h>

namespace Lightnet {
    // Narrowest unsigned index that can address CapacityBytes slots (0..CapacityBytes-1). A byte
    // suffices for any capacity that fits one, which every real UART ring does -- and it keeps the
    // producer ISR's per-byte push to 8-bit ops on an 8-bit MCU (AVR) instead of the 2-instruction
    // 16-bit ops a uint16_t index forces, roughly halving the hottest path in the RX vector. Falls
    // back to uint16_t for larger rings, so the template stays fully general.
    template <bool FitsByte>
    struct RingIndexType {
        typedef uint16_t type;
    };

    template <>
    struct RingIndexType<true> {
        typedef uint8_t type;
    };

    // ByteRing — lock-free single-producer / single-consumer ring of individual bytes.
    //
    // Unlike SpscByteQueue (variable-length, length-prefixed *records*), this is a plain byte
    // stream: one push per byte, one pop per byte, no framing of its own. Built for a UART RX
    // path where the producer is an ISR receiving one byte at a time and the consumer is a main
    // loop draining them (e.g. into a PacketFramer, which does its own framing on top).
    //
    // Same lock-freedom argument as SpscByteQueue: `_w` is written only by the producer, `_r`
    // only by the consumer, each publishes its index only after its own read/write completes.
    // One slot is kept permanently unused so `_w == _r` unambiguously means "empty".
    template <uint16_t CapacityBytes>
    class ByteRing
    {
        public:
            typedef typename RingIndexType<(CapacityBytes <= 256)>::type Index;

            ByteRing() : _w(0), _r(0)
            {
            }

            // Producer side (e.g. an ISR). Returns false (byte dropped) if the ring is full.
            bool push(uint8_t value)
            {
                Index next = (Index)((_w + 1 == CapacityBytes) ? 0 : _w + 1);

                if (next == _r) {
                    return false;
                }

                _buf[_w] = value;
                _w       = next;

                return true;
            }

            // Consumer side (e.g. the main loop). Returns false if the ring is empty.
            bool pop(uint8_t &out)
            {
                if (_w == _r) {
                    return false;
                }

                out = _buf[_r];
                _r  = (Index)((_r + 1 == CapacityBytes) ? 0 : _r + 1);

                return true;
            }

            bool empty() const
            {
                return _w == _r;
            }

            // Reset to empty. NOT concurrency-safe — call only when neither side is touching it.
            void reset()
            {
                _w = 0;
                _r = 0;
            }

        private:
            volatile uint8_t _buf[CapacityBytes];
            volatile Index _w;  // write offset — producer only
            volatile Index _r;  // read offset  — consumer only
    };
}  // namespace Lightnet
