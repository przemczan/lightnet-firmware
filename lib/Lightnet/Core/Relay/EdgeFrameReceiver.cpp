#include "EdgeFrameReceiver.hpp"
#include "../../Utils/Debug.hpp"

namespace Lightnet {
    EdgeFrameReceiver::EdgeFrameReceiver()
        : activeEdge(NO_EDGE), completedEdge(NO_EDGE), lastActivityMs(0), dropLogged(false)
    {
    }

    bool EdgeFrameReceiver::onEdgeWake(uint8_t edgeIndex, uint32_t nowMs)
    {
        if (this->activeEdge != NO_EDGE) {
            DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(
                         DPF("[RECV] wake edge"),
                         edgeIndex,
                         DPF("ignored, busy on"),
                         this->activeEdge
            ));

            return false;  // a claim (this edge or another) is already in flight -- ignore
        }

        this->activeEdge     = edgeIndex;
        this->lastActivityMs = nowMs;
        this->dropLogged     = false;

        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[RECV] wake edge"), edgeIndex, DPF("claimed")));

        return true;
    }

    bool EdgeFrameReceiver::onByte(uint8_t value, uint32_t nowMs)
    {
        if (this->activeEdge == NO_EDGE) {
            // One line per unclaimed gap, not per byte -- this fires for every byte of a whole
            // dropped frame otherwise, and each print is a ~45ms blocking bit-banged call (see
            // DebugSerial.hpp) that starves pollWake() from ever running again while more bytes
            // keep arriving, turning a microsecond race into permanent starvation.
            if (!this->dropLogged) {
                DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[RECV] byte dropped, no claim"), value));

                this->dropLogged = true;
            }

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
            DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[RECV] claim timeout edge"), this->activeEdge));

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
