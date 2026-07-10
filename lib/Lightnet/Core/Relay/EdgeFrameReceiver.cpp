#include "EdgeFrameReceiver.hpp"

namespace Lightnet {
    EdgeFrameReceiver::EdgeFrameReceiver()
        : activeEdge(NO_EDGE), completedEdge(NO_EDGE), lastActivityMs(0)
    {
    }

    bool EdgeFrameReceiver::onEdgeWake(uint8_t edgeIndex, uint32_t nowMs)
    {
        if (this->activeEdge != NO_EDGE) {
            return false;  // a claim (this edge or another) is already in flight -- ignore
        }

        this->activeEdge     = edgeIndex;
        this->lastActivityMs = nowMs;

        return true;
    }

    bool EdgeFrameReceiver::onByte(uint8_t value, uint32_t nowMs)
    {
        if (this->activeEdge == NO_EDGE) {
            return false;  // no claim -- nothing to attribute this byte to
        }

        this->lastActivityMs = nowMs;

        if (!this->framer.pushByte(value)) {
            return false;  // frame still incomplete, or resynced after a bad header CRC
        }

        this->completedEdge = this->activeEdge;
        this->release();

        return true;
    }

    void EdgeFrameReceiver::tick(uint32_t nowMs)
    {
        if (this->activeEdge == NO_EDGE) {
            return;
        }

        if (nowMs - this->lastActivityMs >= FRAME_TIMEOUT_MS) {
            this->framer.reset();
            this->release();
        }
    }

    const Protocol::PacketMeta *EdgeFrameReceiver::frame() const
    {
        return this->framer.frame();
    }

    uint8_t EdgeFrameReceiver::frameSize() const
    {
        return this->framer.frameSize();
    }

    uint8_t EdgeFrameReceiver::fromEdge() const
    {
        return this->completedEdge;
    }

    void EdgeFrameReceiver::release()
    {
        this->activeEdge = NO_EDGE;
    }
}  // namespace Lightnet
