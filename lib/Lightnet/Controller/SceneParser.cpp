#include "SceneParser.hpp"
#include "../Utils/SimpleJson.hpp"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Recursive-descent JSON parser for scene documents.
//
// All JSON tokenization (whitespace, strings, numbers, bools, value-skipping,
// object/array iteration) lives in SimpleJson.hpp. This file contains only the
// scene-domain dispatch: which keys map to which fields, validation, and the
// step/layer/panels structure.
// ============================================================================

namespace Lightnet {
namespace {

// ---------------------------------------------------------------------------
// Domain-specific value parsers
// ---------------------------------------------------------------------------

// Parse a JSON value that is either a "#RRGGBB" string or an object describing
// a colour reference: {"palette":N}, {"useColor":N}, or {"r":R,"g":G,"b":B}.
static bool parseColorRef(const char*& p, const char* end, ColorRef& out)
{
    jsonSkipWs(p, end);
    if (p >= end) return false;

    if (*p == '"') {
        char s[16];
        if (!jsonReadString(p, end, s, sizeof(s))) return false;
        uint8_t r, g, b;
        if (!jsonParseHexColor(s, strlen(s), &r, &g, &b)) return false;
        out = ColorRef_rgb(r, g, b);
        return true;
    }

    if (!jsonEnterObject(p, end)) return false;

    char key[12];
    bool found = false;
    while (jsonNextKey(p, end, key, sizeof(key))) {
        long v;
        if (strcmp(key, "palette") == 0) {
            if (!jsonReadUInt(p, end, &v) || v > 255) return false;
            out = ColorRef_palette((uint8_t)v);
            found = true;
        } else if (strcmp(key, "useColor") == 0) {
            if (!jsonReadUInt(p, end, &v) || v > 2) return false;
            out = ColorRef_useColor((uint8_t)v);
            found = true;
        } else if (key[0] && !key[1] && (key[0] == 'r' || key[0] == 'g' || key[0] == 'b')) {
            if (!jsonReadUInt(p, end, &v) || v > 255) return false;
            // Initialise to white on first channel; overwrite as we go.
            if (!found) { out = ColorRef_rgb(255, 255, 255); found = true; }
            if      (key[0] == 'r') out.rgb.r = (uint8_t)v;
            else if (key[0] == 'g') out.rgb.g = (uint8_t)v;
            else                    out.rgb.b = (uint8_t)v;
            out.kind = COLORREF_RGB;
        } else {
            jsonSkipValue(p, end);
        }
    }
    return found;
}

// ---------------------------------------------------------------------------
// AnimType / RunnerType string→enum
// ---------------------------------------------------------------------------

static uint8_t parseAnimTypeName(const char* s)
{
    if (strcmp(s, "SOLID")     == 0) return ANIM_SOLID;
    if (strcmp(s, "FADE")      == 0) return ANIM_FADE;
    if (strcmp(s, "TRANSITION")== 0) return ANIM_TRANSITION;
    if (strcmp(s, "BREATHE")   == 0) return ANIM_BREATHE;
    if (strcmp(s, "PULSE")     == 0) return ANIM_PULSE;
    if (strcmp(s, "BLINK")     == 0) return ANIM_BLINK;
    if (strcmp(s, "HUE_CYCLE") == 0) return ANIM_HUE_CYCLE;
    if (strcmp(s, "STROBE")    == 0) return ANIM_STROBE;
    if (strcmp(s, "REACTIVE")  == 0) return ANIM_REACTIVE;
    return 0xFF;
}

static uint8_t parseRunnerName(const char* s)
{
    if (strcmp(s, "WAVE")   == 0) return RUN_WAVE;
    if (strcmp(s, "RIPPLE") == 0) return RUN_RIPPLE;
    if (strcmp(s, "CHASE")  == 0) return RUN_CHASE;
    return 0xFF;
}

// ---------------------------------------------------------------------------
// Handle a single step key+value. p already points to the value start.
// Shared by parseStep (inside sequence array) and parseOneShotBody (root level).
// Unknown keys are skipped silently.
// ---------------------------------------------------------------------------

static bool handleStepField(const char* key, const char*& p, const char* end,
                              SceneStep& step, bool& hasType, char* errMsg, size_t errLen)
{
    if (strcmp(key, "type") == 0) {
        char s[20];
        if (!jsonReadString(p, end, s, sizeof(s))) { strncpy(errMsg, "step.type: not a string", errLen); return false; }
        uint8_t t = parseAnimTypeName(s);
        if (t == 0xFF) { snprintf(errMsg, errLen, "unknown animType: %s", s); return false; }
        if (hasType) { strncpy(errMsg, "step: type and runner both set", errLen); return false; }
        step.animType = t; hasType = true;
    } else if (strcmp(key, "runner") == 0) {
        char s[16];
        if (!jsonReadString(p, end, s, sizeof(s))) { strncpy(errMsg, "step.runner: not a string", errLen); return false; }
        uint8_t t = parseRunnerName(s);
        if (t == 0xFF) { snprintf(errMsg, errLen, "unknown runner: %s", s); return false; }
        if (hasType) { strncpy(errMsg, "step: type and runner both set", errLen); return false; }
        step.animType = t; hasType = true;
    } else if (strcmp(key, "colorFrom") == 0) {
        if (!parseColorRef(p, end, step.colorFrom)) { strncpy(errMsg, "step.colorFrom: invalid color ref", errLen); return false; }
    } else if (strcmp(key, "colorTo") == 0 || strcmp(key, "color") == 0) {
        if (!parseColorRef(p, end, step.colorTo)) { strncpy(errMsg, "step.colorTo: invalid color ref", errLen); return false; }
    } else if (strcmp(key, "brightnessFrom") == 0) {
        long v; if (!jsonReadUInt(p, end, &v) || v > 255) { strncpy(errMsg, "step.brightnessFrom out of range", errLen); return false; }
        step.brightnessFrom = (uint8_t)v;
    } else if (strcmp(key, "brightnessTo") == 0) {
        long v; if (!jsonReadUInt(p, end, &v) || v > 255) { strncpy(errMsg, "step.brightnessTo out of range", errLen); return false; }
        step.brightnessTo = (uint8_t)v;
    } else if (strcmp(key, "duration") == 0) {
        long v; if (!jsonReadUInt(p, end, &v)) { strncpy(errMsg, "step.duration: not a uint", errLen); return false; }
        step.durationMs = (uint16_t)v;
    } else if (strcmp(key, "loop") == 0) {
        bool b; if (!jsonReadBool(p, end, &b)) { strncpy(errMsg, "step.loop: not bool", errLen); return false; }
        if (b) step.flags |= FLAG_LOOP;
    } else if (strcmp(key, "pingpong") == 0) {
        bool b; if (!jsonReadBool(p, end, &b)) { strncpy(errMsg, "step.pingpong: not bool", errLen); return false; }
        if (b) step.flags |= FLAG_PINGPONG;
    } else if (strcmp(key, "params") == 0) {
        if (!jsonEnterArray(p, end)) { strncpy(errMsg, "step.params: expected array", errLen); return false; }
        uint8_t pi = 0;
        while (jsonNextElement(p, end)) {
            long v; if (!jsonReadUInt(p, end, &v) || v > 255) { strncpy(errMsg, "step.params[i] out of range", errLen); return false; }
            if (pi >= 4) { strncpy(errMsg, "step.params too long (max 4)", errLen); return false; }
            step.params[pi++] = (uint8_t)v;
        }
    } else if (strcmp(key, "waveWidth") == 0) {
        long v; if (!jsonReadUInt(p, end, &v) || v > 255) return false; step.params[0] = (uint8_t)v;
    } else if (strcmp(key, "rippleWidth") == 0) {
        long v; if (!jsonReadUInt(p, end, &v) || v > 255) return false; step.params[0] = (uint8_t)v;
    } else if (strcmp(key, "originPanel") == 0) {
        long v; if (!jsonReadUInt(p, end, &v) || v > 255) return false; step.params[1] = (uint8_t)v;
    } else {
        jsonSkipValue(p, end);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Parse one step object. p points just after the opening '{'.
// ---------------------------------------------------------------------------

static bool parseStep(const char*& p, const char* end, SceneStep& step, char* errMsg, size_t errLen)
{
    memset(&step, 0, sizeof(step));
    step.animType       = ANIM_SOLID;
    step.brightnessFrom = 255;
    step.brightnessTo   = 255;
    bool hasType = false;

    char key[20];
    while (jsonNextKey(p, end, key, sizeof(key))) {
        if (!handleStepField(key, p, end, step, hasType, errMsg, errLen)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Parse the "panels" value. Accepts string "all", array [..ids..], or
// object {"exclude":[..ids..]}.
// ---------------------------------------------------------------------------

static bool parsePanels(const char*& p, const char* end, SceneLayer& layer, char* errMsg, size_t errLen)
{
    jsonSkipWs(p, end);
    if (p >= end) return false;

    if (*p == '"') {
        char s[8];
        if (!jsonReadString(p, end, s, sizeof(s))) return false;
        if (strcmp(s, "all") != 0) {
            snprintf(errMsg, errLen, "panels: unknown string \"%s\" (use \"all\")", s);
            return false;
        }
        layer.targetMode  = PanelTargetMode::ALL;
        layer.targetCount = 0;
        return true;
    }

    if (*p == '[') {
        jsonEnterArray(p, end);
        layer.targetMode = PanelTargetMode::LIST;
        layer.targetCount = 0;
        while (jsonNextElement(p, end)) {
            long v;
            if (!jsonReadUInt(p, end, &v) || v > 255) { strncpy(errMsg, "panels[]: invalid index", errLen); return false; }
            if (layer.targetCount >= SCENE_MAX_PANELS_PER_LAYER) { strncpy(errMsg, "panels[]: too many panels", errLen); return false; }
            layer.targetList[layer.targetCount++] = (uint8_t)v;
        }
        if (layer.targetCount == 0) { strncpy(errMsg, "panels[]: empty list", errLen); return false; }
        return true;
    }

    if (*p == '{') {
        jsonEnterObject(p, end);
        layer.targetMode  = PanelTargetMode::EXCLUDE;
        layer.targetCount = 0;
        char key[12];
        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "exclude") != 0) { jsonSkipValue(p, end); continue; }
            if (!jsonEnterArray(p, end)) { strncpy(errMsg, "panels.exclude: expected array", errLen); return false; }
            while (jsonNextElement(p, end)) {
                long v;
                if (!jsonReadUInt(p, end, &v) || v > 255) { strncpy(errMsg, "panels.exclude[]: invalid index", errLen); return false; }
                if (layer.targetCount >= SCENE_MAX_PANELS_PER_LAYER) { strncpy(errMsg, "panels.exclude[]: too many", errLen); return false; }
                layer.targetList[layer.targetCount++] = (uint8_t)v;
            }
        }
        return true;
    }

    strncpy(errMsg, "panels: expected string, array, or object", errLen);
    return false;
}

// ---------------------------------------------------------------------------
// Parse one layer object. p points just after the opening '{'.
// ---------------------------------------------------------------------------

static bool parseLayer(const char*& p, const char* end, SceneLayer& layer, char* errMsg, size_t errLen)
{
    memset(&layer, 0, sizeof(layer));
    layer.targetMode = PanelTargetMode::ALL;

    char key[16];
    while (jsonNextKey(p, end, key, sizeof(key))) {
        if (strcmp(key, "group") == 0) {
            long v; if (!jsonReadUInt(p, end, &v) || v == 0 || v > 254) {
                strncpy(errMsg, "layer.group: must be 1-254", errLen); return false;
            }
            layer.groupId = (uint8_t)v;
        } else if (strcmp(key, "panels") == 0) {
            if (!parsePanels(p, end, layer, errMsg, errLen)) return false;
        } else if (strcmp(key, "palette") == 0) {
            if (!jsonReadString(p, end, layer.palette, sizeof(layer.palette))) {
                strncpy(errMsg, "layer.palette: not a string", errLen); return false;
            }
        } else if (strcmp(key, "sequence") == 0) {
            if (!jsonEnterArray(p, end)) { strncpy(errMsg, "layer.sequence: expected array", errLen); return false; }
            while (jsonNextElement(p, end)) {
                if (!jsonEnterObject(p, end)) { strncpy(errMsg, "layer.sequence[]: expected object", errLen); return false; }
                if (layer.stepCount >= SCENE_MAX_STEPS) {
                    strncpy(errMsg, "layer.sequence: too many steps", errLen); return false;
                }
                if (!parseStep(p, end, layer.steps[layer.stepCount], errMsg, errLen)) return false;
                layer.stepCount++;
            }
        } else {
            jsonSkipValue(p, end);
        }
    }

    if (layer.groupId == 0) { strncpy(errMsg, "layer: missing group", errLen); return false; }
    if (layer.stepCount == 0) { strncpy(errMsg, "layer: empty sequence", errLen); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Parse the "colors" object {primary, secondary, tertiary}.
// ---------------------------------------------------------------------------

static bool parseColors(const char*& p, const char* end,
                         Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
                         char* errMsg, size_t errLen)
{
    if (!jsonEnterObject(p, end)) { strncpy(errMsg, "colors: expected object", errLen); return false; }

    char key[12];
    while (jsonNextKey(p, end, key, sizeof(key))) {
        char hex[16];
        if (!jsonReadString(p, end, hex, sizeof(hex))) { strncpy(errMsg, "colors: value not a string", errLen); return false; }
        Protocol::ColorRGB c;
        if (!jsonParseHexColor(hex, strlen(hex), &c.r, &c.g, &c.b)) { snprintf(errMsg, errLen, "colors.%s: bad hex", key); return false; }
        if      (strcmp(key, "primary")   == 0) baseColors[0] = c;
        else if (strcmp(key, "secondary") == 0) baseColors[1] = c;
        else if (strcmp(key, "tertiary")  == 0) baseColors[2] = c;
    }
    return true;
}

}  // anonymous namespace

// ============================================================================
// Public entry points
// ============================================================================

// Parse a flat one-shot body into a single-layer SceneLayer.
// Body shape: {"group":N, "panels":..., <step fields>}
// All step fields (type/runner/color/duration/params/...) live at the root alongside
// group and panels — no "sequence" array. Reuses handleStepField for the step part.
bool parseOneShotBody(const char* json, size_t len, SceneLayer& layer, char* errMsg, size_t errLen)
{
    memset(&layer, 0, sizeof(layer));
    layer.targetMode = PanelTargetMode::ALL;
    SceneStep& step = layer.steps[0];
    memset(&step, 0, sizeof(step));
    step.animType       = ANIM_SOLID;
    step.brightnessFrom = 255;
    step.brightnessTo   = 255;
    bool hasType = false;

    const char* p   = json;
    const char* end = json + len;

    if (!jsonEnterObject(p, end)) { strncpy(errMsg, "expected {", errLen); return false; }

    char key[20];
    while (jsonNextKey(p, end, key, sizeof(key))) {
        if (strcmp(key, "group") == 0) {
            long v; if (!jsonReadUInt(p, end, &v) || v == 0 || v > 254) {
                strncpy(errMsg, "group must be 1-254", errLen); return false;
            }
            layer.groupId = (uint8_t)v;
        } else if (strcmp(key, "panels") == 0) {
            if (!parsePanels(p, end, layer, errMsg, errLen)) return false;
        } else {
            if (!handleStepField(key, p, end, step, hasType, errMsg, errLen)) return false;
        }
    }

    if (layer.groupId == 0) { strncpy(errMsg, "group is required", errLen); return false; }
    layer.stepCount = 1;
    return true;
}

bool parseScene(const char* json, size_t len, SceneParseResult& out)
{
    memset(&out, 0, sizeof(out));
    out.schemaVersion = 1;
    out.loop = false;
    strncpy(out.palette, "userColors", sizeof(out.palette) - 1);
    out.baseColors[0] = {0xFF, 0xFF, 0xFF};
    out.baseColors[1] = {0x00, 0x00, 0x00};
    out.baseColors[2] = {0x00, 0x00, 0x00};

    const char* p   = json;
    const char* end = json + len;

    if (!jsonEnterObject(p, end)) {
        strncpy(out.errMsg, "scene: expected {", sizeof(out.errMsg));
        return false;
    }

    char key[20];
    while (jsonNextKey(p, end, key, sizeof(key))) {
        if (strcmp(key, "schemaVersion") == 0) {
            long v; if (!jsonReadUInt(p, end, &v)) { strncpy(out.errMsg, "schemaVersion: not uint", sizeof(out.errMsg)); return false; }
            out.schemaVersion = (uint8_t)v;
        } else if (strcmp(key, "name") == 0) {
            if (!jsonReadString(p, end, out.name, sizeof(out.name))) {
                strncpy(out.errMsg, "name: not a string", sizeof(out.errMsg)); return false;
            }
        } else if (strcmp(key, "loop") == 0) {
            if (!jsonReadBool(p, end, &out.loop)) { strncpy(out.errMsg, "loop: not bool", sizeof(out.errMsg)); return false; }
        } else if (strcmp(key, "palette") == 0) {
            if (!jsonReadString(p, end, out.palette, sizeof(out.palette))) {
                strncpy(out.errMsg, "palette: not a string", sizeof(out.errMsg)); return false;
            }
        } else if (strcmp(key, "colors") == 0) {
            if (!parseColors(p, end, out.baseColors, out.errMsg, sizeof(out.errMsg))) return false;
        } else if (strcmp(key, "layers") == 0) {
            if (!jsonEnterArray(p, end)) { strncpy(out.errMsg, "layers: expected array", sizeof(out.errMsg)); return false; }
            while (jsonNextElement(p, end)) {
                if (!jsonEnterObject(p, end)) { strncpy(out.errMsg, "layers[]: expected object", sizeof(out.errMsg)); return false; }
                if (out.layerCount >= SCENE_MAX_LAYERS) {
                    strncpy(out.errMsg, "too many layers", sizeof(out.errMsg)); return false;
                }
                if (!parseLayer(p, end, out.layers[out.layerCount], out.errMsg, sizeof(out.errMsg))) return false;
                out.layerCount++;
            }
        } else {
            jsonSkipValue(p, end);
        }
    }

    // --- Validation ---

    if (out.schemaVersion > SCENE_SCHEMA_VERSION) {
        snprintf(out.errMsg, sizeof(out.errMsg),
                 "schema_too_new: scene=%u firmware=%u",
                 (unsigned)out.schemaVersion, (unsigned)SCENE_SCHEMA_VERSION);
        return false;
    }

    if (out.name[0] == '\0') { strncpy(out.errMsg, "name is required", sizeof(out.errMsg)); return false; }
    if (out.layerCount == 0) { strncpy(out.errMsg, "at least one layer required", sizeof(out.errMsg)); return false; }

    // Validate name characters and length. 18-char max keeps
    // /scenes/<name>.json within SPIFFS's 31-char path limit.
    size_t nameLen = 0;
    for (const char* c = out.name; *c; c++, nameLen++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) {
            strncpy(out.errMsg, "name: only [a-zA-Z0-9_-] allowed", sizeof(out.errMsg));
            return false;
        }
    }
    if (nameLen > 18) {
        strncpy(out.errMsg, "name: max 18 characters", sizeof(out.errMsg));
        return false;
    }

    for (uint8_t i = 0; i < out.layerCount; i++) {
        for (uint8_t j = i + 1; j < out.layerCount; j++) {
            if (out.layers[i].groupId == out.layers[j].groupId) {
                strncpy(out.errMsg, "duplicate group id across layers", sizeof(out.errMsg));
                return false;
            }
        }
    }

    // Infinite step (duration=0) only allowed as the last step of a sequence.
    for (uint8_t i = 0; i < out.layerCount; i++) {
        const SceneLayer& layer = out.layers[i];
        for (uint8_t s = 0; s < layer.stepCount; s++) {
            if (layer.steps[s].durationMs == 0 && s < (uint8_t)(layer.stepCount - 1)) {
                strncpy(out.errMsg, "infinite step (duration=0) only allowed as last step", sizeof(out.errMsg));
                return false;
            }
        }
    }

    out.valid = true;
    return true;
}

}  // namespace Lightnet
