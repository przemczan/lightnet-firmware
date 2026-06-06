#include "SceneParser.hpp"
#include "../../Utils/SimpleJson.hpp"
#include "../Topology/PanelSelectorParser.hpp"
#include "../Topology/PanelField.hpp"
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
        static const uint8_t GROUP_NAME_LEN = 16;

        // ---------------------------------------------------------------------------
        // Group-name → numeric-ID table. String group names are interned in order of
        // first appearance and assigned auto IDs 1,2,3…; numeric groups use their
        // literal value and are not interned. startAfter references resolve by name.
        // ---------------------------------------------------------------------------

        struct NameTable {
            char    names[SCENE_MAX_LAYERS][GROUP_NAME_LEN];
            uint8_t ids[SCENE_MAX_LAYERS];
            uint8_t count;
            uint8_t nextAuto; // next auto ID to hand out

            void reset()
            {
                count = 0;
                nextAuto = 1;
            }

            // Return existing ID for `name`, or assign the next auto ID. 0 on overflow.
            uint8_t intern(const char *name)
            {
                for (uint8_t i = 0; i < count; i++) {
                    if (strcmp(names[i], name) == 0) return ids[i];
                }

                if (count >= SCENE_MAX_LAYERS) return 0;

                strncpy(names[count], name, GROUP_NAME_LEN - 1);
                names[count][GROUP_NAME_LEN - 1] = '\0';
                ids[count] = nextAuto++;

                return ids[count++];
            }

            // Resolved ID for `name`, or 0 if not interned.
            uint8_t find(const char *name) const
            {
                for (uint8_t i = 0; i < count; i++) {
                    if (strcmp(names[i], name) == 0) return ids[i];
                }

                return 0;
            }
        };

        // ---------------------------------------------------------------------------
        // Domain-specific value parsers
        // ---------------------------------------------------------------------------

        // Parse a JSON value that is either a "#RRGGBB" string or an object describing
        // a colour reference: {"palette":N}, {"useColor":N}, or {"r":R,"g":G,"b":B}.
        static bool parseColorRef(const char *& p, const char *end, ColorRef& out)
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
                    if (!found) {
                        out = ColorRef_rgb(255, 255, 255);
                        found = true;
                    }

                    if (key[0] == 'r')      out.rgb.r = (uint8_t)v;
                    else if (key[0] == 'g') out.rgb.g = (uint8_t)v;
                    else out.rgb.b = (uint8_t)v;

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

        static uint8_t parseAnimTypeName(const char *s)
        {
            if (strcmp(s, "SOLID") == 0)     return ANIM_SOLID;

            if (strcmp(s, "FADE") == 0)      return ANIM_FADE;

            if (strcmp(s, "TRANSITION") == 0) return ANIM_TRANSITION;

            if (strcmp(s, "BREATHE") == 0)   return ANIM_BREATHE;

            if (strcmp(s, "PULSE") == 0)     return ANIM_PULSE;

            if (strcmp(s, "BLINK") == 0)     return ANIM_BLINK;

            if (strcmp(s, "HUE_CYCLE") == 0) return ANIM_HUE_CYCLE;

            if (strcmp(s, "STROBE") == 0)    return ANIM_STROBE;

            if (strcmp(s, "REACTIVE") == 0)  return ANIM_REACTIVE;

            return 0xFF;
        }

        static uint8_t parseRunnerName(const char *s)
        {
            if (strcmp(s, "WAVE") == 0)   return RUN_WAVE;

            if (strcmp(s, "RIPPLE") == 0) return RUN_RIPPLE;

            if (strcmp(s, "CHASE") == 0)  return RUN_CHASE;

            return 0xFF;
        }

        // ---------------------------------------------------------------------------
        // Handle a single step key+value. p already points to the value start.
        // Shared by parseStep (inside sequence array) and parseOneShotBody (root level).
        // Unknown keys are skipped silently.
        // ---------------------------------------------------------------------------

        static bool handleStepField(
            const char *  key,
            const char *& p,
            const char *  end,
            SceneStep&    step,
            bool&         hasType,
            char *        errMsg,
            size_t        errLen
        )
        {
            if (strcmp(key, "type") == 0) {
                char s[20];

                if (!jsonReadString(p, end, s, sizeof(s))) {
                    strncpy(errMsg, "step.type: not a string", errLen);

                    return false;
                }

                uint8_t t = parseAnimTypeName(s);

                if (t == 0xFF) {
                    snprintf(errMsg, errLen, "unknown animType: %s", s);

                    return false;
                }

                if (hasType) {
                    strncpy(errMsg, "step: type and runner both set", errLen);

                    return false;
                }

                step.animType = t;
                hasType = true;
            } else if (strcmp(key, "runner") == 0) {
                char s[16];

                if (!jsonReadString(p, end, s, sizeof(s))) {
                    strncpy(errMsg, "step.runner: not a string", errLen);

                    return false;
                }

                uint8_t t = parseRunnerName(s);

                if (t == 0xFF) {
                    snprintf(errMsg, errLen, "unknown runner: %s", s);

                    return false;
                }

                if (hasType) {
                    strncpy(errMsg, "step: type and runner both set", errLen);

                    return false;
                }

                step.animType = t;
                hasType = true;
            } else if (strcmp(key, "colorFrom") == 0) {
                if (!parseColorRef(p, end, step.colorFrom)) {
                    strncpy(errMsg, "step.colorFrom: invalid color ref", errLen);

                    return false;
                }
            } else if (strcmp(key, "colorTo") == 0 || strcmp(key, "color") == 0) {
                if (!parseColorRef(p, end, step.colorTo)) {
                    strncpy(errMsg, "step.colorTo: invalid color ref", errLen);

                    return false;
                }
            } else if (strcmp(key, "duration") == 0) {
                long v;

                if (!jsonReadUInt(p, end, &v)) {
                    strncpy(errMsg, "step.duration: not a uint", errLen);

                    return false;
                }

                step.durationMs = (uint16_t)v;
            } else if (strcmp(key, "loop") == 0) {
                bool b;

                if (!jsonReadBool(p, end, &b)) {
                    strncpy(errMsg, "step.loop: not bool", errLen);

                    return false;
                }

                if (b) step.flags |= FLAG_LOOP;
            } else if (strcmp(key, "pingpong") == 0) {
                bool b;

                if (!jsonReadBool(p, end, &b)) {
                    strncpy(errMsg, "step.pingpong: not bool", errLen);

                    return false;
                }

                if (b) step.flags |= FLAG_PINGPONG;
            } else if (strcmp(key, "params") == 0) {
                if (!jsonEnterArray(p, end)) {
                    strncpy(errMsg, "step.params: expected array", errLen);

                    return false;
                }

                uint8_t pi = 0;

                while (jsonNextElement(p, end)) {
                    long v;

                    if (!jsonReadUInt(p, end, &v) || v > 255) {
                        strncpy(errMsg, "step.params[i] out of range", errLen);

                        return false;
                    }

                    if (pi >= 4) {
                        strncpy(errMsg, "step.params too long (max 4)", errLen);

                        return false;
                    }

                    step.params[pi++] = (uint8_t)v;
                }
            } else if (strcmp(key, "waveWidth") == 0 || strcmp(key, "rippleWidth") == 0) {
                long v;

                if (!jsonReadUInt(p, end, &v) || v > 255) return false;

                step.params[RUNNER_PARAM_WIDTH] = (uint8_t)v;
            } else if (strcmp(key, "source") == 0) {
                char s[16];

                if (!jsonReadString(p, end, s, sizeof(s))) {
                    strncpy(errMsg, "step.source: not a string", errLen);

                    return false;
                }

                if (strcmp(s, "root") == 0) {
                    step.params[RUNNER_PARAM_SRC_KIND] = SRC_ROOT;
                } else if (strcmp(s, "leaves") == 0) {
                    step.params[RUNNER_PARAM_SRC_KIND] = SRC_LEAVES;
                } else if (strcmp(s, "all") == 0) {
                    step.params[RUNNER_PARAM_SRC_KIND] = SRC_ALL;
                } else if (strncmp(s, "panel:", 6) == 0) {
                    long n = atol(s + 6);

                    if (n < 1 || n > 255) {
                        strncpy(errMsg, "step.source: bad panel index", errLen);

                        return false;
                    }

                    step.params[RUNNER_PARAM_SRC_KIND] = SRC_PANEL;
                    step.params[RUNNER_PARAM_SRC_ARG]  = (uint8_t)n;
                } else {
                    snprintf(errMsg, errLen, "step.source: unknown (%s)", s);

                    return false;
                }
            } else if (strcmp(key, "reverse") == 0) {
                bool b;

                if (!jsonReadBool(p, end, &b)) {
                    strncpy(errMsg, "step.reverse: not bool", errLen);

                    return false;
                }

                if (b) step.params[RUNNER_PARAM_FLAGS] |= RUNNER_FLAG_REVERSE;
            } else if (strcmp(key, "originPanel") == 0) {
                // Legacy ripple origin → migrate to source:panel:N.
                long v;

                if (!jsonReadUInt(p, end, &v) || v == 0 || v > 255) return false;

                step.params[RUNNER_PARAM_SRC_KIND] = SRC_PANEL;
                step.params[RUNNER_PARAM_SRC_ARG]  = (uint8_t)v;
            } else {
                jsonSkipValue(p, end);
            }

            return true;
        }

        // ---------------------------------------------------------------------------
        // Parse one step object. p points just after the opening '{'.
        // ---------------------------------------------------------------------------

        static bool parseStep(const char *& p, const char *end, SceneStep& step, char *errMsg, size_t errLen)
        {
            memset(&step, 0, sizeof(step));
            step.animType = ANIM_SOLID;

            bool hasType = false;

            char key[20];

            while (jsonNextKey(p, end, key, sizeof(key))) {
                if (!handleStepField(key, p, end, step, hasType, errMsg, errLen)) return false;
            }

            // A step with neither "type" nor "runner" is a GAP: a timed no-op that just
            // delays the layer for its duration (panels hold their current state).
            if (!hasType) step.animType = ANIM_GAP;

            return true;
        }

        // ---------------------------------------------------------------------------
        // Parse the "panels" value into the layer's selector program. The full grammar
        // (index arrays, graph selectors, tags, and any/all/not/exclude composition)
        // lives in the pure, natively-tested PanelSelectorParser.
        // ---------------------------------------------------------------------------

        static bool parsePanels(const char *& p, const char *end, SceneLayer& layer, char *errMsg, size_t errLen)
        {
            return parsePanelSelector(p, end, layer.target, errMsg, errLen);
        }

        // ---------------------------------------------------------------------------
        // Parse one layer object. p points just after the opening '{'.
        // ---------------------------------------------------------------------------

        static bool parseLayer(
            const char *& p,
            const char *  end,
            SceneLayer&   layer,
            NameTable&    names,
            char (&startAfterName)[GROUP_NAME_LEN],
            char *        errMsg,
            size_t        errLen
        )
        {
            memset(&layer, 0, sizeof(layer));
            layer.target.clear();
            layer.target.emit(SEL_ALL); // default targeting when no "panels" key is present
            startAfterName[0] = '\0';

            char key[16];

            while (jsonNextKey(p, end, key, sizeof(key))) {
                if (strcmp(key, "group") == 0) {
                    jsonSkipWs(p, end);

                    if (p < end && *p == '"') {
                        // Named group — intern to an auto-assigned numeric ID.
                        char gname[GROUP_NAME_LEN];

                        if (!jsonReadString(p, end, gname, sizeof(gname)) || gname[0] == '\0') {
                            strncpy(errMsg, "layer.group: empty name", errLen);

                            return false;
                        }

                        uint8_t id = names.intern(gname);

                        if (id == 0) {
                            strncpy(errMsg, "layer.group: too many group names", errLen);

                            return false;
                        }

                        layer.groupId = id;
                    } else {
                        // Numeric group (back-compat).
                        long v;

                        if (!jsonReadUInt(p, end, &v) || v == 0 || v > 254) {
                            strncpy(errMsg, "layer.group: must be 1-254 or a name", errLen);

                            return false;
                        }

                        layer.groupId = (uint8_t)v;
                    }
                } else if (strcmp(key, "async") == 0) {
                    bool b;

                    if (!jsonReadBool(p, end, &b)) {
                        strncpy(errMsg, "layer.async: not bool", errLen);

                        return false;
                    }

                    layer.async = b ? 1 : 0;
                } else if (strcmp(key, "startAfter") == 0) {
                    if (!jsonReadString(p, end, startAfterName, GROUP_NAME_LEN) || startAfterName[0] == '\0') {
                        strncpy(errMsg, "layer.startAfter: not a non-empty group name", errLen);

                        return false;
                    }
                } else if (strcmp(key, "panels") == 0) {
                    if (!parsePanels(p, end, layer, errMsg, errLen)) return false;
                } else if (strcmp(key, "palette") == 0) {
                    if (!jsonReadString(p, end, layer.palette, sizeof(layer.palette))) {
                        strncpy(errMsg, "layer.palette: not a string", errLen);

                        return false;
                    }
                } else if (strcmp(key, "sequence") == 0) {
                    if (!jsonEnterArray(p, end)) {
                        strncpy(errMsg, "layer.sequence: expected array", errLen);

                        return false;
                    }

                    while (jsonNextElement(p, end)) {
                        if (!jsonEnterObject(p, end)) {
                            strncpy(errMsg, "layer.sequence[]: expected object", errLen);

                            return false;
                        }

                        if (layer.stepCount >= SCENE_MAX_STEPS) {
                            strncpy(errMsg, "layer.sequence: too many steps", errLen);

                            return false;
                        }

                        if (!parseStep(p, end, layer.steps[layer.stepCount], errMsg, errLen)) return false;

                        layer.stepCount++;
                    }
                } else {
                    jsonSkipValue(p, end);
                }
            }

            if (layer.groupId == 0) {
                strncpy(errMsg, "layer: missing group", errLen);

                return false;
            }

            if (layer.stepCount == 0) {
                strncpy(errMsg, "layer: empty sequence", errLen);

                return false;
            }

            return true;
        }

        // ---------------------------------------------------------------------------
        // Parse the "colors" object {primary, secondary, tertiary}.
        // ---------------------------------------------------------------------------

        static bool parseColors(
            const char *&      p,
            const char *       end,
            Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
            char *             errMsg,
            size_t             errLen
        )
        {
            if (!jsonEnterObject(p, end)) {
                strncpy(errMsg, "colors: expected object", errLen);

                return false;
            }

            char key[12];

            while (jsonNextKey(p, end, key, sizeof(key))) {
                char hex[16];

                if (!jsonReadString(p, end, hex, sizeof(hex))) {
                    strncpy(errMsg, "colors: value not a string", errLen);

                    return false;
                }

                Protocol::ColorRGB c;

                if (!jsonParseHexColor(hex, strlen(hex), &c.r, &c.g, &c.b)) {
                    snprintf(errMsg, errLen, "colors.%s: bad hex", key);

                    return false;
                }

                if (strcmp(key, "primary") == 0)        baseColors[0] = c;
                else if (strcmp(key, "secondary") == 0) baseColors[1] = c;
                else if (strcmp(key, "tertiary") == 0)  baseColors[2] = c;
            }

            return true;
        }
    } // anonymous namespace

    // ============================================================================
    // Public entry points
    // ============================================================================

    // Parse a flat one-shot body into a single-layer SceneLayer.
    // Body shape: {"group":N, "panels":..., <step fields>}
    // All step fields (type/runner/color/duration/params/...) live at the root alongside
    // group and panels — no "sequence" array. Reuses handleStepField for the step part.
    bool parseOneShotBody(const char *json, size_t len, SceneLayer& layer, char *errMsg, size_t errLen)
    {
        memset(&layer, 0, sizeof(layer));
        layer.target.clear();
        layer.target.emit(SEL_ALL); // default targeting when no "panels" key is present

        SceneStep& step = layer.steps[0];

        memset(&step, 0, sizeof(step));
        step.animType = ANIM_SOLID;

        bool hasType = false;

        const char *p   = json;
        const char *end = json + len;

        if (!jsonEnterObject(p, end)) {
            strncpy(errMsg, "expected {", errLen);

            return false;
        }

        char key[20];

        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "group") == 0) {
                long v;

                if (!jsonReadUInt(p, end, &v) || v == 0 || v > 254) {
                    strncpy(errMsg, "group must be 1-254", errLen);

                    return false;
                }

                layer.groupId = (uint8_t)v;
            } else if (strcmp(key, "panels") == 0) {
                if (!parsePanels(p, end, layer, errMsg, errLen)) return false;
            } else {
                if (!handleStepField(key, p, end, step, hasType, errMsg, errLen)) return false;
            }
        }

        if (layer.groupId == 0) {
            strncpy(errMsg, "group is required", errLen);

            return false;
        }

        layer.stepCount = 1;

        return true;
    }

    bool parseScene(const char *json, size_t len, SceneParseResult& out)
    {
        memset(&out, 0, sizeof(out));
        out.schemaVersion = 1;
        out.loop = false;
        out.speed = 1.0f;
        strncpy(out.palette, "userColors", sizeof(out.palette) - 1);
        out.baseColors[0] = { 0xFF, 0xFF, 0xFF };
        out.baseColors[1] = { 0x00, 0x00, 0x00 };
        out.baseColors[2] = { 0x00, 0x00, 0x00 };

        // Scratch for group-name interning and per-layer startAfter references; both are
        // resolved to numeric IDs after all layers are parsed (forward refs allowed).
        NameTable names;

        names.reset();

        char startAfterNames[SCENE_MAX_LAYERS][GROUP_NAME_LEN];

        for (uint8_t i = 0; i < SCENE_MAX_LAYERS; i++) startAfterNames[i][0] = '\0';

        const char *p   = json;
        const char *end = json + len;

        if (!jsonEnterObject(p, end)) {
            strncpy(out.errMsg, "scene: expected {", sizeof(out.errMsg));

            return false;
        }

        char key[20];

        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "schemaVersion") == 0) {
                long v;

                if (!jsonReadUInt(p, end, &v)) {
                    strncpy(out.errMsg, "schemaVersion: not uint", sizeof(out.errMsg));

                    return false;
                }

                out.schemaVersion = (uint8_t)v;
            } else if (strcmp(key, "name") == 0) {
                if (!jsonReadString(p, end, out.name, sizeof(out.name))) {
                    strncpy(out.errMsg, "name: not a string", sizeof(out.errMsg));

                    return false;
                }
            } else if (strcmp(key, "loop") == 0) {
                if (!jsonReadBool(p, end, &out.loop)) {
                    strncpy(out.errMsg, "loop: not bool", sizeof(out.errMsg));

                    return false;
                }
            } else if (strcmp(key, "speed") == 0) {
                if (!jsonReadFloat(p, end, &out.speed)) {
                    strncpy(out.errMsg, "speed: not a number", sizeof(out.errMsg));

                    return false;
                }

                if (out.speed < 0.1f) out.speed = 0.1f;

                if (out.speed > 10.0f) out.speed = 10.0f;
            } else if (strcmp(key, "palette") == 0) {
                if (!jsonReadString(p, end, out.palette, sizeof(out.palette))) {
                    strncpy(out.errMsg, "palette: not a string", sizeof(out.errMsg));

                    return false;
                }
            } else if (strcmp(key, "colors") == 0) {
                if (!parseColors(p, end, out.baseColors, out.errMsg, sizeof(out.errMsg))) return false;
            } else if (strcmp(key, "layers") == 0) {
                if (!jsonEnterArray(p, end)) {
                    strncpy(out.errMsg, "layers: expected array", sizeof(out.errMsg));

                    return false;
                }

                while (jsonNextElement(p, end)) {
                    if (!jsonEnterObject(p, end)) {
                        strncpy(out.errMsg, "layers[]: expected object", sizeof(out.errMsg));

                        return false;
                    }

                    if (out.layerCount >= SCENE_MAX_LAYERS) {
                        strncpy(out.errMsg, "too many layers", sizeof(out.errMsg));

                        return false;
                    }

                    if (!parseLayer(p, end, out.layers[out.layerCount], names,
                                    startAfterNames[out.layerCount],
                                    out.errMsg, sizeof(out.errMsg))) return false;

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

        if (out.name[0] == '\0') {
            strncpy(out.errMsg, "name is required", sizeof(out.errMsg));

            return false;
        }

        if (out.layerCount == 0) {
            strncpy(out.errMsg, "at least one layer required", sizeof(out.errMsg));

            return false;
        }

        // Validate name characters and length.
        size_t nameLen = 0;

        for (const char *c = out.name; *c; c++, nameLen++) {
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

        // Resolve layer-level startAfter (by group name) to numeric group IDs.
        for (uint8_t i = 0; i < out.layerCount; i++) {
            if (startAfterNames[i][0] == '\0') continue;

            uint8_t id = names.find(startAfterNames[i]);

            if (id == 0) {
                snprintf(out.errMsg, sizeof(out.errMsg),
                         "startAfter: unknown group \"%s\"", startAfterNames[i]);

                return false;
            }

            if (id == out.layers[i].groupId) {
                strncpy(out.errMsg, "startAfter: layer cannot wait for itself", sizeof(out.errMsg));

                return false;
            }

            out.layers[i].startAfterGroupId = id;
            out.layers[i].async = 0; // startAfter takes precedence — async has no effect
        }

        // A depended-upon layer must be able to finish, otherwise its dependents never
        // start — reject an infinite (duration=0) last step on any startAfter target.
        for (uint8_t i = 0; i < out.layerCount; i++) {
            uint8_t dep = out.layers[i].startAfterGroupId;

            if (dep == 0) continue;

            for (uint8_t k = 0; k < out.layerCount; k++) {
                const SceneLayer& t = out.layers[k];

                if (t.groupId != dep) continue;

                if (t.stepCount > 0 && t.steps[t.stepCount - 1].durationMs == 0) {
                    strncpy(out.errMsg, "startAfter: target layer never ends (infinite last step)", sizeof(out.errMsg));

                    return false;
                }

                break;
            }
        }

        // Reject startAfter dependency cycles: follow each layer's chain; a chain longer
        // than layerCount hops without terminating means a loop.
        for (uint8_t i = 0; i < out.layerCount; i++) {
            uint8_t cur  = i;
            uint8_t hops = 0;

            while (out.layers[cur].startAfterGroupId != 0) {
                int next = -1;

                for (uint8_t k = 0; k < out.layerCount; k++) {
                    if (out.layers[k].groupId == out.layers[cur].startAfterGroupId) {
                        next = k;
                        break;
                    }
                }

                if (next < 0) break;

                cur = (uint8_t)next;

                if (++hops > out.layerCount) {
                    strncpy(out.errMsg, "startAfter: dependency cycle", sizeof(out.errMsg));

                    return false;
                }
            }
        }

        out.valid = true;

        return true;
    }
}  // namespace Lightnet
