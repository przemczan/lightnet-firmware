#include "AnimationRunner.hpp"
#include "../Common/ProtocolMeta.hpp"
#include "RunnerMath.hpp"

namespace Lightnet {
    // Emit a SET_COLOR(color × brightness/255) to one panel through the sink.
    void AnimationRunner::sendScaledColor(uint8_t address, const Protocol::ColorRGB& color, uint8_t brightness)
    {
        if (!sink) return;

        Protocol::PacketSetColor colorPkt = Protocol::makePacket<Protocol::PacketSetColor>(Protocol::PACKET_SET_COLOR);

        colorPkt.color.rgb = {
            (uint8_t)((uint16_t)color.r * brightness / 255),
            (uint8_t)((uint16_t)color.g * brightness / 255),
            (uint8_t)((uint16_t)color.b * brightness / 255)
        };
        sink->send(address, Protocol::packetMeta(colorPkt), sizeof(colorPkt), false);
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
        float center  = waveCenterAt(t, maxCoord, waveWidth);

        for (uint8_t i = 0; i < panelCount; i++) {
            uint8_t brightness_val = waveBrightness((float)coord[i], center, waveWidth);

            if (brightness_val != lastBrightness[i]) {
                sendScaledColor(panelAddresses[i], color, brightness_val);
                lastBrightness[i] = brightness_val;
            }
        }

        if (elapsed >= durationMs) {
            // Clear any panel still lit by frame-quantisation at the trailing edge so the
            // wave leaves nothing behind (the sweep fades all panels to 0 by t=1, but the
            // last frame can land at t slightly <1, stranding a ~1-brightness residue).
            for (uint8_t i = 0; i < panelCount; i++) {
                if (lastBrightness[i] != 0) {
                    sendScaledColor(panelAddresses[i], color, 0);
                    lastBrightness[i] = 0;
                }
            }

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

    void RippleRunner::load(const uint8_t *addrs, const uint8_t *coordSrc, const uint8_t *coordFarSrc, uint8_t n)
    {
        for (uint8_t i = 0; i < n; i++) {
            panelAddresses[i] = addrs[i];
            coord[i]          = coordSrc ? coordSrc[i] : i;
            coordFar[i]       = coordFarSrc ? coordFarSrc[i] : coord[i]; // null → point model
            lastBrightness[i] = 0;
        }
    }

    RippleRunner::RippleRunner(
        uint8_t            groupId,
        const uint8_t *    _panelAddresses,
        const uint8_t *    _coord,
        const uint8_t *    _coordFar,
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
        load(_panelAddresses, _coord, _coordFar, panelCount);
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

            coord[i]    = d;
            coordFar[i] = d; // point model: near == far

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
        float radius  = rippleRadiusAt(t, maxCoord, rippleWidth);

        for (uint8_t i = 0; i < panelCount; i++) {
            uint8_t brightness_val = rippleBandBrightness((float)coord[i], (float)coordFar[i], radius, rippleWidth);

            if (brightness_val != lastBrightness[i]) {
                sendScaledColor(panelAddresses[i], color, brightness_val);
                lastBrightness[i] = brightness_val;
            }
        }

        if (elapsed >= durationMs) {
            // Clear any panel still lit by the trailing edge so the ripple leaves nothing behind.
            for (uint8_t i = 0; i < panelCount; i++) {
                if (lastBrightness[i] != 0) {
                    sendScaledColor(panelAddresses[i], color, 0);
                    lastBrightness[i] = 0;
                }
            }

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
            for (uint8_t i = 0; i < panelCount; i++) {
                if (lastBrightness[i] != 0) {
                    sendScaledColor(panelAddresses[i], color, 0);
                    lastBrightness[i] = 0;
                }
            }

            finished = true;
        }
    }

    bool ChaseRunner::isFinished() const
    {
        return finished;
    }
}  // namespace Lightnet
