#pragma once

// ArqEdgeLink — IEdgeLink decorator adding the sender side of the link-ARQ layer
// (Core/Relay/LinkArq.hpp) to the panel's real transport.
//
// Every sendOnEdge() of a Protocol::isLinkAckedType() frame is followed by a blocking ack
// window: the RX mux is parked on the egress edge and incoming bytes are drained through a
// LinkAckMatcher until the hop ack (matching the frame's full-frame CRC) arrives or
// LINK_ACK_TIMEOUT_MS expires; on timeout the frame is retransmitted from an internal copy, up
// to LINK_RETRANSMITS times. Exempt types pass straight through, so handing this decorator to
// PanelRouter (and to LightnetPanel's own reply sends) changes nothing for streams or the
// discovery control plane.
//
// The window bypasses EdgeFrameReceiver's claim machinery on purpose — re-entering
// LightnetPanel::pollBytes() from inside a dispatch (relays happen inside pollBytes) would
// recurse. Instead the owner provides ILinkWindowHooks so the wake-interrupt bookkeeping
// LightnetPanel maintains (pendingWakeMask, the PCMSK0 suppression mirror) stays consistent
// across the window: hooks.onLinkWindowBegin() masks the wakes and clears any pending latch,
// hooks.onLinkWindowEnd() clears again (transitions our own ack-window traffic latched at the
// boundaries) and re-arms. The mux is deliberately left parked on the egress edge afterwards —
// per protocol the next inbound frame on this panel most often arrives there (the hop's own
// onward reply), and a frame from any other edge re-parks via its wake as usual.
//
// If the peer's ack is lost but the peer already dispatched (its own reply may then arrive
// inside our window), the matcher consumes those reply bytes as noise and the retransmission
// can collide with the tail of that reply — see LinkArq.hpp's preamble for why this is rare,
// bounded, and recovered end-to-end. The peer's LinkDedup guarantees the retransmission is
// never dispatched twice.

#include <stdint.h>
#include "../Core/Relay/IEdgeLink.hpp"
#include "../Core/Relay/LinkArq.hpp"
#include "EdgeUartTransport.hpp"

class ILinkWindowHooks
{
    public:
        virtual void onLinkWindowBegin(uint8_t edgeIndex) = 0;
        virtual void onLinkWindowEnd() = 0;
};

class ArqEdgeLink : public Lightnet::IEdgeLink
{
    public:
        ArqEdgeLink(EdgeUartTransport &transport, ILinkWindowHooks &hooks);

        // Transmits, then (acked types only) runs the ack window with retransmissions.
        void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) override;

        // Diagnostics only -- free-running, wrap silently (same convention as
        // EdgeUartTransport's stamps): link retransmissions performed, and frames that
        // exhausted every retransmission without an ack (residual for end-to-end recovery).
        uint16_t retransmitCount() const;
        uint16_t ackTimeoutCount() const;

    private:
        // True once the matching ack arrived; false on window timeout.
        bool awaitLinkAck(uint8_t edgeIndex, uint16_t frameCrc);

        EdgeUartTransport &transport;
        ILinkWindowHooks &hooks;
        Lightnet::LinkAckMatcher ackMatcher;
        uint8_t retransmitBuffer[Protocol::MAX_PACKET_SIZE];
        uint16_t linkRetransmits = 0;
        uint16_t linkAckTimeouts = 0;
};
