#include "PacketFramer.hpp"
#include "../../Utils/Debug.hpp"

namespace Lightnet {
    PacketFramer::PacketFramer(bool validateProtocolVersion)
        : filled(0), expectedSize(0), validateProtocolVersion(validateProtocolVersion)
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
                return false;  // unrecognized type byte — noise, stay unsynced
            }

            this->expectedSize = size;
        }

        this->buffer[this->filled] = value;
        this->filled++;

        if (this->filled < this->expectedSize) {
            return false;  // frame still incomplete
        }

        const Protocol::PacketMeta *meta = (const Protocol::PacketMeta *)this->buffer;

        bool valid = Protocol::validatePacket(meta, this->filled, this->validateProtocolVersion) == 0;

        #ifndef __AVR__
            // Panel RX bus logs are deferred to LightnetPanel::flushIdleDebugLogs() -- see
            // DebugSerial.hpp on why pushByte() must not print on the panel build.
            DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTLN(
                         DPF("[BUS] rx type"),
                         this->buffer[0],
                         DPF("panel"),
                         meta->header.targetPanelIndex,
                         valid ? DPF("valid") : DPF("invalid")
            ));

            DEBUG_IF(DEBUG_LIGHTNET_BUS_PACKET_CONTENT, {
            _debugPrintTimestamp();
            D_PRINT(DPF("[BUS] rx bytes:"));

            for (uint8_t i = 0; i < this->filled; i++) {
                _debugPrintSpace();
                D_PRINT(this->buffer[i]);
            }

            _debugPrintNewline();
        });
        #endif

        if (!valid) {
            this->reset();

            return false;
        }

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
