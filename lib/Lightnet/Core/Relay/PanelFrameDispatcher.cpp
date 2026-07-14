#include "PanelFrameDispatcher.hpp"

namespace Lightnet {
    PanelFrameDispatcher::PanelFrameDispatcher(PanelDiscoveryDriver &driver, PanelRouter &router)
        : driver(driver), router(router)
    {
    }

    bool PanelFrameDispatcher::onFrameArrived(
        uint8_t                     fromEdge,
        const Protocol::PacketMeta *frame,
        uint8_t                     size,
        uint32_t                    nowMs
    )
    {
        uint16_t myIndex = this->driver.assignedPanelIndex();
        uint16_t target  = frame->header.targetPanelIndex;

        // A frame addressed to this panel terminates here: no other node acts on it, so
        // relaying it downstream would only put a pointless transmission on the wire -- and in
        // the ADVANCE case that transmission lands exactly on top of the probe reply the driver
        // is about to solicit (see the ordering note below).
        bool addressedToSelf = (myIndex != 0) && (target == myIndex);

        // Relay BEFORE letting the driver react. The driver's reaction can be an immediate
        // probe PULL whose reply starts arriving within microseconds of the PULL's last byte --
        // a relay transmitted after that PULL would still be holding the transport's self-echo
        // mask (EdgeUartTransport::onRxByte() discards everything received mid-transmission)
        // when the reply lands, silently discarding it.
        if (frame->header.type != Protocol::PACKET_INITIALIZATION_PULL && !addressedToSelf) {
            this->router.onFrameArrived(fromEdge, frame, size);
        }

        this->driver.onFrameArrived(fromEdge, frame, size, nowMs);

        // Re-read: the frame may have been the PULL that just assigned this panel its index.
        myIndex = this->driver.assignedPanelIndex();

        if (myIndex == 0) {
            return false;  // not yet discovered -- nothing to dispatch locally
        }

        return (target == 0) || (target == myIndex);
    }
}  // namespace Lightnet
