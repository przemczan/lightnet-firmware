#ifdef LIGHTNET_TARGET_CONTROLLER
#include "ControllerDiscoveryService.hpp"

ControllerDiscoveryService::ControllerDiscoveryService(ControllerEdgeTransport &transport, uint8_t edgeCountPerPanel)
    : transport(transport), treeBuilder(edgeCountPerPanel), coordinator(transport, &treeBuilder)
{
}

void ControllerDiscoveryService::begin()
{
    this->coordinator.begin();
}

void ControllerDiscoveryService::tick()
{
    while (this->transport.available()) {
        if (this->framer.pushByte(this->transport.readByte())) {
            this->coordinator.onFrameArrived(this->framer.frame(), this->framer.frameSize());
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
