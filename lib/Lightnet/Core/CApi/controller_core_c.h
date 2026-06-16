/* controller_core_c.h — C ABI over the portable Lightnet SCENE engine.
 *
 * Companion to panel_core_c.h. Where panel_core runs ONE panel's animation player, controller_core
 * runs the whole controller-side scene orchestrator (SceneParser -> ScenePlayer ->
 * AnimationScheduler -> runners) with no hardware, so the mobile app can preview a whole
 * scene WITHOUT a controller. It produces exactly the same wire packets the controller would
 * mirror; the host feeds them back into the per-panel panel_core players it already drives for
 * the live preview, so offline preview and live preview share one render path.
 *
 * Usage:
 *   h = scene_create();
 *   scene_set_sink(h, cb, user);                       // receive emitted packets
 *   scene_set_topology(h, idx, n, links, lc, ec, root);// the panel tree (cached or virtual)
 *   scene_set_palette(h, "fire", stops, count);        // any named palettes the scene uses
 *   scene_set_tag(h, "left", panels, count);           // any device tags the scene targets
 *   scene_load_and_play(h, json, len, now);            // parse + start -> emits packets
 *   ... each frame: scene_tick(h, now);                // advances steps -> emits packets
 *   scene_destroy(h);
 *
 * Packets are delivered through the sink callback as raw wire bytes (PacketMeta header
 * included), identical to a MIRROR_BATCH record's { address, type, payload }. `address` 0 is a
 * general call (all panels). Time is a 32-bit millisecond counter supplied by the caller.
 * Not thread-safe; drive one handle from one thread.
 */
#ifndef CONTROLLER_CORE_C_H
#define CONTROLLER_CORE_C_H

#include <stdint.h>

#ifdef __cplusplus
    extern "C" {
#endif

/* Opaque handle to one scene engine (one virtual controller). */
typedef void *scene_handle;

scene_handle scene_create(void);
void         scene_destroy(scene_handle h);

/* Every emitted packet is also accumulated into an internal MIRROR_BATCH buffer; drain it after
 * each load/tick/stop with scene_drain() (the simple path the mobile bindings use). Optionally also
 * forward each packet live to a callback. `bytes`/`len` are the full wire packet (PacketMeta header
 * included); address 0 = general call. */
typedef void (*scene_packet_cb)(void *user, uint8_t address, uint8_t type, const uint8_t *bytes, uint8_t len);
void scene_set_sink(scene_handle h, scene_packet_cb cb, void *user);

/* Copy the packets emitted since the last drain into `out` as a MIRROR_BATCH payload
 * (u32 millis = the `now` of the last load/tick/stop, u16 count, then count records of
 * {u8 address, u8 type, u8 size, u8[size]}) and clear the buffer. Returns the payload length.
 * If `out` is null or `maxlen` is too small, writes nothing and leaves the buffer intact — call
 * scene_drain(h, NULL, 0) first to size the buffer. This is the same layout decodeMirrorBatch parses,
 * so offline preview reuses the live-preview decode + per-panel render path verbatim. */
int  scene_drain(scene_handle h, uint8_t *out, int maxlen);

/* Supply the panel tree the scene resolves selectors against (cached from a controller or a
 * user-authored virtual tree). `indices`/`edge_counts` are `count` bytes each (per panel).
 * `links` is `link_count` * 4 bytes: {panelA, edgeA, panelB, edgeB}. `logical_root` is the
 * 1-based panel the rooted view is built from (0 -> physical root). */
void scene_set_topology(
    scene_handle    h,
    const uint8_t * indices,
    uint8_t         count,
    const uint8_t * links,
    uint8_t         link_count,
    const uint8_t * edge_counts,
    uint8_t         logical_root
);

/* Register a named palette. `stops` is `count` * 4 bytes: {pos, r, g, b}. Replaces a palette
 * of the same name. "userColors" is synthesized from the scene's base colors, not registered. */
void scene_set_palette(scene_handle h, const char *name, const uint8_t *stops, uint8_t count);
void scene_clear_palettes(scene_handle h);

/* Register a device tag -> the 1-based panel indices carrying it. */
void scene_set_tag(scene_handle h, const char *name, const uint8_t *panels, uint8_t count);
void scene_clear_tags(scene_handle h);

/* Parse `len` bytes of scene JSON and start playing. Emits the scene-start packets through the
 * sink. Returns 1 on success, 0 on parse/validation failure (see scene_last_error). */
int  scene_load_and_play(scene_handle h, const char *json, int len, uint32_t now);

/* Advance the playing scene by one frame; emits any step/spawn packets due. */
void scene_tick(scene_handle h, uint32_t now);

/* Stop playback (broadcasts a general-call STOP); `now` timestamps the drained batch. */
void scene_stop(scene_handle h, uint32_t now);

/* Playback speed multiplier [0.1, 10.0]; takes effect on the next step. */
void scene_set_speed(scene_handle h, float speed);

/* Re-resolve layer palettes while a scene is playing (mirrors ScenePlayer::reresolvePalettes).
 * Pass NULL / empty palette or NULL baseColors to leave that aspect unchanged. baseColors, when
 * set, is 9 bytes: primary/secondary/tertiary RGB. */
void scene_reresolve_palettes(scene_handle h, const char *palette, const uint8_t *baseColors);

int  scene_is_playing(scene_handle h);

/* Human-readable message from the last failed scene_load_and_play (empty if none). */
const char *scene_last_error(scene_handle h);

#ifdef __cplusplus
    }
#endif

#endif /* CONTROLLER_CORE_C_H */
