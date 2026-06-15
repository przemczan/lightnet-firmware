/* controller_core_c.cpp — implementation of the scene-engine C ABI (see controller_core_c.h).
 *
 * Wraps a ScenePlayer + AnimationScheduler and the three pure seams the engine resolves
 * against: a callback IPacketSink (forwards emitted packets to the caller), an in-memory
 * IPaletteResolver, an in-memory ITagResolver, and an in-memory ITopologyProvider. No Arduino,
 * no filesystem, no bus — the same engine the controller runs, driven entirely from data.
 */

#include "controller_core_c.h"

#include <string.h>
#include <vector>

#include "../Controller/ScenePlayer.hpp"
#include "../Controller/SceneParser.hpp"
#include "../Controller/AnimationScheduler.hpp"
#include "../Controller/IPacketSink.hpp"
#include "../Controller/IPaletteResolver.hpp"
#include "../Controller/SceneTopology.hpp"  // ITopologyProvider, TopoLink
#include "../Controller/TagResolver.hpp"    // ITagResolver, TopologyIndex, PanelSet

using namespace Lightnet;

namespace {
    constexpr int SC_MAX_PALETTES = 16;
    constexpr int SC_MAX_TAGS     = 64;
    constexpr int SC_NAME_LEN     = 16;

    // IPacketSink that accumulates each packet into a MIRROR_BATCH record buffer (drained by
    // scene_drain) and optionally forwards it live to a callback. pace() is a no-op (no bus).
    struct BufferSink : public IPacketSink {
        std::vector<uint8_t> *buf   = nullptr;  // record bytes: {addr, type, size, payload...}
        uint16_t *            count = nullptr;
        scene_packet_cb       cb    = nullptr;
        void *                user  = nullptr;

        void send(uint8_t address, Protocol::packetType_t type, const void *packet, uint8_t size, bool wantAck) override
        {
            (void)wantAck;
            const uint8_t *b = (const uint8_t *)packet;
            buf->push_back(address);
            buf->push_back((uint8_t)type);
            buf->push_back(size);
            buf->insert(buf->end(), b, b + size);
            (*count)++;

            if (cb) cb(user, address, (uint8_t)type, b, size);
        }
    };

    // In-memory named-palette resolver. "userColors" is synthesized by ScenePlayer, not here.
    struct RegPalettes : public IPaletteResolver {
        struct Entry {
            char         name[SC_NAME_LEN];
            GradientStop stops[PALETTE_STOPS];
            uint8_t      count;
        };
        Entry entries[SC_MAX_PALETTES];
        int   n = 0;

        bool resolve(const char *name, GradientStop *out, uint8_t &outCount) const override
        {
            for (int i = 0; i < n; i++) {
                if (strcmp(entries[i].name, name) == 0) {
                    outCount = entries[i].count;

                    for (uint8_t k = 0; k < entries[i].count && k < PALETTE_STOPS; k++) out[k] = entries[i].stops[k];

                    return true;
                }
            }

            return false;
        }

        void add(const char *name, const GradientStop *stops, uint8_t count)
        {
            if (!name || n >= SC_MAX_PALETTES) return;

            if (count > PALETTE_STOPS) count = PALETTE_STOPS;

            // Replace an existing entry of the same name, else append.
            int slot = n;

            for (int i = 0; i < n; i++) if (strcmp(entries[i].name, name) == 0) {
                    slot = i;
                    break;
                }

            if (slot == n) n++;

            strncpy(entries[slot].name, name, SC_NAME_LEN - 1);
            entries[slot].name[SC_NAME_LEN - 1] = '\0';
            entries[slot].count = count;

            for (uint8_t k = 0; k < count; k++) entries[slot].stops[k] = stops[k];
        }

        void clear()
        {
            n = 0;
        }
    };

    // In-memory tag resolver: tag name -> 1-based panel indices, mapped to slots via topo.
    struct RegTags : public ITagResolver {
        struct Entry {
            char    name[SC_NAME_LEN];
            uint8_t panel;
        };
        Entry entries[SC_MAX_TAGS];
        int   n = 0;

        void panelsForTag(const char *name, const TopologyIndex &topo, PanelSet &out) const override
        {
            for (int i = 0; i < n; i++) {
                if (strcmp(entries[i].name, name) == 0) {
                    uint8_t sl;

                    if (topo.slotOf(entries[i].panel, sl)) out.set(sl);
                }
            }
        }

        void add(const char *name, const uint8_t *panels, uint8_t count)
        {
            if (!name || !panels) return;

            for (uint8_t k = 0; k < count && n < SC_MAX_TAGS; k++) {
                strncpy(entries[n].name, name, SC_NAME_LEN - 1);
                entries[n].name[SC_NAME_LEN - 1] = '\0';
                entries[n].panel = panels[k];
                n++;
            }
        }

        void clear()
        {
            n = 0;
        }
    };

    // In-memory topology provider: the panel tree supplied by scene_set_topology.
    struct RegTopology : public ITopologyProvider {
        uint8_t  indices[LIGHTNET_MAX_PANELS];
        uint8_t  edgeCounts[LIGHTNET_MAX_PANELS];
        TopoLink links[LIGHTNET_MAX_PANELS];
        uint8_t  count     = 0;
        uint8_t  linkCount = 0;

        uint8_t fillTopology(uint8_t *idx, uint8_t *ec, TopoLink *lk, uint8_t maxLinks, uint8_t &lc) const override
        {
            for (uint8_t i = 0; i < count; i++) {
                idx[i] = indices[i];
                ec[i] = edgeCounts[i];
            }

            lc = (linkCount <= maxLinks) ? linkCount : maxLinks;

            for (uint8_t i = 0; i < lc; i++) lk[i] = links[i];

            return count;
        }
    };

    struct SceneCore {
        BufferSink           sink;
        RegPalettes          palettes;
        RegTags              tags;
        RegTopology          topo;
        AnimationScheduler   scheduler;
        ScenePlayer          player;
        SceneParseResult     parse; // transient parse buffer
        char                 err[64];

        std::vector<uint8_t> outbuf;  // accumulated MIRROR_BATCH records since the last drain
        uint16_t             outCount = 0;
        uint32_t             lastNow  = 0;

        SceneCore()
            : scheduler(sink), player(scheduler, palettes, topo)
        {
            err[0]     = '\0';
            sink.buf   = &outbuf;
            sink.count = &outCount;
            player.setTagResolver(&tags);
        }

        void resetBatch(uint32_t now)
        {
            outbuf.clear();
            outCount = 0;
            lastNow = now;
        }
    };

    inline SceneCore *self(scene_handle h)
    {
        return reinterpret_cast<SceneCore *>(h);
    }
}  // namespace

extern "C" {
scene_handle scene_create(void)
{
    return reinterpret_cast<scene_handle>(new SceneCore());
}

void scene_destroy(scene_handle h)
{
    delete self(h);
}

void scene_set_sink(scene_handle h, scene_packet_cb cb, void *user)
{
    if (!h) return;

    self(h)->sink.cb   = cb;
    self(h)->sink.user = user;
}

void scene_set_topology(
    scene_handle   h,
    const uint8_t *indices,
    uint8_t        count,
    const uint8_t *links,
    uint8_t        link_count,
    const uint8_t *edge_counts,
    uint8_t        logical_root
)
{
    if (!h) return;

    SceneCore *c = self(h);

    if (count > LIGHTNET_MAX_PANELS) count = LIGHTNET_MAX_PANELS;

    if (link_count > LIGHTNET_MAX_PANELS) link_count = LIGHTNET_MAX_PANELS;

    c->topo.count     = count;
    c->topo.linkCount = link_count;

    for (uint8_t i = 0; i < count; i++) {
        c->topo.indices[i]    = indices ? indices[i] : 0;
        c->topo.edgeCounts[i] = edge_counts ? edge_counts[i] : 0;
    }

    for (uint8_t i = 0; i < link_count; i++) {
        c->topo.links[i].panelA = links[i * 4 + 0];
        c->topo.links[i].edgeA  = links[i * 4 + 1];
        c->topo.links[i].panelB = links[i * 4 + 2];
        c->topo.links[i].edgeB  = links[i * 4 + 3];
    }

    // Apply the rooting (rebuilds the views from the tree just stored; no scene playing yet).
    c->player.setLogicalRoot(logical_root, 0);
}

void scene_set_palette(scene_handle h, const char *name, const uint8_t *stops, uint8_t count)
{
    if (!h || !name || !stops) return;

    // stops: count * {pos, r, g, b} — the on-wire GradientStop layout.
    self(h)->palettes.add(name, reinterpret_cast<const GradientStop *>(stops), count);
}

void scene_clear_palettes(scene_handle h)
{
    if (h) self(h)->palettes.clear();
}

void scene_set_tag(scene_handle h, const char *name, const uint8_t *panels, uint8_t count)
{
    if (h) self(h)->tags.add(name, panels, count);
}

void scene_clear_tags(scene_handle h)
{
    if (h) self(h)->tags.clear();
}

int scene_load_and_play(scene_handle h, const char *json, int len, uint32_t now)
{
    if (!h || !json || len <= 0) return 0;

    SceneCore *c = self(h);

    if (!parseScene(json, (size_t)len, c->parse) || !c->parse.valid) {
        strncpy(c->err, c->parse.errMsg, sizeof(c->err) - 1);
        c->err[sizeof(c->err) - 1] = '\0';

        return 0;
    }

    c->err[0] = '\0';
    c->resetBatch(now);
    c->player.loadAndPlay(
        c->parse.layers, c->parse.layerCount, c->parse.loop, c->parse.name,
        c->parse.palette, c->parse.baseColors, now, c->parse.speed, c->parse.background
    );

    return 1;
}

void scene_tick(scene_handle h, uint32_t now)
{
    if (!h) return;

    self(h)->resetBatch(now);
    self(h)->player.tick(now);
}

void scene_stop(scene_handle h, uint32_t now)
{
    if (!h) return;

    self(h)->resetBatch(now);
    self(h)->player.stop();
}

int scene_drain(scene_handle h, uint8_t *out, int maxlen)
{
    if (!h) return 0;

    SceneCore *c = self(h);

    int len = 6 + (int)c->outbuf.size(); // u32 millis + u16 count + records

    if (!out || maxlen < len) return len; // report size; leave the buffer intact

    out[0] = (uint8_t)(c->lastNow);
    out[1] = (uint8_t)(c->lastNow >> 8);
    out[2] = (uint8_t)(c->lastNow >> 16);
    out[3] = (uint8_t)(c->lastNow >> 24);
    out[4] = (uint8_t)(c->outCount);
    out[5] = (uint8_t)(c->outCount >> 8);

    if (!c->outbuf.empty()) memcpy(out + 6, c->outbuf.data(), c->outbuf.size());

    c->outbuf.clear();
    c->outCount = 0;

    return len;
}

void scene_set_speed(scene_handle h, float speed)
{
    if (h) self(h)->player.setSpeed(speed);
}

int scene_is_playing(scene_handle h)
{
    return h && self(h)->player.isPlaying() ? 1 : 0;
}

const char *scene_last_error(scene_handle h)
{
    return h ? self(h)->err : "";
}
}  // extern "C"
