#include "AnimationRunner.hpp"
#include "../Controller/PanelsController.hpp"
#include "Arduino.h"

namespace Lightnet {

// ============================================================================
// PanelLocalRunner
// ============================================================================

PanelLocalRunner::PanelLocalRunner(uint8_t groupId, uint16_t _durationMs)
    : AnimationRunner(groupId), durationMs(_durationMs), startMs(0), finished(false)
{
}

void PanelLocalRunner::tick(uint32_t nowMs)
{
    if (finished) return;

    if (startMs == 0) {
        startMs = nowMs;
    }

    uint32_t elapsed = nowMs - startMs;

    if (durationMs > 0 && elapsed >= durationMs) {
        finished = true;
    }
}

bool PanelLocalRunner::isFinished() const
{
    return finished;
}

// ============================================================================
// WaveRunner
// ============================================================================

WaveRunner::WaveRunner(uint8_t groupId, const uint8_t* _panelAddresses, uint8_t _panelCount,
                       uint16_t _durationMs, uint8_t _waveWidth, Protocol::ColorRGB _color)
    : AnimationRunner(groupId), panelCount(_panelCount), durationMs(_durationMs),
      startMs(0), waveWidth(_waveWidth), color(_color), finished(false)
{
    for (uint8_t i = 0; i < _panelCount && i < 30; i++) {
        panelAddresses[i] = _panelAddresses[i];
    }
}

void WaveRunner::tick(uint32_t nowMs)
{
    if (finished) return;

    if (startMs == 0) {
        startMs = nowMs;
    }

    uint32_t elapsed = nowMs - startMs;

    // Wave travels from -1.5 to panelCount+0.5 units over durationMs
    // So wave center position = -1.5 + (panelCount + 2) * (elapsed / durationMs)
    float waveCenter = -1.5f + (float)(panelCount + 2) * (float)elapsed / (float)durationMs;

    // Send SET_BRIGHTNESS to each panel based on distance from wave center
    Protocol::PacketSetBrightness brightness;
    Protocol::setPacketMeta(&brightness.meta, Protocol::PACKET_SET_BRIGHTNESS);

    for (uint8_t i = 0; i < panelCount; i++) {
        uint8_t brightness_val = computeWaveBrightness((float)i, waveCenter);

        // Only send if changed (delta optimization)
        static uint8_t lastBrightness[30] = {0};
        if (brightness_val != lastBrightness[i]) {
            brightness.brightness = brightness_val;
            LNBus.sendPacketNack(panelAddresses[i], &brightness, sizeof(brightness), Protocol::PACKET_SET_BRIGHTNESS);
            lastBrightness[i] = brightness_val;
        }
    }

    // Check if finished
    if (elapsed >= durationMs) {
        finished = true;
    }
}

bool WaveRunner::isFinished() const
{
    return finished;
}

uint8_t WaveRunner::computeWaveBrightness(float panelPos, float waveCenter)
{
    // Triangular wave envelope with half-width of waveWidth
    float distance = fabsf(panelPos - waveCenter);
    float half_width = (float)waveWidth / 2.0f;

    if (distance >= half_width) {
        return 0;
    }

    // Linear falloff: brightness = 255 * (1 - distance / half_width)
    float brightness_f = 255.0f * (1.0f - distance / half_width);
    uint8_t brightness_u8 = (uint8_t)brightness_f;

    return brightness_u8;
}

// ============================================================================
// RippleRunner
// ============================================================================

RippleRunner::RippleRunner(uint8_t groupId, const uint8_t* _panelAddresses, uint8_t _panelCount,
                           uint8_t _originPanel, uint16_t _durationMs, uint8_t _rippleWidth,
                           Protocol::ColorRGB _color)
    : AnimationRunner(groupId), panelCount(_panelCount), originPanel(_originPanel),
      durationMs(_durationMs), startMs(0), rippleWidth(_rippleWidth),
      color(_color), finished(false)
{
    for (uint8_t i = 0; i < _panelCount && i < 30; i++) {
        panelAddresses[i] = _panelAddresses[i];
    }
}

void RippleRunner::tick(uint32_t nowMs)
{
    if (finished) return;

    if (startMs == 0) {
        startMs = nowMs;
    }

    uint32_t elapsed = nowMs - startMs;

    // Ripple expands from originPanel, maxRadius = panelCount + 1
    float maxRadius = (float)(panelCount + 1);
    float rippleRadius = maxRadius * (float)elapsed / (float)durationMs;

    Protocol::PacketSetBrightness brightness;
    Protocol::setPacketMeta(&brightness.meta, Protocol::PACKET_SET_BRIGHTNESS);

    for (uint8_t i = 0; i < panelCount; i++) {
        float distance = fabsf((float)i - (float)originPanel);
        float ringWidth = (float)rippleWidth / 2.0f;

        // Brightness based on proximity to ripple ring
        float dist_from_ring = fabsf(distance - rippleRadius);
        uint8_t brightness_val = 0;

        if (dist_from_ring < ringWidth) {
            brightness_val = (uint8_t)(255.0f * (1.0f - dist_from_ring / ringWidth));
        }

        static uint8_t lastBrightness[30] = {0};
        if (brightness_val != lastBrightness[i]) {
            brightness.brightness = brightness_val;
            LNBus.sendPacketNack(panelAddresses[i], &brightness, sizeof(brightness), Protocol::PACKET_SET_BRIGHTNESS);
            lastBrightness[i] = brightness_val;
        }
    }

    if (elapsed >= durationMs) {
        finished = true;
    }
}

bool RippleRunner::isFinished() const
{
    return finished;
}

// ============================================================================
// ChaseRunner
// ============================================================================

ChaseRunner::ChaseRunner(uint8_t groupId, const uint8_t* _panelAddresses, uint8_t _panelCount,
                         uint16_t _durationMs, Protocol::ColorRGB _color)
    : AnimationRunner(groupId), panelCount(_panelCount), durationMs(_durationMs),
      startMs(0), color(_color), finished(false)
{
    for (uint8_t i = 0; i < _panelCount && i < 30; i++) {
        panelAddresses[i] = _panelAddresses[i];
    }
}

void ChaseRunner::tick(uint32_t nowMs)
{
    if (finished) return;

    if (startMs == 0) {
        startMs = nowMs;
    }

    uint32_t elapsed = nowMs - startMs;

    // Position: which panel is lit (0 to panelCount-1)
    uint8_t litPanel = (uint8_t)((float)elapsed / (float)durationMs * (float)panelCount) % panelCount;

    Protocol::PacketSetBrightness brightness;
    Protocol::setPacketMeta(&brightness.meta, Protocol::PACKET_SET_BRIGHTNESS);

    for (uint8_t i = 0; i < panelCount; i++) {
        uint8_t brightness_val = (i == litPanel) ? 255 : 0;

        static uint8_t lastBrightness[30] = {0};
        if (brightness_val != lastBrightness[i]) {
            brightness.brightness = brightness_val;
            LNBus.sendPacketNack(panelAddresses[i], &brightness, sizeof(brightness), Protocol::PACKET_SET_BRIGHTNESS);
            lastBrightness[i] = brightness_val;
        }
    }

    if (elapsed >= durationMs) {
        finished = true;
    }
}

bool ChaseRunner::isFinished() const
{
    return finished;
}

}  // namespace Lightnet
