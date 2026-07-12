#include "TrunkFrameReceiver.hpp"

namespace Lightnet {
    TrunkFrameReceiver::TrunkFrameReceiver(bool validateProtocolVersion)
        : framer(validateProtocolVersion), lastByteMs(0)
    {
    }

    bool TrunkFrameReceiver::onByte(uint8_t value, uint32_t nowMs)
    {
        if (this->framer.hasPartialFrame() && (nowMs - this->lastByteMs) >= IDLE_GAP_RESET_MS) {
            this->framer.reset();  // stale/truncated frame -- discard before this byte
        }

        this->lastByteMs = nowMs;

        return this->framer.pushByte(value);
    }

    const Protocol::PacketMeta *TrunkFrameReceiver::frame() const
    {
        return this->framer.frame();
    }

    uint8_t TrunkFrameReceiver::frameSize() const
    {
        return this->framer.frameSize();
    }

    void TrunkFrameReceiver::reset()
    {
        this->framer.reset();
    }
}  // namespace Lightnet
