#include "SceneWriter.hpp"
#include "../../Utils/SimpleJson.hpp"
#include "../Common/AnimationTypes.hpp"
#include "../Common/ColorRef.hpp"
#include "PanelField.hpp"
#include "ScenePlayer.hpp"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Lightnet {
    namespace {
        static size_t append(char *buf, size_t cap, size_t pos, const char *fmt, ...)
        {
            if (pos >= cap) return pos;

            va_list ap;

            va_start(ap, fmt);

            int n = vsnprintf(buf + pos, cap - pos, fmt, ap);

            va_end(ap);

            if (n < 0) return pos;

            return pos + (size_t)n;
        }

        static const char *animTypeWireName(uint8_t t)
        {
            switch (t) {
                case ANIM_SOLID:       return "SOLID";
                case ANIM_FADE:        return "FADE";
                case ANIM_TRANSITION:  return "TRANSITION";
                case ANIM_BREATHE:     return "BREATHE";
                case ANIM_PULSE:       return "PULSE";
                case ANIM_BLINK:       return "BLINK";
                case ANIM_HUE_CYCLE:   return "HUE_CYCLE";
                case ANIM_STROBE:      return "STROBE";
                case ANIM_REACTIVE:    return "REACTIVE";
                case RUN_WAVE:         return "WAVE";
                case RUN_RIPPLE:       return "RIPPLE";
                case RUN_CHASE:        return "CHASE";
                case RUN_WHEEL:        return "WHEEL";
                case RUN_BOUNCE:       return "BOUNCE";
                case RUN_RAIN:         return "RAIN";
                case RUN_SPARKLE:      return "SPARKLE";
                case RUN_MATRIX:       return "MATRIX";
                default:               return nullptr;
            }
        }

        static const char *animateTargetName(uint8_t t)
        {
            switch (t) {
                case TARGET_DIM:        return "dim";
                case TARGET_DESATURATE: return "desaturate";
                case TARGET_HUE:        return "hue";
                case TARGET_INVERT:     return "invert";
                case TARGET_BRIGHTEN:   return "brighten";
                case TARGET_SATURATE:   return "saturate";
                default:                return nullptr;
            }
        }

        static const char *blendWireName(uint8_t blend)
        {
            switch (blend) {
                case COMPOSE_OPAQUE:     return "opaque";
                case COMPOSE_ADD:        return "add";
                case COMPOSE_MAX:        return "max";
                case COMPOSE_MULTIPLY:   return "multiply";
                case COMPOSE_SCREEN:     return "screen";
                case COMPOSE_DARKEN:     return "darken";
                case COMPOSE_OVERLAY:    return "overlay";
                case COMPOSE_DIFFERENCE: return "difference";
                case COMPOSE_SUBTRACT:   return "subtract";
                default:                 return nullptr;
            }
        }

        static size_t writeColorRef(char *buf, size_t cap, size_t pos, const ColorRef& c)
        {
            if (c.kind == COLORREF_RGB) {
                char hex[8];

                jsonFormatHex(c.rgb.r, c.rgb.g, c.rgb.b, hex);

                return append(buf, cap, pos, "\"%s\"", hex);
            }

            if (c.kind == COLORREF_PALETTE) {
                return append(buf, cap, pos, "{\"palette\":%u}", (unsigned)c.palette.pos);
            }

            return append(buf, cap, pos, "{\"useColor\":%u}", (unsigned)c.useColor.slot);
        }

        static size_t writePanelSelector(char *buf, size_t cap, size_t pos, const PanelSelector& sel)
        {
            if (sel.len == 0 || (sel.len == 1 && sel.ops[0] == SEL_ALL)) {
                return append(buf, cap, pos, "\"all\"");
            }

            if (sel.len >= 2 && sel.ops[0] == SEL_INDICES) {
                uint8_t n     = sel.ops[1];
                uint8_t base  = 2;
                bool complement = ((sel.len > base + n) && (sel.ops[base + n] == SEL_NOT));

                if (complement) {
                    pos = append(buf, cap, pos, "{\"exclude\":[");

                    for (uint8_t i = 0; i < n; i++) {
                        pos = append(buf, cap, pos, "%s%u", i ? "," : "", (unsigned)sel.ops[base + i]);
                    }

                    return append(buf, cap, pos, "]}");
                }

                pos = append(buf, cap, pos, "[");

                for (uint8_t i = 0; i < n; i++) {
                    pos = append(buf, cap, pos, "%s%u", i ? "," : "", (unsigned)sel.ops[base + i]);
                }

                return append(buf, cap, pos, "]");
            }

            return append(buf, cap, pos, "\"all\"");
        }

        static size_t writeRunnerSource(
            char *           buf,
            size_t           cap,
            size_t           pos,
            const SceneStep& step
        )
        {
            uint8_t kind = step.params[RUNNER_PARAM_SRC_KIND];

            if (kind == SRC_ROOT) return append(buf, cap, pos, "\"source\":\"root\"");

            if (kind == SRC_LEAVES) return append(buf, cap, pos, "\"source\":\"leaves\"");

            if (kind == SRC_ALL) return append(buf, cap, pos, "\"source\":\"all\"");

            if (kind == SRC_PANEL) {
                return append(buf, cap, pos, "\"source\":\"panel:%u\"",
                              (unsigned)step.params[RUNNER_PARAM_SRC_ARG]);
            }

            return pos;
        }

        static size_t writeStep(char *buf, size_t cap, size_t pos, const SceneStep& step)
        {
            const char *name = animTypeWireName(step.animType);

            if (!name) return pos;

            pos = append(buf, cap, pos, "{");

            if (isRunnerType(step.animType)) {
                pos = append(buf, cap, pos, "\"runner\":\"%s\"", name);
            } else {
                pos = append(buf, cap, pos, "\"type\":\"%s\"", name);
            }

            if (step.colorFrom.kind != COLORREF_RGB || step.colorFrom.rgb.r != 0 ||
                step.colorFrom.rgb.g != 0 || step.colorFrom.rgb.b != 0) {
                pos = append(buf, cap, pos, ",\"colorFrom\":");
                pos = writeColorRef(buf, cap, pos, step.colorFrom);
            }

            pos = append(buf, cap, pos, ",\"color\":");
            pos = writeColorRef(buf, cap, pos, step.colorTo);

            if (step.durationMs > 0 || !isRunnerType(step.animType)) {
                pos = append(buf, cap, pos, ",\"duration\":%u", (unsigned)step.durationMs);
            }

            if (step.flags & FLAG_LOOP) pos = append(buf, cap, pos, ",\"loop\":true");

            if (step.flags & FLAG_PINGPONG) pos = append(buf, cap, pos, ",\"pingpong\":true");

            if (step.animates != TARGET_COLOR) {
                const char *target = animateTargetName(step.animates);

                if (target) {
                    pos = append(buf, cap, pos, ",\"animates\":\"%s\"", target);
                    pos = append(buf, cap, pos, ",\"from\":%u,\"to\":%u",
                                 (unsigned)step.valueFrom, (unsigned)step.valueTo);
                }
            }

            if (isRunnerType(step.animType)) {
                if (step.params[RUNNER_PARAM_WIDTH] != 0) {
                    if (step.animType == RUN_WHEEL) {
                        pos = append(buf, cap, pos, ",\"thickness\":%u",
                                     (unsigned)step.params[RUNNER_PARAM_WIDTH]);
                    } else {
                        pos = append(buf, cap, pos, ",\"width\":%u",
                                     (unsigned)step.params[RUNNER_PARAM_WIDTH]);
                    }
                }

                if (step.params[RUNNER_PARAM_LINES] != 0 && step.animType == RUN_WHEEL) {
                    pos = append(buf, cap, pos, ",\"lines\":%u",
                                 (unsigned)step.params[RUNNER_PARAM_LINES]);
                }

                if (step.params[RUNNER_PARAM_COUNT] != 0 &&
                    (step.animType == RUN_WAVE || step.animType == RUN_RIPPLE ||
                     step.animType == RUN_CHASE)) {
                    pos = append(buf, cap, pos, ",\"count\":%u",
                                 (unsigned)step.params[RUNNER_PARAM_COUNT]);
                }

                if (step.params[RUNNER_PARAM_WAVES] != 0 &&
                    (step.animType == RUN_RAIN || step.animType == RUN_SPARKLE ||
                     step.animType == RUN_MATRIX)) {
                    pos = append(buf, cap, pos, ",\"waves\":%u",
                                 (unsigned)step.params[RUNNER_PARAM_WAVES]);
                }

                if (step.speedMs != 0) {
                    pos = append(buf, cap, pos, ",\"speed\":%u", (unsigned)step.speedMs);
                }

                if (step.params[RUNNER_PARAM_AMOUNT] != 0) {
                    pos = append(buf, cap, pos, ",\"amount\":%u",
                                 (unsigned)step.params[RUNNER_PARAM_AMOUNT]);
                }

                if (step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_GEOMETRIC) {
                    pos = append(buf, cap, pos, ",\"directionality\":\"geometric\"");

                    if (step.params[RUNNER_PARAM_SRC_ARG] != 0) {
                        pos = append(buf, cap, pos, ",\"angle\":%u",
                                     (unsigned)step.params[RUNNER_PARAM_SRC_ARG] * 2u);
                    }
                } else {
                    pos = append(buf, cap, pos, ",");
                    pos = writeRunnerSource(buf, cap, pos, step);
                }

                if (step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_REVERSE) {
                    pos = append(buf, cap, pos, ",\"reverse\":true");
                }
            } else if (step.params[STEP_PARAM_PREPARE_1] != 0 || step.params[STEP_PARAM_PREPARE_2] != 0) {
                pos = append(buf, cap, pos, ",\"params\":[%u,%u",
                             (unsigned)step.params[STEP_PARAM_PREPARE_1],
                             (unsigned)step.params[STEP_PARAM_PREPARE_2]);

                for (uint8_t i = 2; i < 6; i++) {
                    if (step.params[i] != 0) {
                        pos = append(buf, cap, pos, ",%u", (unsigned)step.params[i]);
                    }
                }

                pos = append(buf, cap, pos, "]");
            }

            return append(buf, cap, pos, "}");
        }

        static const char *groupWireName(const SceneLayer& layer)
        {
            return (layer.groupName[0] != '\0') ? layer.groupName : nullptr;
        }

        static size_t writeStartAfter(
            char *             buf,
            size_t             cap,
            size_t             pos,
            const SceneLayer&  layer,
            const SceneRecord& record
        )
        {
            if (layer.startAfterGroupId == 0) return pos;

            for (uint8_t k = 0; k < record.layerCount; k++) {
                if (record.layers[k].groupId != layer.startAfterGroupId) continue;

                const char *gn = groupWireName(record.layers[k]);

                if (gn) return append(buf, cap, pos, ",\"startAfter\":\"%s\"", gn);

                return append(buf, cap, pos, ",\"startAfter\":\"%u\"",
                              (unsigned)layer.startAfterGroupId);
            }

            return pos;
        }

        static size_t writeLayer(
            char *             buf,
            size_t             cap,
            size_t             pos,
            const SceneLayer&  layer,
            const SceneRecord& record
        )
        {
            pos = append(buf, cap, pos, "{");

            const char *gn = groupWireName(layer);

            if (gn) {
                pos = append(buf, cap, pos, "\"group\":\"%s\"", gn);
            } else {
                pos = append(buf, cap, pos, "\"group\":%u", (unsigned)layer.groupId);
            }

            pos = append(buf, cap, pos, ",\"panels\":");
            pos = writePanelSelector(buf, cap, pos, layer.target);

            if (layer.palette[0] != '\0') {
                pos = append(buf, cap, pos, ",\"palette\":\"%s\"", layer.palette);
            }

            if (layer.blend != COMPOSE_DEFAULT) {
                const char *blend = blendWireName(layer.blend);

                if (blend) pos = append(buf, cap, pos, ",\"blend\":\"%s\"", blend);
            }

            if (layer.disabled) pos = append(buf, cap, pos, ",\"disabled\":true");

            if (layer.async & ScenePlayer::LAYER_ASYNC_NON_BLOCKING) {
                pos = append(buf, cap, pos, ",\"async\":\"free\"");
            } else if (layer.async & ScenePlayer::LAYER_ASYNC_LOOP) {
                pos = append(buf, cap, pos, ",\"async\":true");
            }

            pos = writeStartAfter(buf, cap, pos, layer, record);

            pos = append(buf, cap, pos, ",\"sequence\":[");

            for (uint8_t s = 0; s < layer.stepCount; s++) {
                if (s) pos = append(buf, cap, pos, ",");

                pos = writeStep(buf, cap, pos, layer.steps[s]);
            }

            return append(buf, cap, pos, "]}");
        }
    } // anonymous namespace

    int serializeScene(const SceneRecord& record, char *buf, size_t bufLen)
    {
        if (!buf || bufLen < 32) return -1;

        size_t pos = 0;

        pos = append(buf, bufLen, pos, "{\"schemaVersion\":%u", (unsigned)record.schemaVersion);

        if (record.id[0] != '\0') {
            pos = append(buf, bufLen, pos, ",\"id\":\"%s\"", record.id);
        }

        pos = append(buf, bufLen, pos, ",\"name\":\"%s\"", record.name);

        if (record.loop) pos = append(buf, bufLen, pos, ",\"loop\":true");

        if (record.speed != 1.0f) {
            pos = append(buf, bufLen, pos, ",\"speed\":%.2f", record.speed);
        }

        if (record.hasPalette) {
            pos = append(buf, bufLen, pos, ",\"palette\":\"%s\"", record.palette);
        }

        if (record.hasColors) {
            char pHex[8], sHex[8], tHex[8];

            jsonFormatHex(record.baseColors[0].r, record.baseColors[0].g, record.baseColors[0].b, pHex);
            jsonFormatHex(record.baseColors[1].r, record.baseColors[1].g, record.baseColors[1].b, sHex);
            jsonFormatHex(record.baseColors[2].r, record.baseColors[2].g, record.baseColors[2].b, tHex);
            pos = append(buf, bufLen, pos,
                         ",\"colors\":{\"primary\":\"%s\",\"secondary\":\"%s\",\"tertiary\":\"%s\"}",
                         pHex, sHex, tHex);
        }

        if (record.background.r != 0 || record.background.g != 0 || record.background.b != 0) {
            char bgHex[8];

            jsonFormatHex(record.background.r, record.background.g, record.background.b, bgHex);
            pos = append(buf, bufLen, pos, ",\"background\":\"%s\"", bgHex);
        }

        pos = append(buf, bufLen, pos, ",\"layers\":[");

        for (uint8_t i = 0; i < record.layerCount; i++) {
            if (i) pos = append(buf, bufLen, pos, ",");

            pos = writeLayer(buf, bufLen, pos, record.layers[i], record);
        }

        pos = append(buf, bufLen, pos, "]}");

        if (pos >= bufLen) return -1;

        buf[pos] = '\0';

        return (int)pos;
    }
}  // namespace Lightnet
