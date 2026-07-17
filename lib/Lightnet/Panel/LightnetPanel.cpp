#ifndef LIGHTNET_TARGET_CONTROLLER
#include "LightnetPanel.hpp"
#include "BootloaderBridge.hpp"
#include "../Runtime/PanelClock.hpp"
#include "../Utils/Crc.hpp"
#include "../Utils/Debug.hpp"
#include <avr/io.h>
#include <avr/interrupt.h>

LightnetPanel::LightnetPanel()
    : discovery(EdgeUartTransport::EDGE_COUNT),
    arqLink(LNEdgeTransport, *this),
    driver(discovery, LNEdgeTransport),
    router(discovery, arqLink),
    dispatcher(driver, router),
    pendingWakeMask(0),
    lastRelayActivityMs(0),
    probingSinceMs(0),
    wakeInterruptsSuppressed(false)
#if DEBUG
        , pendingRxBusLogCount(0), lastHeartbeatMs(0), wakePcintRawCount(0), wakeIsrFiredCount(0),
        wakeIsrMaskedCount(0), wakeClaimGrantedCount(0), wakeClaimIgnoredCount(0),
        lastWakeEdgeSeen(NO_EDGE), diagCompletedFrames{}, diagPhantomClaims{},
        diagDeadFrameClaims{}, diagFramingErrorsByEdge{}, lastSeenFramingErrorStamp(0),
        diagMultiBitWakeMasks(0), diagSelfClaims(0), diagClaimBytes(0), diagLinkAcksSent(0),
        diagDupFramesDropped{}, diagLastDupType(0), diagLastDupCrc(0), lastDiagSum(0)
#endif
{
}

void LightnetPanel::begin()
{
    // Baud comes from src/panel.config.hpp -- must match the controller's LIGHTNET_TRUNK_BAUD.
    // The hardware redesign plan's §5 latency budget assumes 1Mbps, but this bus's physical
    // margin (series resistors + mux + cabling next to the +24V rail) doesn't hold up that high
    // on real hardware: at 500kbps the panel-to-panel hop shows UART framing/overrun faults
    // (rxErr in the heartbeat) and drops frames. 250kbps (UBRR=3, 0% baud error) is the fastest
    // rate bench-validated clean end to end; going faster is a hardware (slew/noise) problem,
    // not a firmware one.
    LNEdgeTransport.begin(LIGHTNET_TRUNK_BAUD);
}

void LightnetPanel::onEdgeWakeIsr(uint8_t edgeIndex)
{
    // Our own edge's drive can couple onto a neighbouring edge's separate wake-sense line
    // (PB1/PB2/PB3 sense each edge directly, not through the mux -- see EdgeUartTransport.hpp's
    // pin map), producing a wake with no real frame behind it. sendOnEdge() masks the wake
    // interrupts (PCMSK0) for the bulk of its own transmission, so this guard only catches the
    // boundary slivers that masking can't -- a transition landing between transmitting going
    // true and the mask taking effect (or the reverse at the end). Left unguarded, claiming a
    // coupled wake steals the mux onto the wrong edge until the next real wake fixes it --
    // discard instead, exactly like onRxByte()'s own self-echo mask for bytes.
    #if DEBUG
        this->wakeIsrFiredCount++;
    #endif

    if (LNEdgeTransport.isTransmitting()) {
        #if DEBUG
            this->wakeIsrMaskedCount++;
        #endif

        return;
    }

    // Latch which edge woke into the pending mask -- see pendingWakeMask's own comment on why
    // this must be a mask (every edge that woke survives until drained) rather than a single
    // "last edge wins" value. The actual claim decision and mux switch happen from pollWake() in
    // the main loop -- see the class comment's threading model.
    this->pendingWakeMask |= (uint8_t)(1 << edgeIndex);

    // Disarm the wake interrupts here, in the ISR, the instant the first one fires -- do NOT wait
    // for the main loop to do it once a claim is established. The wake-sense line IS the edge's
    // data line (EdgeUartTransport.hpp), and PCINT0 (vector 4) outranks the USART RX ISR
    // (vector 19), so a frame arriving on the parked edge storms this ISR back-to-back on every
    // bit transition, starving the RX ISR past the USART's 2-byte buffer into overruns (rxErrDOR)
    // -- and starving the main loop out of ever reaching the claim that syncWakeInterruptSuppression()
    // would otherwise mask on, a self-reinforcing deadlock that gets worse as baud rises. Only the
    // first transition carries information (which edge woke, now latched above); mask the rest.
    // Bookkeeping: set wakeInterruptsSuppressed too so syncWakeInterruptSuppression() heals every
    // path -- it re-enables the wakes at pollWake()'s end for a phantom wake (claimedEdge stays
    // NO_EDGE), or holds them masked once a real claim takes (claimedEdge set). Without this line
    // the mirror would read "not suppressed" while the hardware is masked, and the reconcile would
    // no-op, leaving the wakes dead forever.
    LNEdgeTransport.setWakeInterruptsEnabled(false);
    this->wakeInterruptsSuppressed = true;
}

void LightnetPanel::pollWake(uint32_t nowMs)
{
    // Fast path -- a single-byte volatile read is atomic on AVR, so the empty case (the vast
    // majority: this runs before every received byte in pollBytes()) needs no cli/sei at all.
    // A wake landing right after this read is not lost, just picked up on the next call.
    if (this->pendingWakeMask == 0) {
        return;
    }

    uint8_t mask;

    cli();
    mask         = this->pendingWakeMask;
    this->pendingWakeMask = 0;
    sei();

    #if DEBUG

        if (mask & (uint8_t)(mask - 1)) {
            this->diagMultiBitWakeMasks++;
        }

    #endif

    for (uint8_t edge = 0; edge < EdgeUartTransport::EDGE_COUNT; edge++) {
        if (!(mask & (1 << edge))) {
            continue;
        }

        #if DEBUG
            this->lastWakeEdgeSeen = edge;
        #endif

        if (this->receiver.onEdgeWake(edge, nowMs)) {
            #if DEBUG
                this->wakeClaimGrantedCount++;
                this->noteClaimStarted();
            #endif
            LNEdgeTransport.selectRxEdge(edge);

            // Only one edge can legitimately be receiving at a time (single-active-flow
            // invariant) -- any other bits still set in this drained mask are noise from the same
            // window, not a second real transmission, and will simply re-latch on their own next
            // real wake if they were genuine.
            break;
        }

        #if DEBUG
            this->wakeClaimIgnoredCount++;
        #endif
    }

    this->syncWakeInterruptSuppression();
}

void LightnetPanel::syncWakeInterruptSuppression()
{
    bool shouldSuppress =
        this->receiver.claimedEdge() != Lightnet::EdgeFrameReceiver::NO_EDGE;

    if (shouldSuppress == this->wakeInterruptsSuppressed) {
        return;
    }

    this->wakeInterruptsSuppressed = shouldSuppress;
    LNEdgeTransport.setWakeInterruptsEnabled(!shouldSuppress);
}

void LightnetPanel::clearPendingWakes()
{
    cli();
    this->pendingWakeMask = 0;
    sei();
}

void LightnetPanel::onLinkWindowBegin(uint8_t edgeIndex)
{
    (void)edgeIndex;  // ArqEdgeLink parks the mux itself; only the wake bookkeeping is ours

    LNEdgeTransport.setWakeInterruptsEnabled(false);
    this->wakeInterruptsSuppressed = true;
    this->clearPendingWakes();
}

void LightnetPanel::onLinkWindowEnd()
{
    // Transitions from the window's own traffic (the ack, or the boundary of our transmission)
    // may have latched before the begin-mask took effect -- a survivor would be claimed by the
    // next pollWake() as a phantom, deafening the mux for FRAME_TIMEOUT_MS.
    this->clearPendingWakes();

    // No claim can be held here (the window only ever opens outside a claim -- after a
    // completed frame released it, or around a panel-originated send), so this re-arms.
    this->syncWakeInterruptSuppression();
}

void LightnetPanel::sendLinkAck(uint8_t edgeIndex, uint16_t frameCrc)
{
    Protocol::PacketLinkAck ack =
        Protocol::makePacket<Protocol::PacketLinkAck>(Protocol::PACKET_LINK_ACK);

    ack.frameCrc = frameCrc;

    // Through the decorator for uniformity -- LINK_ACK is itself exempt, so this never nests
    // an ack window.
    this->arqLink.sendOnEdge(edgeIndex, Protocol::packetMeta(ack), sizeof(ack));

    #if DEBUG
        this->diagLinkAcksSent++;
    #endif
}

void LightnetPanel::flushPendingRxBusLogs()
{
    #if DEBUG

        if (this->pendingRxBusLogCount == 0) {
            return;
        }

        const PendingRxBusLog &entry = this->pendingRxBusLogs[0];

        DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTLN(
                     DPF("[BUS] rx type"),
                     entry.type,
                     DPF("panel"),
                     entry.panel,
                     DPF("self"),
                     entry.self,
                     DPF("valid actLocally"),
                     entry.actedLocally
        ));

        for (uint8_t i = 1; i < this->pendingRxBusLogCount; i++) {
            this->pendingRxBusLogs[i - 1] = this->pendingRxBusLogs[i];
        }

        this->pendingRxBusLogCount--;

    #endif
}

void LightnetPanel::pollBytes(uint32_t nowMs)
{
    uint8_t processed = 0;

    // Bounded by MAX_BYTES_PER_POLL (see its own comment) -- continuous RX must not keep this
    // loop from ever returning to tick()'s other duties.
    while (LNEdgeTransport.available() && processed < MAX_BYTES_PER_POLL) {
        // Re-check for a pending wake before every byte, not just once per tick() -- otherwise a
        // wake that lands while this loop is still draining (e.g. a slow handler, or traffic
        // arriving faster than one tick()) never gets claimed: pollWake() only runs once at the
        // top of tick(), so a claim released mid-drain would starve until this loop empties out
        // entirely, which incoming traffic can defer indefinitely.
        this->pollWake(nowMs);

        // Bytes waiting with no claim still physically came in through the mux from its
        // currently-selected edge (the mux only ever moves on a claim grant), so attribution is
        // never actually ambiguous -- claim that edge rather than letting onByte() drop them.
        // This is what keeps back-to-back frames alive: wake interrupts are suppressed for the
        // whole previous claim (syncWakeInterruptSuppression()), so a follow-up frame that
        // arrived during it is sitting in the ring with no wake ever latched for it. A noise
        // byte claiming an idle edge this way is bounded by the receiver's own
        // FRAME_TIMEOUT_MS/MAX_CLAIM_MS recovery, same as a stray wake.
        if (this->receiver.claimedEdge() == Lightnet::EdgeFrameReceiver::NO_EDGE) {
            this->receiver.onEdgeWake(LNEdgeTransport.currentRxEdge(), nowMs);
            this->syncWakeInterruptSuppression();
            #if DEBUG
                this->diagSelfClaims++;
                this->noteClaimStarted();
            #endif
        }

        uint8_t value = LNEdgeTransport.readByte();

        this->lastRelayActivityMs = nowMs;

        #if DEBUG
            this->diagClaimBytes++;

            // Attribute any framing faults the RX ISR flagged since the last drained byte to the
            // edge receiving now -- see diagFramingErrorsByEdge's comment.
            uint8_t feStamp = LNEdgeTransport.framingErrorStamp();

            if (feStamp != this->lastSeenFramingErrorStamp) {
                uint8_t claimed = this->receiver.claimedEdge();

                if (claimed < EdgeUartTransport::EDGE_COUNT) {
                    this->diagFramingErrorsByEdge[claimed] +=
                        (uint8_t)(feStamp - this->lastSeenFramingErrorStamp);
                }

                this->lastSeenFramingErrorStamp = feStamp;
            }

        #endif

        if (this->receiver.onByte(value, nowMs)) {
            #if DEBUG
                this->diagCompletedFrames[this->receiver.fromEdge()]++;
            #endif

            // A completed frame released the claim -- re-enable the wake interrupts before the
            // (potentially slow) dispatch below, so a new flow starting on a different edge
            // during it still gets its wake latched.
            this->syncWakeInterruptSuppression();

            const Protocol::PacketMeta *frame = this->receiver.frame();
            uint8_t size  = this->receiver.frameSize();

            // Link-ARQ receiver side (Core/Relay/LinkArq.hpp). A hop ack arriving outside any
            // sender window is stale link-local noise -- consumed, never dispatched or relayed.
            if (frame->header.type == Protocol::PACKET_LINK_ACK) {
                processed++;

                continue;
            }

            // Ack BEFORE dispatching: the upstream sender's window closes while this panel is
            // still relaying/acting. A duplicate (hop retransmission whose previous ack was
            // lost) is re-acked but not dispatched again -- no double relay, no double action.
            if (Protocol::isLinkAckedType(frame->header.type)) {
                uint16_t frameCrc = crc16(const_cast<Protocol::PacketMeta *>(frame), size);

                this->sendLinkAck(this->receiver.fromEdge(), frameCrc);

                if (this->linkDedup.checkAndNote(this->receiver.fromEdge(), frameCrc, nowMs)) {
                    #if DEBUG
                        this->diagDupFramesDropped[this->receiver.fromEdge()]++;
                        this->diagLastDupType = (uint8_t)frame->header.type;
                        this->diagLastDupCrc  = frameCrc;
                    #endif
                    processed++;

                    continue;
                }
            }

            bool actLocally = this->dispatcher.onFrameArrived(
                this->receiver.fromEdge(),
                frame,
                size,
                nowMs
            );

            this->lastRelayActivityMs = nowMs;

            if (actLocally) {
                this->handlePacket(frame, size);
            }

            #if DEBUG

                if (DEBUG_LIGHTNET_BUS) {
                    if (this->pendingRxBusLogCount >= PENDING_RX_BUS_LOG_CAP) {
                        // Queue full -- evict the oldest entry rather than dropping the new one,
                        // same reasoning as PanelDiscoveryDriver::deferLog(): the most recent bus
                        // traffic is what matters when diagnosing a stall.
                        for (uint8_t i = 1; i < PENDING_RX_BUS_LOG_CAP; i++) {
                            this->pendingRxBusLogs[i - 1] = this->pendingRxBusLogs[i];
                        }

                        this->pendingRxBusLogCount--;
                    }

                    this->pendingRxBusLogs[this->pendingRxBusLogCount].type         = (uint8_t)frame->header.type;
                    this->pendingRxBusLogs[this->pendingRxBusLogCount].panel        = frame->header.targetPanelIndex;
                    this->pendingRxBusLogs[this->pendingRxBusLogCount].self         = this->driver.assignedPanelIndex();
                    this->pendingRxBusLogs[this->pendingRxBusLogCount].actedLocally = actLocally;
                    this->pendingRxBusLogCount++;
                }

            #endif
        }

        processed++;
    }
}

void LightnetPanel::pollProbeClaim(uint32_t nowMs)
{
    if (!this->driver.isProbing()) {
        return;
    }

    uint8_t edge = this->driver.probingEdge();

    if (this->receiver.preemptClaim(edge, nowMs)) {
        #if DEBUG
            this->noteClaimStarted();
        #endif
        LNEdgeTransport.selectRxEdge(edge);
        this->syncWakeInterruptSuppression();
    }
}

void LightnetPanel::flushIdleDebugLogs(uint32_t nowMs)
{
    if (LNEdgeTransport.available()) {
        return;
    }

    if (this->driver.isProbing()) {
        if (this->probingSinceMs == 0) {
            this->probingSinceMs = nowMs;
        }

        if ((uint32_t)(nowMs - this->probingSinceMs) < PROBE_STUCK_MS) {
            return;
        }

        // Still probing long past PROBE_ATTEMPTS * PROBE_TIMEOUT_MS -- fall through and flush
        // anyway so a stuck probe shows up in the log instead of blocking it forever.
    } else {
        this->probingSinceMs = 0;
    }

    if ((uint32_t)(nowMs - this->lastRelayActivityMs) < RELAY_QUIET_MS) {
        return;
    }

    this->driver.flushOneDeferredLog();

    if (LNEdgeTransport.available()) {
        return;
    }

    this->flushPendingRxBusLogs();
}

#if DEBUG
    void LightnetPanel::flushHeartbeatLog(uint32_t nowMs)
    {
        // Deliberately ignores RELAY_QUIET_MS/PROBE_STUCK_MS and the deferred-log queues entirely --
        // a receiver that's gone truly deaf produces none of the events those gates wait for, so this
        // is the only way to see live state (which edge the mux is parked on, whether the frame
        // receiver has a claim stuck open) during a stall like that. Only skipped while actively
        // probing, since that's the one state with a real timing budget (PROBE_TIMEOUT_MS) a blocking
        // bit-banged print could still perturb.
        if (this->driver.isProbing()) {
            return;
        }

        if ((uint32_t)(nowMs - this->lastHeartbeatMs) < HEARTBEAT_INTERVAL_MS) {
            return;
        }

        this->lastHeartbeatMs = nowMs;

        DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTLN(
                     DPF("[HB] muxEdge"),
                     LNEdgeTransport.currentRxEdge(),
                     DPF("claim"),
                     this->receiver.claimedEdge(),
                     DPF("act"),
                     LNEdgeTransport.activityStamp(),
                     DPF("rxErrFE"),
                     LNEdgeTransport.framingErrorStamp(),
                     DPF("rxErrDOR"),
                     LNEdgeTransport.overrunErrorStamp(),
                     DPF("parent"),
                     this->discovery.parentEdge(),
                     DPF("pcintRaw"),
                     this->wakePcintRawCount,
                     DPF("isrFired"),
                     this->wakeIsrFiredCount,
                     DPF("isrMasked"),
                     this->wakeIsrMaskedCount,
                     DPF("claimGranted"),
                     this->wakeClaimGrantedCount,
                     DPF("claimIgnored"),
                     this->wakeClaimIgnoredCount,
                     DPF("lastWakeEdge"),
                     this->lastWakeEdgeSeen
        ));
    }

    void LightnetPanel::noteClaimStarted()
    {
        this->diagClaimBytes = 0;
    }

    void LightnetPanel::noteClaimReleasedByTimeout(uint8_t edge)
    {
        if (edge >= EdgeUartTransport::EDGE_COUNT) {
            return;
        }

        if (this->diagClaimBytes == 0) {
            this->diagPhantomClaims[edge]++;
        } else {
            this->diagDeadFrameClaims[edge]++;
        }
    }

    // One [DIAG] ledger line, printed only once the trunk has been quiet for DIAG_QUIET_MS and
    // only when something changed since the last dump -- so it appears after a traffic burst
    // (discovery, an OTA run) ends, never during one, and a blocking bit-banged print can't
    // swallow live relay frames. Per-edge triples are edge 0/1/2:
    //   cmp = frames completed, phm = phantom claims (timeout, zero bytes),
    //   dead = dead-frame claims (timeout, partial frame), fe = framing faults attributed to the
    //   receiving edge (feRaw = the transport's global stamp; any drift vs the triple's total is
    //   faults on discarded self-echo bytes), tx = frames sent (relay + own).
    //   Link-ARQ: lack = hop acks emitted, lrtx = hop retransmissions performed, lto = frames
    //   that exhausted every retransmission unacked; ldup (per-edge triple) = duplicates
    //   re-acked but not re-dispatched, with dtype/dcrc = the most recent one's header type and
    //   full-frame crc (identifies WHAT repeats when ldup climbs).
    //   mb = multi-edge wake masks, self = self-claims (bytes with no wake),
    //   dor = UART overrun faults, ign = wakes ignored while a claim was held.
    void LightnetPanel::flushRelayDiag(uint32_t nowMs)
    {
        if (LNEdgeTransport.available()) {
            return;
        }

        if ((uint32_t)(nowMs - this->lastRelayActivityMs) < DIAG_QUIET_MS) {
            return;
        }

        uint32_t sum = (uint32_t)this->diagMultiBitWakeMasks + this->diagSelfClaims
                       + LNEdgeTransport.framingErrorStamp() + LNEdgeTransport.overrunErrorStamp()
                       + this->wakeClaimIgnoredCount + this->diagLinkAcksSent
                       + this->arqLink.retransmitCount() + this->arqLink.ackTimeoutCount();

        for (uint8_t edge = 0; edge < EdgeUartTransport::EDGE_COUNT; edge++) {
            sum += this->diagCompletedFrames[edge];
            sum += this->diagPhantomClaims[edge];
            sum += this->diagDeadFrameClaims[edge];
            sum += this->diagFramingErrorsByEdge[edge];
            sum += this->diagDupFramesDropped[edge];
            sum += LNEdgeTransport.txFrameCount(edge);
        }

        if (sum == this->lastDiagSum) {
            return;
        }

        this->lastDiagSum = sum;

        DEBUG_IF(DEBUG_RELAY_DIAG, D_PRINTLN(
                     DPF("[DIAG] cmp"),
                     this->diagCompletedFrames[0],
                     this->diagCompletedFrames[1],
                     this->diagCompletedFrames[2],
                     DPF("phm"),
                     this->diagPhantomClaims[0],
                     this->diagPhantomClaims[1],
                     this->diagPhantomClaims[2],
                     DPF("dead"),
                     this->diagDeadFrameClaims[0],
                     this->diagDeadFrameClaims[1],
                     this->diagDeadFrameClaims[2],
                     DPF("fe"),
                     this->diagFramingErrorsByEdge[0],
                     this->diagFramingErrorsByEdge[1],
                     this->diagFramingErrorsByEdge[2],
                     DPF("tx"),
                     LNEdgeTransport.txFrameCount(0),
                     LNEdgeTransport.txFrameCount(1),
                     LNEdgeTransport.txFrameCount(2),
                     DPF("lack"),
                     this->diagLinkAcksSent,
                     DPF("lrtx"),
                     this->arqLink.retransmitCount(),
                     DPF("lto"),
                     this->arqLink.ackTimeoutCount(),
                     DPF("ldup"),
                     this->diagDupFramesDropped[0],
                     this->diagDupFramesDropped[1],
                     this->diagDupFramesDropped[2],
                     DPF("dtype"),
                     this->diagLastDupType,
                     DPF("dcrc"),
                     this->diagLastDupCrc,
                     DPF("mb"),
                     this->diagMultiBitWakeMasks,
                     DPF("self"),
                     this->diagSelfClaims,
                     DPF("feRaw"),
                     LNEdgeTransport.framingErrorStamp(),
                     DPF("dor"),
                     LNEdgeTransport.overrunErrorStamp(),
                     DPF("ign"),
                     this->wakeClaimIgnoredCount
        ));
    }

#endif

void LightnetPanel::tick(uint32_t nowMs)
{
    this->pollWake(nowMs);
    this->pollBytes(nowMs);
    this->driver.tick(nowMs);

    #if DEBUG
        uint8_t heldEdge = this->receiver.claimedEdge();
    #endif

    this->receiver.tick(nowMs);

    #if DEBUG

        // The only way a claim ends inside receiver.tick() is a timeout release -- ledger it as
        // phantom (claimed but never received a byte) or dead-frame (bytes arrived, frame never
        // completed).
        if (heldEdge != Lightnet::EdgeFrameReceiver::NO_EDGE
            && this->receiver.claimedEdge() == Lightnet::EdgeFrameReceiver::NO_EDGE) {
            this->noteClaimReleasedByTimeout(heldEdge);
        }

    #endif

    // receiver.tick() may have released a stalled claim -- re-enable the wakes if so.
    this->syncWakeInterruptSuppression();
    this->pollProbeClaim(nowMs);
    this->animPlayer.tick((uint16_t)nowMs);

    if (this->animPlayer.takeDirty()) {
        Protocol::ColorRGB c = this->animPlayer.currentColor();

        this->rgbController.color(c.r, c.g, c.b);
    }

    // Bytes can land during the above -- drain them before any bit-banged debug output.
    if (LNEdgeTransport.available()) {
        this->pollWake(nowMs);
        this->pollBytes(nowMs);
        LNEdgeTransport.pollTrunkActivityLed(nowMs);

        return;
    }

    this->flushIdleDebugLogs(nowMs);
    #if DEBUG
        this->flushHeartbeatLog(nowMs);
        this->flushRelayDiag(nowMs);
    #endif
    LNEdgeTransport.pollTrunkActivityLed(nowMs);
}

void LightnetPanel::handlePacket(const Protocol::PacketMeta *packet, uint8_t size)
{
    (void)size;

    switch (packet->header.type) {
        case Protocol::PACKET_TURN_ON_OFF:
            this->handleTurnOnOff((const Protocol::PacketTurnOnOff *)packet);
            break;

        case Protocol::PACKET_SET_COLOR:
            this->handleSetColor((const Protocol::PacketSetColor *)packet);
            break;

        case Protocol::PACKET_PANEL_CONFIGURATION:
            this->handlePanelConfiguration((const Protocol::PacketPanelConfiguration *)packet);
            break;

        case Protocol::PACKET_ANIMATION_PREPARE:
            this->handleAnimationPrepare((const Protocol::PacketAnimationPrepare *)packet);
            break;

        case Protocol::PACKET_ANIMATION_START:
            this->handleAnimationStart((const Protocol::PacketAnimationStart *)packet);
            break;

        case Protocol::PACKET_ANIMATION_CONTROL:
            this->handleAnimationControl((const Protocol::PacketAnimationControl *)packet);
            break;

        case Protocol::PACKET_ANIMATION_UPDATE_PARAMS:
            this->handleAnimationUpdateParams((const Protocol::PacketAnimationUpdateParams *)packet);
            break;

        case Protocol::PACKET_SET_PALETTE:
            this->handleSetPalette((const Protocol::PacketSetPalette *)packet);
            break;

        case Protocol::PACKET_SET_BASE_COLORS:
            this->handleSetBaseColors((const Protocol::PacketSetBaseColors *)packet);
            break;

        case Protocol::PACKET_SET_GLOBAL_BRIGHTNESS:
            this->handleSetGlobalBrightness((const Protocol::PacketSetGlobalBrightness *)packet);
            break;

        case Protocol::PACKET_SET_BACKGROUND:
            this->animPlayer.setBackground(((const Protocol::PacketSetBackground *)packet)->color);
            break;

        case Protocol::PACKET_RESET_DEVICE:
            PORTD |= (1 << PD6);
            Lightnet::delay(10);
            PORTD &= ~(1 << PD6);
            Lightnet::delay(10);
            PORTD |= (1 << PD6);
            Lightnet::delay(10);
            PORTD &= ~(1 << PD6);

            MCUSR  = MCUSR & 0b11110111;
            WDTCSR = WDTCSR | 0b00011000;
            WDTCSR = 0b00000001;
            WDTCSR = WDTCSR | 0b01000000;
            MCUSR  = MCUSR & 0b11110111;
            Lightnet::delay(50);
            break;

        case Protocol::PACKET_ENTER_BOOTLOADER:
            this->handleEnterBootloader((const Protocol::PacketEnterBootloader *)packet);
            break;

        case Protocol::PACKET_FETCH_STATE:
            this->handleFetchState();
            break;

        default:
            break;
    }
}

void LightnetPanel::handleTurnOnOff(const Protocol::PacketTurnOnOff *packet)
{
    if (packet->on) {
        this->rgbController.turnOn();
    } else {
        this->rgbController.turnOff();
    }

    this->sendAck();
}

void LightnetPanel::handleSetColor(const Protocol::PacketSetColor *packet)
{
    // Route through the player so it stays the single colour authority.
    this->animPlayer.setColorDirect(packet->color.rgb);
}

void LightnetPanel::handlePanelConfiguration(const Protocol::PacketPanelConfiguration *packet)
{
    this->rgbController.gammaCorrection(packet->useGammaCorrection);
    this->rgbController.setColorTemperature(packet->colorTemperature);
    this->rgbController.setColorCorrection(packet->colorCorrection);

    this->sendAck();
}

void LightnetPanel::handleAnimationPrepare(const Protocol::PacketAnimationPrepare *packet)
{
    this->animPlayer.prepare(packet);
}

void LightnetPanel::handleAnimationStart(const Protocol::PacketAnimationStart *packet)
{
    this->animPlayer.start(packet->seq_id, packet->group_id, (uint16_t)Lightnet::millis());
}

void LightnetPanel::handleAnimationControl(const Protocol::PacketAnimationControl *packet)
{
    this->animPlayer.control(packet->cmd, packet->group_id, (uint16_t)Lightnet::millis());
}

void LightnetPanel::handleAnimationUpdateParams(const Protocol::PacketAnimationUpdateParams *packet)
{
    this->animPlayer.updateParams(
        packet->seq_id,
        packet->group_id,
        packet->param_type,
        packet->value,
        packet->transitionMs,
        (uint16_t)Lightnet::millis()
    );
}

void LightnetPanel::handleSetPalette(const Protocol::PacketSetPalette *packet)
{
    this->animPlayer.setPalette(packet->stops, packet->count);
}

void LightnetPanel::handleSetBaseColors(const Protocol::PacketSetBaseColors *packet)
{
    this->animPlayer.setBaseColors(packet->colors);
}

void LightnetPanel::handleSetGlobalBrightness(const Protocol::PacketSetGlobalBrightness *packet)
{
    this->rgbController.globalBrightness(packet->value);
}

void LightnetPanel::handleFetchState()
{
    uint16_t myIndex = this->driver.assignedPanelIndex();
    Protocol::PacketPanelState reply =
        Protocol::makePacket<Protocol::PacketPanelState>(Protocol::PACKET_FETCH_STATE_REPLY, myIndex);

    reply.panelState.panelIndex = myIndex;
    reply.panelState.state      = this->rgbController.on() ? 1 : 0;
    reply.panelState.color      = this->rgbController.color();

    // Via the ARQ decorator: replies are acked types, so their first hop upstream gets the
    // same link-level protection as every relayed hop above it.
    this->arqLink.sendOnEdge(this->discovery.parentEdge(), Protocol::packetMeta(reply), sizeof(reply));
}

void LightnetPanel::sendAck()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK, this->driver.assignedPanelIndex());

    this->arqLink.sendOnEdge(this->discovery.parentEdge(), &ack, sizeof(ack));
}

void LightnetPanel::handleEnterBootloader(const Protocol::PacketEnterBootloader *packet)
{
    if (packet->token != BootloaderBridge::ENTRY_TOKEN) {
        DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTLN(
                     DPF("[BOOTLOADER] token mismatch got"),
                     packet->token,
                     DPF("want"),
                     BootloaderBridge::ENTRY_TOKEN
        ));

        return;
    }

    DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTLN(DPF("[BOOTLOADER] entering, index"), this->driver.assignedPanelIndex()));

    // The relay bootloader (lib/Lightnet/Panel/bootloader/RelayBootloader.cpp) has no topology
    // of its own -- it needs this panel's own assigned index and parent edge handed to it before
    // the jump (see BootloaderProtocol.hpp for why).
    BootloaderBridge::prepareAndReset(this->discovery.parentEdge(), this->driver.assignedPanelIndex());
    // execution never reaches here
}

LightnetPanel LNPanel;
#endif  // LIGHTNET_TARGET_CONTROLLER
