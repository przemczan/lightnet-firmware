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
        this->driver.onFrameArrived(fromEdge, frame, size, nowMs);

        if (frame->header.type != Protocol::PACKET_INITIALIZATION_PULL) {
            this->router.onFrameArrived(fromEdge, frame, size);
        }

        uint16_t myIndex = this->driver.assignedPanelIndex();

        if (myIndex == 0) {
            return false;  // not yet discovered -- nothing to dispatch locally
        }

        uint16_t target = frame->header.targetPanelIndex;

        return (target == 0) || (target == myIndex);
    }
}  // namespace Lightnet
