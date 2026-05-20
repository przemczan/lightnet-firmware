#include "AnimationScheduler.hpp"
#include "AnimationRunner.hpp"
#include "../Controller/PanelsController.hpp"

namespace Lightnet {

AnimationScheduler::AnimationScheduler(uint8_t _maxPanels)
    : maxPanels(_maxPanels), lastFrameMs(0), nextSeqId(1)
{
    activeRunners = new List<AnimationRunner*>();
    panelStates = new AnimationRecord[maxPanels];
    memset(panelStates, 0, sizeof(AnimationRecord) * maxPanels);
}

AnimationScheduler::~AnimationScheduler()
{
    delete activeRunners;
    delete[] panelStates;
}

void AnimationScheduler::initialize()
{
    // Nothing special needed on first init
}

void AnimationScheduler::playOnPanels(uint8_t group_id, uint8_t animType, uint8_t flags, uint16_t durationMs,
                                       const ColorRef& colorFrom, const ColorRef& colorTo,
                                       uint8_t brightnessFrom, uint8_t brightnessTo,
                                       uint8_t param1, uint8_t param2,
                                       const uint8_t* panelAddresses, uint8_t panelCount)
{
    // Build PREPARE packet
    Protocol::PacketAnimationPrepare prepare;
    Protocol::setPacketMeta(&prepare.meta, Protocol::PACKET_ANIMATION_PREPARE);
    prepare.animType = animType;
    prepare.group_id = group_id;
    prepare.flags = flags;
    prepare.transitionMs = 0;  // TODO: make configurable
    prepare.durationMs = durationMs;
    prepare.colorFrom = colorFrom;
    prepare.colorTo = colorTo;
    prepare.brightnessFrom = brightnessFrom;
    prepare.brightnessTo = brightnessTo;
    prepare.param1 = param1;
    prepare.param2 = param2;

    // Send PREPARE to each panel, retrying on failure so a single bus glitch
    // doesn't silently leave a panel with no animation queued.
    for (uint8_t i = 0; i < panelCount; i++) {
        uint8_t addr = panelAddresses[i];

        uint8_t err = 1;
        for (uint8_t attempt = 0; attempt < 3 && err != 0; attempt++) {
            if (attempt > 0) {
                delayMicroseconds(100);
                Serial.printf("play failed, retrying... attempt %d", attempt);
            }
            err = LNBus.sendPacketAck(addr, &prepare, sizeof(prepare), Protocol::PACKET_ANIMATION_PREPARE);
        }

        if (addr < maxPanels) {
            panelStates[addr].animType = animType;
            panelStates[addr].groupId = group_id;
            panelStates[addr].durationMs = durationMs;
            panelStates[addr].startMs = millis();
            panelStates[addr].isController = false;
        }
    }

    // Give panels time to process their PREPARE before START arrives.
    delayMicroseconds(300);

    // Send General Call START 3 times for reliability.
    // All retries share the same seq_id so the panel's duplicate guard lets
    // through exactly one execution while absorbing the redundant copies.
    Protocol::PacketAnimationStart startPkt;
    Protocol::setPacketMeta(&startPkt.meta, Protocol::PACKET_ANIMATION_START);
    startPkt.seq_id  = nextSeqId;
    startPkt.group_id = group_id;
    nextSeqId++;
    if (nextSeqId == 0) nextSeqId = 1;

    for (uint8_t retry = 0; retry < 2; retry++) {
        LNBus.sendPacketNack(0x00, &startPkt, sizeof(startPkt), Protocol::PACKET_ANIMATION_START);
        delayMicroseconds(200);
    }
}

void AnimationScheduler::stopGroup(uint8_t group_id)
{
    // For now, we can add a method to send CONTROL/STOP to all panels in a group
    // But we'd need to know which panels are in the group
    // For MVP, just broadcast via General Call or unicast to tracked panels
    // TODO: implement group tracking if needed
}

void AnimationScheduler::pauseGroup(uint8_t group_id)
{
    // TODO: send CONTROL/PAUSE to all panels with matching groupId
}

void AnimationScheduler::resumeGroup(uint8_t group_id)
{
    // TODO: send CONTROL/RESUME to all panels with matching groupId
}

void AnimationScheduler::triggerGroup(uint8_t group_id, uint8_t value)
{
    // Send General Call UPDATE_PARAMS with TRIGGER
    sendGeneralCallUpdateParams(group_id, Lightnet::PARAM_TRIGGER, value);
}

const AnimationRecord* AnimationScheduler::getStatus(uint8_t panelAddress)
{
    if (panelAddress >= maxPanels) {
        return nullptr;
    }
    return &panelStates[panelAddress];
}

void AnimationScheduler::tick(uint32_t nowMs)
{
    // Frame gate: 60fps = 16.67ms
    const uint32_t FRAME_MS = 17;
    if ((uint32_t)(nowMs - lastFrameMs) < FRAME_MS) {
        return;
    }
    lastFrameMs = nowMs;

    // Tick all runners, then remove finished ones. Iterate in reverse so
    // removeByIndex doesn't shift unvisited entries.
    uint16_t i = activeRunners->getSize();
    while (i--) {
        AnimationRunner* runner = activeRunners->get(i);
        if (!runner) continue;
        runner->tick(nowMs);
        if (runner->isFinished()) {
            activeRunners->removeByIndex(i);
            delete runner;
        }
    }
}

void AnimationScheduler::addRunner(AnimationRunner* runner)
{
    if (runner) {
        activeRunners->push(runner);
    }
}

void AnimationScheduler::removeRunner(AnimationRunner* runner)
{
    for (uint16_t i = 0; i < activeRunners->getSize(); i++) {
        if (activeRunners->get(i) == runner) {
            activeRunners->removeByIndex(i);
            return;
        }
    }
}

void AnimationScheduler::sendGeneralCallStart(uint8_t group_id)
{
    Protocol::PacketAnimationStart start;
    Protocol::setPacketMeta(&start.meta, Protocol::PACKET_ANIMATION_START);
    start.seq_id = nextSeqId;
    start.group_id = group_id;

    // Send to General Call address (0x00)
    LNBus.sendPacketNack(0x00, &start, sizeof(start), Protocol::PACKET_ANIMATION_START);

    nextSeqId++;
    if (nextSeqId == 0) nextSeqId = 1;  // skip 0
}

void AnimationScheduler::sendGeneralCallUpdateParams(uint8_t group_id, uint8_t param_type, uint8_t value)
{
    Protocol::PacketAnimationUpdateParams params;
    Protocol::setPacketMeta(&params.meta, Protocol::PACKET_ANIMATION_UPDATE_PARAMS);
    params.seq_id = nextSeqId;
    params.group_id = group_id;
    params.param_type = param_type;
    params.value = value;
    params.transitionMs = 10;  // TODO: make configurable

    // Send to General Call address (0x00)
    LNBus.sendPacketNack(0x00, &params, sizeof(params), Protocol::PACKET_ANIMATION_UPDATE_PARAMS);

    nextSeqId++;
    if (nextSeqId == 0) nextSeqId = 1;
}

void AnimationScheduler::sendControlToPanels(uint8_t group_id, uint8_t cmd, const uint8_t* panelAddresses, uint8_t panelCount)
{
    Protocol::PacketAnimationControl control;
    Protocol::setPacketMeta(&control.meta, Protocol::PACKET_ANIMATION_CONTROL);
    control.cmd = cmd;

    for (uint8_t i = 0; i < panelCount; i++) {
        LNBus.sendPacketAck(panelAddresses[i], &control, sizeof(control), Protocol::PACKET_ANIMATION_CONTROL);
    }
}

// ============================================================================
// RGB convenience overload — wraps inline RGB in ColorRefs
// ============================================================================

void AnimationScheduler::playOnPanels(uint8_t group_id, uint8_t animType, uint8_t flags, uint16_t durationMs,
                                       const Protocol::ColorRGB& colorFrom, const Protocol::ColorRGB& colorTo,
                                       uint8_t brightnessFrom, uint8_t brightnessTo,
                                       uint8_t param1, uint8_t param2,
                                       const uint8_t* panelAddresses, uint8_t panelCount)
{
    ColorRef from = ColorRef_rgb(colorFrom.r, colorFrom.g, colorFrom.b);
    ColorRef to   = ColorRef_rgb(colorTo.r,   colorTo.g,   colorTo.b);
    playOnPanels(group_id, animType, flags, durationMs, from, to,
                 brightnessFrom, brightnessTo, param1, param2,
                 panelAddresses, panelCount);
}

// ============================================================================
// Appearance broadcasts
// ============================================================================

void AnimationScheduler::broadcastPalette(const GradientStop* stops, uint8_t count)
{
    if (count == 0) return;
    if (count > PALETTE_STOPS) count = PALETTE_STOPS;

    Protocol::PacketSetPalette pkt;
    Protocol::setPacketMeta(&pkt.meta, Protocol::PACKET_SET_PALETTE);
    pkt.count = count;
    memset(pkt.stops, 0, sizeof(pkt.stops));
    for (uint8_t i = 0; i < count; i++) {
        pkt.stops[i] = stops[i];
    }
    // General Call — all panels apply simultaneously.
    LNBus.sendPacketNack(0x00, &pkt, sizeof(pkt), Protocol::PACKET_SET_PALETTE);
}

void AnimationScheduler::unicastPaletteToPanels(const GradientStop* stops, uint8_t count,
                                                 const uint8_t* panelAddresses, uint8_t panelCount)
{
    if (count == 0) return;
    if (count > PALETTE_STOPS) count = PALETTE_STOPS;

    Protocol::PacketSetPalette pkt;
    Protocol::setPacketMeta(&pkt.meta, Protocol::PACKET_SET_PALETTE);
    pkt.count = count;
    memset(pkt.stops, 0, sizeof(pkt.stops));
    for (uint8_t i = 0; i < count; i++) {
        pkt.stops[i] = stops[i];
    }
    for (uint8_t i = 0; i < panelCount; i++) {
        LNBus.sendPacketAck(panelAddresses[i], &pkt, sizeof(pkt), Protocol::PACKET_SET_PALETTE);
    }
}

void AnimationScheduler::broadcastBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT])
{
    Protocol::PacketSetBaseColors pkt;
    Protocol::setPacketMeta(&pkt.meta, Protocol::PACKET_SET_BASE_COLORS);
    for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
        pkt.colors[i] = colors[i];
    }
    LNBus.sendPacketNack(0x00, &pkt, sizeof(pkt), Protocol::PACKET_SET_BASE_COLORS);
}

void AnimationScheduler::broadcastGlobalBrightness(uint8_t value)
{
    Protocol::PacketSetGlobalBrightness pkt;
    Protocol::setPacketMeta(&pkt.meta, Protocol::PACKET_SET_GLOBAL_BRIGHTNESS);
    pkt.value = value;
    LNBus.sendPacketNack(0x00, &pkt, sizeof(pkt), Protocol::PACKET_SET_GLOBAL_BRIGHTNESS);
}

}  // namespace Lightnet
