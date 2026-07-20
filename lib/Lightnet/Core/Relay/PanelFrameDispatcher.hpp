#pragma once

// PanelFrameDispatcher — the one decision LightnetPanel's dispatch loop needs per arrived
// frame, extracted as pure logic so it's host-testable.
//
// Three rules, already individually designed elsewhere — this class only sequences them:
//   1. Every frame *except* PACKET_INITIALIZATION_PULL and frames addressed to this panel's own
//      index is fed to PanelRouter. PULL must never reach it (PanelDiscovery::onParentOffer()
//      has no "is this meant for me" guard, so relaying an in-transit PULL to an unrelated node
//      would corrupt its already-resolved edge state; see PanelDiscoveryDriver.hpp's class
//      comment). A self-addressed frame terminates here — no other node acts on it, so relaying
//      it is wasted wire time, and in the ADVANCE case that relay transmission would talk right
//      over the probe reply rule 2's driver reaction solicits.
//   2. Every frame is fed to PanelDiscoveryDriver, unconditionally (see PanelDiscoveryDriver.hpp)
//      — and only AFTER rule 1's relaying is already on the wire. A driver reaction can transmit
//      a probe PULL whose reply arrives within microseconds; a relay transmitted after that PULL
//      would overlap the reply while the transport's self-echo mask is discarding all RX
//      (EdgeUartTransport::onRxByte()).
//   3. Whether the caller's own application-packet switch (LightnetPanel::handlePacket()) should
//      act on this frame locally: only once this panel has been assigned an index, and only if
//      the frame's targetPanelIndex (protocol v10) is 0 (broadcast) or this panel's own index. A
//      frame that fails this check may still have been correctly relayed by rule 1 above (e.g.
//      someone else's unicast, or upstream traffic like DISCOVERY_DONE/REGISTER_EDGE passing
//      through on its way to the controller) — this return value is purely "should *I* act on
//      the contents," not "was this frame useful."
//
// Pure logic — takes the real PanelDiscoveryDriver/PanelRouter (both pure themselves), no
// Arduino, no transport.

#include <stdint.h>
#include "PanelDiscoveryDriver.hpp"
#include "PanelRouter.hpp"

namespace Lightnet {
    class PanelFrameDispatcher
    {
        public:
            PanelFrameDispatcher(PanelDiscoveryDriver &driver, PanelRouter &router);

            // Feed one frame that arrived on `fromEdge`. Returns true if the caller's own
            // application-packet switch should act on this frame's contents locally.
            bool onFrameArrived(uint8_t fromEdge, const Protocol::PacketMeta *frame, uint8_t size, uint32_t nowMs);

        private:
            PanelDiscoveryDriver &driver;
            PanelRouter &router;
    };
}  // namespace Lightnet
