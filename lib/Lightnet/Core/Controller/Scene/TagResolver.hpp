#pragma once

// ============================================================================
// Tag names + the tag-resolution interface.
//
// Per-device tags let a scene target panels by a personal label ("accent",
// "left") that the graph can't infer. The name-validity rule lives here ONCE so
// the parser, the HTTP endpoint, and the on-disk store all agree — a tag that is
// settable must also be parseable in a scene, and vice versa.
//
// Pure C++ (TopologyIndex only) → the selector engine stays natively testable
// against a mock resolver.
// ============================================================================

#include <stdint.h>
#include <stddef.h>
#include "TopologyIndex.hpp"

namespace Lightnet {
    // Max tag-name length (characters, excluding the NUL terminator).
    static const uint8_t TAG_NAME_MAX = 15;

    // A valid tag is 1..TAG_NAME_MAX chars of [a-zA-Z0-9_-].
    inline bool isValidTagName(const char *s)
    {
        if (!s || !*s) return false;

        uint8_t n = 0;

        for (const char *p = s; *p; p++) {
            char c  = *p;
            bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
                      ((c >= '0') && (c <= '9')) || (c == '_') || (c == '-');

            if (!ok) return false;

            if (++n > TAG_NAME_MAX) return false;
        }

        return true;
    }

    // Resolves a tag name to the panels carrying it, as a slot set. `topo` provides
    // the panel-index → slot mapping. Implemented device-side by TagStore; mocked in
    // native tests.
    struct ITagResolver {
        virtual ~ITagResolver()
        {
        }

        virtual void panelsForTag(const char *name, const TopologyIndex& topo, PanelSet& out) const = 0;
    };
}  // namespace Lightnet
