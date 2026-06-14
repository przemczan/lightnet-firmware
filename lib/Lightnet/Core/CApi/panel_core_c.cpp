// panel_core_c.cpp — thin C ABI over Lightnet::AnimationPlayer. See panel_core_c.h.
//
// Packet decoding reuses the firmware struct layout: the on-wire bytes are the packed,
// little-endian PacketMeta-prefixed structs, so a bounds-checked reinterpret_cast is the
// decoder. (Packed structs have alignment 1 → the cast is well-defined for any byte pointer.)
// Target ABIs — Android arm64/x86_64, iOS arm64 — are all little-endian, matching the bus.

#include "panel_core_c.h"
#include "AnimationPlayer.hpp"   // resolved via the Core/Panel include dir (see CMakeLists.txt)

using Lightnet::AnimationPlayer;

static inline AnimationPlayer *self(anim_handle h)
{
    return static_cast<AnimationPlayer *>(h);
}

extern "C" {
anim_handle anim_create(void)
{
    return new AnimationPlayer();
}

void        anim_destroy(anim_handle h)
{
    delete self(h);
}

int anim_prepare(anim_handle h, const uint8_t *bytes, int len)
{
    if (!h || !bytes || len < (int)sizeof(::Protocol::PacketAnimationPrepare)) return 0;

    self(h)->prepare(reinterpret_cast<const ::Protocol::PacketAnimationPrepare *>(bytes));

    return 1;
}

void anim_start(anim_handle h, uint8_t seq_id, uint8_t group_id, uint16_t now)
{
    if (h) self(h)->start(seq_id, group_id, now);
}

void anim_control(anim_handle h, uint8_t cmd, uint8_t group_id, uint16_t now)
{
    if (h) self(h)->control(cmd, group_id, now);
}

void anim_update_params(
    anim_handle h,
    uint8_t     seq_id,
    uint8_t     group_id,
    uint8_t     param_type,
    uint8_t     value,
    uint8_t     transition_ms,
    uint16_t    now
)
{
    if (h) self(h)->updateParams(seq_id, group_id, param_type, value, transition_ms, now);
}

int anim_set_palette(anim_handle h, const uint8_t *bytes, int len)
{
    // Length-tolerant: a mirrored SET_PALETTE may carry only `count` stops, not the full 16-slot
    // struct. Read count, then bound the stop array by the bytes actually present.
    const int META = (int)sizeof(::Protocol::PacketMeta);

    if (!h || !bytes || len < META + 1) return 0;

    uint8_t count = bytes[META];
    int maxStops = (len - (META + 1)) / (int)sizeof(Lightnet::GradientStop);

    if ((int)count > maxStops) count = (uint8_t)((maxStops < 0) ? 0 : maxStops);

    // bytes[META+1 ..] is already a packed GradientStop[] (pos,r,g,b) — same layout as the wire.
    self(h)->setPalette(reinterpret_cast<const Lightnet::GradientStop *>(bytes + META + 1), count);

    return 1;
}

int anim_set_base_colors(anim_handle h, const uint8_t *bytes, int len)
{
    const int META = (int)sizeof(::Protocol::PacketMeta);

    if (!h || !bytes || len < META + (int)(Lightnet::BASE_COLORS_COUNT *sizeof(::Protocol::ColorRGB)))  return 0;

    // bytes[META ..] is the packed ColorRGB[BASE_COLORS_COUNT] (same layout as PacketSetBaseColors.colors).
    self(h)->setBaseColors(reinterpret_cast<const ::Protocol::ColorRGB *>(bytes + META));

    return 1;
}

void anim_set_background(anim_handle h, uint8_t r, uint8_t g, uint8_t b)
{
    if (h) self(h)->setBackground(::Protocol::ColorRGB{ r, g, b });
}

void anim_set_color_direct(anim_handle h, uint8_t r, uint8_t g, uint8_t b)
{
    if (h) self(h)->setColorDirect(::Protocol::ColorRGB{ r, g, b });
}

void anim_tick(anim_handle h, uint16_t now)
{
    if (h) self(h)->tick(now);
}

void anim_get_color(anim_handle h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!h) return;

    ::Protocol::ColorRGB c = self(h)->currentColor();

    if (r) *r = c.r;

    if (g) *g = c.g;

    if (b) *b = c.b;
}

int anim_take_dirty(anim_handle h)
{
    return (h && self(h)->takeDirty())   ? 1 : 0;
}

int anim_is_animating(anim_handle h)
{
    return (h && self(h)->isAnimating()) ? 1 : 0;
}
}  // extern "C"
