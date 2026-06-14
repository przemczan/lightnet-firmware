/* panel_core_c.h — C ABI over the portable Lightnet animation core.
 *
 * One flat C surface that both Android (NDK/JNI) and iOS (Kotlin/Native cinterop) can bind to.
 * It wraps Lightnet::AnimationPlayer (lib/Lightnet/Core/Anim). The animation MATH lives in C++;
 * clock-domain translation / resync / mirror plumbing stay in the mobile Kotlin wrapper.
 *
 * Time is a 16-bit millisecond counter (wraps ~65.5 s) supplied by the caller — same contract as
 * the firmware. Packet entry points take the RAW wire bytes (the on-wire little-endian packed
 * layout, PacketMeta header included) and reuse the firmware struct layout to decode — no
 * re-serialization on the mobile side.
 *
 * Lifecycle: anim_create() -> ... -> anim_destroy(). Not thread-safe; drive one handle from one
 * thread (e.g. the preview tick loop).
 */
#ifndef PANEL_CORE_C_H
#define PANEL_CORE_C_H

#include <stdint.h>

#ifdef __cplusplus
    extern "C" {
#endif

/* Opaque handle to one AnimationPlayer (one virtual panel). */
typedef void *anim_handle;

anim_handle anim_create(void);
void        anim_destroy(anim_handle h);

/* Packet handlers — `bytes`/`len` are the full wire packet (PacketMeta header included).
 * Return 1 if applied, 0 if rejected (null/short buffer). `now` = 16-bit ms. */
int  anim_prepare(anim_handle h, const uint8_t *bytes, int len);          /* PacketAnimationPrepare */
void anim_start(anim_handle h, uint8_t seq_id, uint8_t group_id, uint16_t now);
void anim_control(anim_handle h, uint8_t cmd, uint8_t group_id, uint16_t now);
void anim_update_params(
    anim_handle h,
    uint8_t     seq_id,
    uint8_t     group_id,
    uint8_t     param_type,
    uint8_t     value,
    uint8_t     transition_ms,
    uint16_t    now
);

int  anim_set_palette(anim_handle h, const uint8_t *bytes, int len);      /* PacketSetPalette */
int  anim_set_base_colors(anim_handle h, const uint8_t *bytes, int len);  /* PacketSetBaseColors */
void anim_set_background(anim_handle h, uint8_t r, uint8_t g, uint8_t b);
void anim_set_color_direct(anim_handle h, uint8_t r, uint8_t g, uint8_t b); /* PACKET_SET_COLOR */

/* Frame tick + output. Drive like the firmware:
 *   anim_tick(h, now); if (anim_take_dirty(h)) anim_get_color(h, &r,&g,&b);  */
void anim_tick(anim_handle h, uint16_t now);
void anim_get_color(anim_handle h, uint8_t *r, uint8_t *g, uint8_t *b);
int  anim_take_dirty(anim_handle h);    /* 1 if the colour changed since the last call */
int  anim_is_animating(anim_handle h);

#ifdef __cplusplus
    }
#endif

#endif /* PANEL_CORE_C_H */
