#include "PacketFramer.hpp"

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

        bool valid = Protocol::validatePacket(
            (const Protocol::PacketMeta *)this->buffer,
            this->filled,
            this->validateProtocolVersion
                     ) == 0;

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
