#ifdef LIGHTNET_TARGET_CONTROLLER
#include "ControllerDiscoveryService.hpp"
#include "../../Utils/Debug.hpp"

ControllerDiscoveryService::ControllerDiscoveryService(ControllerEdgeTransport &transport, uint8_t edgeCountPerPanel)
    : transport(transport), treeBuilder(edgeCountPerPanel), coordinator(transport, &treeBuilder)
{
}

void ControllerDiscoveryService::begin(uint32_t nowMs)
{
    this->coordinator.begin(nowMs);  // logs its own "[DISC] root pull sent" (see DiscoveryCoordinator.cpp)
}

void ControllerDiscoveryService::tick(uint32_t nowMs)
{
    // Bounded so a burst of incoming bytes (or continuous line noise on an unconnected/floating
    // trunk) can't hog the CPU here indefinitely -- the next tick() call picks up where this one
    // left off.
    uint16_t drained = 0;
    bool wasComplete = this->coordinator.isComplete();

    while (this->transport.available() && drained < MAX_BYTES_PER_TICK) {
        if (this->framer.onByte(this->transport.readByte(), nowMs)) {
            this->coordinator.onFrameArrived(this->framer.frame(), this->framer.frameSize(), nowMs);
        }

        drained++;
    }

    // Raw byte arrival on the trunk RX pin, independent of whether it ever resolves into a
    // valid frame -- distinguishes "nothing is reaching this pin at all" from "bytes arrive but
    // don't frame" (noise, wrong baud, a receive path that never completes a valid packet).
    if (drained > 0) {
        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] rx raw bytes"), drained));
    }

    this->coordinator.tick(nowMs);

    if (!wasComplete && this->coordinator.isComplete()) {
        if (this->coordinator.walkStalled()) {
            DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(
                         DPF("[DISC] stalled, panels found"),
                         this->treeBuilder.panelCount()
            ));
        } else {
            DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] complete, panels found"), this->treeBuilder.panelCount()));
        }
    }
}

bool ControllerDiscoveryService::isComplete() const
{
    return this->coordinator.isComplete();
}

const Lightnet::DiscoveryTreeBuilder &ControllerDiscoveryService::tree() const
{
    return this->treeBuilder;
}

#endif  // LIGHTNET_TARGET_CONTROLLER
