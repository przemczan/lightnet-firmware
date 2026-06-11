#pragma once

#include <stdint.h>

namespace Lightnet {
    // ============================================================================
    // SpscByteQueue — lock-free single-producer / single-consumer byte-record ring
    // ============================================================================
    //
    // A fixed-capacity FIFO of variable-length byte records, designed for one
    // producer (e.g. an ISR) and one consumer (e.g. the main loop) running
    // concurrently with NO locks and NO interrupt masking.
    //
    // Each record is stored as a 2-byte little-endian length prefix followed by
    // the payload. Records may wrap across the physical end of the buffer (the
    // consumer reassembles them on `pop`), so any record up to capacity-3 bytes
    // can always be enqueued whenever enough *total* free space exists — there is
    // no contiguous-space / fragmentation failure mode.
    //
    // HARD PREREQUISITE: exactly ONE producer and ONE consumer, each on its own
    // execution context. (Panel use: producer = TWI ISR `push`; consumer = main-loop
    // `pop`.) Two producers or two consumers — or a re-entrant call — breaks it.
    //
    // Why it is race-free without locks:
    //   * `_w` (write offset) is written ONLY by the producer; read by the consumer.
    //   * `_r` (read offset)  is written ONLY by the consumer; read by the producer.
    //   * `push` reads the consumer's *not-yet-advanced* `_r` and only ever writes
    //     into the free region [`_w`, `_r`). If the producer preempts a `pop`
    //     mid-read, it sees the old `_r`, treats the record being read as still
    //     occupied, and cannot touch those bytes. The consumer frees that space only
    //     by publishing the advanced `_r` AFTER its read loop completes. Symmetrically
    //     the producer publishes `_w` only AFTER the whole record is written, so the
    //     consumer never observes a half-written record.
    //   * `_buf` and the indices are `volatile`, so the compiler keeps these
    //     publish-last orderings. On a single in-order core (AVR) that is sufficient;
    //     on a multi-core target add real memory barriers at the publish/observe points.
    //   * Index type is uint8_t when CapacityBytes <= 256, so an index load/store is a
    //     single atomic byte access on 8-bit MCUs — the basis of the lock freedom.
    //
    // One byte is kept permanently unused so that `_w == _r` unambiguously means
    // "empty" (rather than "full").
    //
    // Cost note: `_buf` is `volatile`, so the producer copies byte-by-byte (no memcpy).
    // On AVR that adds a handful of cycles per byte to the (interrupt-off) ISR push —
    // negligible for ~80 B packets, but keep it in mind on very tight ISR budgets.
    namespace detail {
        template <bool Fits8> struct SpscIndex {
            typedef uint16_t type;
        };
        template <>           struct SpscIndex<true> {
            typedef uint8_t type;
        };
    }

    template <uint16_t CapacityBytes>
    class SpscByteQueue
    {
        public:
            typedef typename detail::SpscIndex<(CapacityBytes <= 256)>::type Index;

            SpscByteQueue() : _w(0), _r(0)
            {
            }

            // Producer side. Copies one record (`len` bytes) into the ring.
            // Returns false (record dropped, ring untouched) if it does not fit.
            bool push(const void *data, uint16_t len)
            {
                if (len == 0) return false;

                const uint16_t need  = HEADER + len;
                const uint16_t w     = _w;                 // producer-owned
                const uint16_t r     = _r;                 // atomic snapshot of consumer index
                const uint16_t used  = (w >= r) ? (uint16_t)(w - r)
                                                : (uint16_t)(CapacityBytes - r + w);
                const uint16_t freeB = (uint16_t)(CapacityBytes - 1 - used);

                if (need > freeB) return false;

                const uint8_t *s = (const uint8_t *)data;
                uint16_t p = w;

                p = put(p, (uint8_t)(len & 0xFF));
                p = put(p, (uint8_t)(len >> 8));

                for (uint16_t i = 0; i < len; i++) p = put(p, s[i]);

                _w = (Index)p;   // publish only after the whole record is written

                return true;
            }

            // Consumer side. Copies the next record into `out` (capacity `outCap`)
            // and returns its length. Returns 0 when the ring is empty. `outCap`
            // must be >= the largest record ever pushed (records never exceed
            // CapacityBytes - HEADER - 1, so sizing `out` to CapacityBytes is safe).
            uint16_t pop(void *out, uint16_t outCap)
            {
                const uint16_t w = _w;     // atomic snapshot of producer index
                uint16_t r = _r;           // consumer-owned

                if (w == r) return 0;      // empty

                uint8_t lo, hi;
                uint16_t p = r;

                p = get(p, lo);
                p = get(p, hi);

                const uint16_t len = (uint16_t)lo | ((uint16_t)hi << 8);
                uint8_t *d   = (uint8_t *)out;

                for (uint16_t i = 0; i < len; i++) {
                    uint8_t b;

                    p = get(p, b);

                    if (i < outCap) d[i] = b;
                }

                _r = (Index)p;   // publish only after the whole record is read

                return len;
            }

            bool empty() const
            {
                return _w == _r;
            }

            // Reset to empty. NOT concurrency-safe — call only when neither side is
            // touching the queue (e.g. during init).
            void reset()
            {
                _w = 0;
                _r = 0;
            }

        private:
            static const uint16_t HEADER = 2;

            uint16_t put(uint16_t p, uint8_t b)
            {
                _buf[p] = b;

                return (uint16_t)((p + 1 == CapacityBytes) ? 0 : p + 1);
            }

            uint16_t get(uint16_t p, uint8_t &b) const
            {
                b = _buf[p];

                return (uint16_t)((p + 1 == CapacityBytes) ? 0 : p + 1);
            }

            volatile uint8_t _buf[CapacityBytes];
            volatile Index _w;     // write offset — producer only
            volatile Index _r;     // read offset  — consumer only
    };
}  // namespace Lightnet
