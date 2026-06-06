#include "AnimationRunner.hpp"
#include "RunnerMath.hpp"
#include "../Panels/PanelsController.hpp"
#include "Arduino.h"

namespace Lightnet {
    // Shared helper: emit a SET_COLOR(color × brightness/255) to one panel.
    static void sendScaledColor(uint8_t address, const Protocol::ColorRGB& color, uint8_t brightness)
    {
        Protocol::PacketSetColor colorPkt;

        Protocol::setPacketMeta(&colorPkt.meta, Protocol::PACKET_SET_COLOR);
        colorPkt.color.rgb = {
            (uint8_t)((uint16_t)color.r * brightness / 255),
            (uint8_t)((uint16_t)color.g * brightness / 255),
            (uint8_t)((uint16_t)color.b * brightness / 255)
        };
        LNBus.sendPacketNack(address, &colorPkt, sizeof(colorPkt), Protocol::PACKET_SET_COLOR);
    }

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

    void WaveRunner::load(const uint8_t *addrs, const uint8_t *coordSrc, uint8_t n)
    {
        for (uint8_t i = 0; i < n; i++) {
            panelAddresses[i] = addrs[i];
            coord[i]          = coordSrc ? coordSrc[i] : i;
            lastBrightness[i] = 0;
        }
    }

    WaveRunner::WaveRunner(
        uint8_t            groupId,
        const uint8_t *    _panelAddresses,
        const uint8_t *    _coord,
        uint8_t            _panelCount,
        uint8_t            _maxCoord,
        uint16_t           _durationMs,
        uint8_t            _waveWidth,
        Protocol::ColorRGB _color
    )
        : AnimationRunner(groupId), panelCount(_panelCount < MAX_PANELS ? _panelCount : MAX_PANELS),
        maxCoord(_maxCoord), durationMs(_durationMs), startMs(0), waveWidth(_waveWidth),
        color(_color), finished(false)
    {
        load(_panelAddresses, _coord, panelCount);
    }

    WaveRunner::WaveRunner(
        uint8_t            groupId,
        const uint8_t *    _panelAddresses,
        uint8_t            _panelCount,
        uint16_t           _durationMs,
        uint8_t            _waveWidth,
        Protocol::ColorRGB _color
    )
        : AnimationRunner(groupId), panelCount(_panelCount < MAX_PANELS ? _panelCount : MAX_PANELS),
        maxCoord(0), durationMs(_durationMs), startMs(0), waveWidth(_waveWidth),
        color(_color), finished(false)
    {
        maxCoord = panelCount ? (uint8_t)(panelCount - 1) : 0;
        load(_panelAddresses, nullptr, panelCount);
    }

    void WaveRunner::tick(uint32_t nowMs)
    {
        if (finished) return;

        if (startMs == 0) {
            startMs = nowMs;
        }

        uint32_t elapsed = nowMs - startMs;
        float t       = durationMs ? (float)elapsed / (float)durationMs : 1.0f;
        float center  = waveCenterAt(t, maxCoord);

        for (uint8_t i = 0; i < panelCount; i++) {
            uint8_t brightness_val = waveBrightness((float)coord[i], center, waveWidth);

            if (brightness_val != lastBrightness[i]) {
                sendScaledColor(panelAddresses[i], color, brightness_val);
                lastBrightness[i] = brightness_val;
            }
        }

        if (elapsed >= durationMs) {
            finished = true;
        }
    }

    bool WaveRunner::isFinished() const
    {
        return finished;
    }

    // ============================================================================
    // RippleRunner
    // ============================================================================

    void RippleRunner::load(const uint8_t *addrs, const uint8_t *coordSrc, uint8_t n)
    {
        for (uint8_t i = 0; i < n; i++) {
            panelAddresses[i] = addrs[i];
            coord[i]          = coordSrc ? coordSrc[i] : i;
            lastBrightness[i] = 0;
        }
    }

    RippleRunner::RippleRunner(
        uint8_t            groupId,
        const uint8_t *    _panelAddresses,
        const uint8_t *    _coord,
        uint8_t            _panelCount,
        uint8_t            _maxCoord,
        uint16_t           _durationMs,
        uint8_t            _rippleWidth,
        Protocol::ColorRGB _color
    )
        : AnimationRunner(groupId), panelCount(_panelCount < MAX_PANELS ? _panelCount : MAX_PANELS),
        maxCoord(_maxCoord), durationMs(_durationMs), startMs(0), rippleWidth(_rippleWidth),
        color(_color), finished(false)
    {
        load(_panelAddresses, _coord, panelCount);
    }

    RippleRunner::RippleRunner(
        uint8_t            groupId,
        const uint8_t *    _panelAddresses,
        uint8_t            _panelCount,
        uint8_t            _originPanel,
        uint16_t           _durationMs,
        uint8_t            _rippleWidth,
        Protocol::ColorRGB _color
    )
        : AnimationRunner(groupId), panelCount(_panelCount < MAX_PANELS ? _panelCount : MAX_PANELS),
        maxCoord(0), durationMs(_durationMs), startMs(0), rippleWidth(_rippleWidth),
        color(_color), finished(false)
    {
        // Legacy origin in list space: coord[i] = |i - originPanel|.
        uint8_t mx = 0;

        for (uint8_t i = 0; i < panelCount; i++) {
            panelAddresses[i] = _panelAddresses[i];
            lastBrightness[i] = 0;

            uint8_t d = (i >= _originPanel) ? (uint8_t)(i - _originPanel) : (uint8_t)(_originPanel - i);

            coord[i] = d;

            if (d > mx) mx = d;
        }

        maxCoord = mx;
    }

    void RippleRunner::tick(uint32_t nowMs)
    {
        if (finished) return;

        if (startMs == 0) {
            startMs = nowMs;
        }

        uint32_t elapsed = nowMs - startMs;
        float t       = durationMs ? (float)elapsed / (float)durationMs : 1.0f;
        float radius  = rippleRadiusAt(t, maxCoord);

        for (uint8_t i = 0; i < panelCount; i++) {
            uint8_t brightness_val = rippleBrightness((float)coord[i], radius, rippleWidth);

            if (brightness_val != lastBrightness[i]) {
                sendScaledColor(panelAddresses[i], color, brightness_val);
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

    void ChaseRunner::load(const uint8_t *addrs, const uint8_t *coordSrc, uint8_t n)
    {
        for (uint8_t i = 0; i < n; i++) {
            panelAddresses[i] = addrs[i];
            coord[i]          = coordSrc ? coordSrc[i] : i;
            lastBrightness[i] = 0;
        }
    }

    ChaseRunner::ChaseRunner(
        uint8_t            groupId,
        const uint8_t *    _panelAddresses,
        const uint8_t *    _coord,
        uint8_t            _panelCount,
        uint8_t            _maxCoord,
        uint16_t           _durationMs,
        Protocol::ColorRGB _color
    )
        : AnimationRunner(groupId), panelCount(_panelCount < MAX_PANELS ? _panelCount : MAX_PANELS),
        maxCoord(_maxCoord), durationMs(_durationMs), startMs(0), color(_color), finished(false)
    {
        load(_panelAddresses, _coord, panelCount);
    }

    ChaseRunner::ChaseRunner(
        uint8_t            groupId,
        const uint8_t *    _panelAddresses,
        uint8_t            _panelCount,
        uint16_t           _durationMs,
        Protocol::ColorRGB _color
    )
        : AnimationRunner(groupId), panelCount(_panelCount < MAX_PANELS ? _panelCount : MAX_PANELS),
        maxCoord(0), durationMs(_durationMs), startMs(0), color(_color), finished(false)
    {
        maxCoord = panelCount ? (uint8_t)(panelCount - 1) : 0;
        load(_panelAddresses, nullptr, panelCount);
    }

    void ChaseRunner::tick(uint32_t nowMs)
    {
        if (finished) return;

        if (startMs == 0) {
            startMs = nowMs;
        }

        uint32_t elapsed = nowMs - startMs;
        float t       = durationMs ? (float)elapsed / (float)durationMs : 1.0f;
        uint8_t lit     = chaseLitCoord(t, maxCoord);

        for (uint8_t i = 0; i < panelCount; i++) {
            uint8_t brightness_val = chaseBrightness(coord[i], lit);

            if (brightness_val != lastBrightness[i]) {
                sendScaledColor(panelAddresses[i], color, brightness_val);
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
