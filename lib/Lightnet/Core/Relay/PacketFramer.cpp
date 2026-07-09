#include "PacketFramer.hpp"
#include "../../Utils/Debug.hpp"

namespace Lightnet {
    PacketFramer::PacketFramer(bool validateProtocolVersion)
        : filled(0), expectedSize(0), validateProtocolVersion(validateProtocolVersion),
        unsyncedLogged(false)
    {
    }

    void PacketFramer::reset()
    {
        this->filled = 0;
        this->expectedSize = 0;
    }

    bool PacketFramer::pushByte(uint8_t value)
    {
        // A previously-completed frame that the caller hasn't reset() — start the next one
        // fresh rather than writing past the end of buffer.
        if (this->filled > 0 && this->filled == this->expectedSize) {
            this->reset();
        }

        if (this->filled == 0) {
            uint8_t size = Protocol::packetSizeForType((Protocol::packetType_t)value);

            if (size == 0) {
                // One line per unsynced run, not per byte -- a preamble is several bytes long and
                // each print is a blocking bit-banged call (see DebugSerial.hpp) that competes
                // with the ISR draining the rest of the same frame off the wire.
                if (!this->unsyncedLogged) {
                    DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[FRAMER] unsynced byte"), value));

                    this->unsyncedLogged = true;
                }

                return false;  // unrecognized type byte — noise, stay unsynced
            }

            this->expectedSize   = size;
            this->unsyncedLogged = false;
        }

        this->buffer[this->filled] = value;
        this->filled++;

        if (this->filled < this->expectedSize) {
            return false;  // frame still incomplete
        }

        uint8_t validationResult = Protocol::validatePacket(
            (const Protocol::PacketMeta *)this->buffer,
            this->filled,
            this->validateProtocolVersion
        );

        if (validationResult != 0) {
            // One dump per failed frame (not per byte) -- see the unsynced-byte log above for why
            // per-byte prints in this path are off the table.
            DEBUG_IF(DEBUG_DISCOVERY, {
                D_PRINTLN(
                    DPF("[FRAMER] frame invalid type"),
                    this->buffer[0],
                    DPF("size"),
                    this->filled,
                    DPF("reason"),
                    validationResult
                );

                _debugPrintTimestamp();
                D_PRINT(DPF("[FRAMER] bytes:"));

                for (uint8_t i = 0; i < this->filled; i++) {
                    _debugPrintSpace();
                    D_PRINT(this->buffer[i]);
                }

                _debugPrintNewline();
            });

            this->reset();

            return false;
        }

        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(
                     DPF("[FRAMER] frame ok type"),
                     this->buffer[0],
                     DPF("size"),
                     this->filled
        ));

        return true;
    }

    const Protocol::PacketMeta *PacketFramer::frame() const
    {
        return (const Protocol::PacketMeta *)this->buffer;
    }

    uint8_t PacketFramer::frameSize() const
    {
        return this->filled;
    }
}  // namespace Lightnet
