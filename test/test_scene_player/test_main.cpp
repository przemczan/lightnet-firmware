// Host test for the shared scene engine.
//
// Proves the controller's scene orchestration (SceneParser -> ScenePlayer ->
// AnimationScheduler) runs with NO hardware: a mock IPacketSink captures the outbound
// packets, a mock ITopologyProvider supplies a 3-panel tree, and a mock IPaletteResolver
// stands in for the filesystem palette store. This is exactly what the mobile app will do
// to preview a scene without a controller.
//
// Run with: pio test -e native -f test_scene_player

#include <unity.h>
#include <string.h>

#include "Core/Controller/ScenePlayer.hpp"
#include "Core/Controller/SceneParser.hpp"
#include "Core/Controller/AnimationScheduler.hpp"
#include "Core/Controller/IPacketSink.hpp"
#include "Core/Controller/IPaletteResolver.hpp"
#include "Core/Common/AnimationTypes.hpp"

using namespace Lightnet;

// --- Mocks -----------------------------------------------------------------

struct CapturedPacket {
    uint8_t address;
    uint8_t type;
    uint8_t size;
    bool    wantAck;
    uint8_t groupId; // PACKET_ANIMATION_START / PREPARE / CONTROL
    uint8_t flags;   // PACKET_ANIMATION_PREPARE only
    uint8_t cmd;     // PACKET_ANIMATION_CONTROL only (ANIM_CTRL_*)
};

struct MockSink : public IPacketSink {
    static const int MAX = 256;
    CapturedPacket   pkts[MAX];
    int              count = 0;
    int              paceForHopCount = 0;

    void send(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size, bool wantAck) override
    {
        if (count < MAX) {
            uint8_t groupId = 0;
            uint8_t flags   = 0;
            uint8_t cmd     = 0;

            if (packet->header.type == Protocol::PACKET_ANIMATION_START
                && size >= sizeof(Protocol::PacketAnimationStart)) {
                groupId = ((const Protocol::PacketAnimationStart *)packet)->group_id;
            } else if (packet->header.type == Protocol::PACKET_ANIMATION_PREPARE
                       && size >= sizeof(Protocol::PacketAnimationPrepare)) {
                const auto *prep = (const Protocol::PacketAnimationPrepare *)packet;

                groupId = prep->group_id;
                flags   = prep->flags;
            } else if (packet->header.type == Protocol::PACKET_ANIMATION_CONTROL
                       && size >= sizeof(Protocol::PacketAnimationControl)) {
                const auto *ctrl = (const Protocol::PacketAnimationControl *)packet;

                groupId = ctrl->group_id;
                cmd     = ctrl->cmd;
            }

            pkts[count] = { address, (uint8_t)packet->header.type, size, wantAck, groupId, flags, cmd };
            count++;
        }
    }

    // pace(microseconds) inherited as a no-op — no bus to settle here. paceForHop() is the
    // per-hop pacing the real relay sink applies after every unicast acked send; count the calls
    // so a test can assert every unicast is paced (no relay collision / retransmit storm).
    void paceForHop(uint8_t /*packetSize*/) override
    {
        paceForHopCount++;
    }

    int countOfType(uint8_t t) const
    {
        int n = 0;

        for (int i = 0; i < count; i++) if (pkts[i].type == t) n++;

        return n;
    }

    int stopCountForGroup(uint8_t group, int from = 0) const
    {
        int n = 0;

        for (int i = from; i < count; i++) {
            if (pkts[i].type == Protocol::PACKET_ANIMATION_CONTROL
                && pkts[i].cmd == ANIM_CTRL_STOP
                && pkts[i].groupId == group) n++;
        }

        return n;
    }

    int prepareCountForGroup(uint8_t group, int from = 0) const
    {
        int n = 0;

        for (int i = from; i < count; i++) {
            if (pkts[i].type == Protocol::PACKET_ANIMATION_PREPARE && pkts[i].groupId == group) n++;
        }

        return n;
    }

    // Unicast = addressed to one panel (address != 0); general calls use address 0.
    int unicastSendCount() const
    {
        int n = 0;

        for (int i = 0; i < count; i++) if (pkts[i].address != 0) n++;

        return n;
    }
};

// No palettes registered → ScenePlayer falls back to the synthesized userColors palette.
struct MockPalette : public IPaletteResolver {
    bool resolve(const char *name, GradientStop *out, uint8_t &outCount) const override
    {
        (void)name;
        (void)out;
        outCount = 0;

        return false;
    }
};

// A 3-panel line: 1 -- 2 -- 3 (square panels), rooted at 1.
struct MockTopo : public ITopologyProvider {
    uint8_t fillTopology(uint8_t *indices, uint8_t *edgeCounts, TopoLink *links, uint8_t maxLinks, uint8_t &linkCount) const override
    {
        indices[0] = 1;
        indices[1] = 2;
        indices[2] = 3;
        edgeCounts[0] = 4;
        edgeCounts[1] = 4;
        edgeCounts[2] = 4;

        linkCount = 0;

        if (maxLinks >= 2) {
            links[0].panelA = 1;
            links[0].edgeA = 0;
            links[0].panelB = 2;
            links[0].edgeB = 2;
            links[1].panelA = 2;
            links[1].edgeA = 0;
            links[1].panelB = 3;
            links[1].edgeB = 2;
            linkCount = 2;
        }

        return 3;
    }
};

// --- Tests -----------------------------------------------------------------

static const char *SOLID_SCENE =
    R"({
  "name": "host",
  "loop": false,
  "colors": { "primary": "#FF0000", "secondary": "#00FF00", "tertiary": "#0000FF" },
  "layers": [
    { "group": 1, "panels": "all", "sequence": [
        { "type": "SOLID", "color": "#FF0000", "duration": 1000 }
    ] }
  ]
})";

// A whole scene parses, plays, and emits packets through the sink with no hardware.
void test_solid_scene_emits_prepare_and_start()
{
    SceneRecord res = {};
    char errMsg[64];
    bool ok = parseScene(SOLID_SCENE, strlen(SOLID_SCENE), res, errMsg, sizeof(errMsg));

    TEST_ASSERT_TRUE_MESSAGE(ok, errMsg);
    TEST_ASSERT_EQUAL_UINT8(1, res.layerCount);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;

    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(
        res.layers,
        res.layerCount,
        res.loop,
        res.palette,
        res.baseColors,                             /*nowMs=*/
        0,
        res.speed,
        res.background
    );

    // "all" resolves to the 3 mock panels → one PREPARE each. The general-call START is
    // deliberately sent redundantly for reliability (shared seq_id), so it appears 3 times.
    TEST_ASSERT_EQUAL_INT(3, sink.countOfType(Protocol::PACKET_ANIMATION_PREPARE));
    TEST_ASSERT_EQUAL_INT(3, sink.countOfType(Protocol::PACKET_ANIMATION_START));
    // Scene start also pushes the compositor base + a clearing black.
    TEST_ASSERT_EQUAL_INT(1, sink.countOfType(Protocol::PACKET_SET_BACKGROUND));
    TEST_ASSERT_TRUE(player.isPlaying());
}

// stop() broadcasts a general-call ANIM_CTRL_STOP and clears the playing flag.
void test_stop_emits_control_and_clears_playing()
{
    SceneRecord res = {};
    char errMsg[64];

    parseScene(SOLID_SCENE, strlen(SOLID_SCENE), res, errMsg, sizeof(errMsg));

    MockSink sink;
    MockPalette palette;
    MockTopo topo;
    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(
        res.layers,
        res.layerCount,
        res.loop,
        res.palette,
        res.baseColors,
        0,
        res.speed,
        res.background
    );

    int before = sink.count;

    player.stop();

    TEST_ASSERT_FALSE(player.isPlaying());
    TEST_ASSERT_TRUE(sink.count > before);
    TEST_ASSERT_TRUE(sink.countOfType(Protocol::PACKET_ANIMATION_CONTROL) >= 1);
}

static const char *USER_BARRIER_SCENE =
    R"({
  "schemaVersion": 7,
  "id": "4gszvyz0",
  "name": "test",
  "loop": true,
  "layers": [
    {
      "group": "layer3",
      "panels": "all",
      "sequence": [
        {
          "type": "BREATHE",
          "colorFrom": "#0000AA",
          "color": "#0000FF",
          "duration": 3180,
          "pingpong": true
        }
      ]
    },
    {
      "group": "layer1",
      "panels": "all",
      "sequence": [
        {
          "runner": "CHASE",
          "color": { "palette": 255 },
          "duration": 1000,
          "source": "root"
        }
      ]
    },
    {
      "group": "layer2",
      "panels": "all",
      "disabled": true,
      "sequence": [
        {
          "runner": "WAVE",
          "color": { "palette": 128 },
          "duration": 2038,
          "width": 5,
          "directionality": "geometric",
          "angle": 314
        }
      ]
    },
    {
      "group": "layer4",
      "panels": "all",
      "blend": "add",
      "disabled": true,
      "sequence": [
        { "duration": 764 },
        {
          "runner": "SPARKLE",
          "color": "#FF0000",
          "duration": 1274,
          "width": 255,
          "waves": 4,
          "source": "root"
        }
      ]
    }
  ]
})";

// layer1 (CHASE, 1s) must not re-fire until layer3 (BREATHE, 3180ms) completes.
void test_user_scene_sync_barrier()
{
    SceneRecord res = {};
    char errMsg[128];
    bool ok = parseScene(USER_BARRIER_SCENE, strlen(USER_BARRIER_SCENE), res, errMsg, sizeof(errMsg));

    TEST_ASSERT_TRUE_MESSAGE(ok, errMsg);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;
    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(
        res.layers,
        res.layerCount,
        res.loop,
        res.palette,
        res.baseColors,
        0,
        res.speed,
        res.background
    );

    uint8_t layer1Group = player.groupIdForName("layer1");

    TEST_ASSERT_NOT_EQUAL(0, layer1Group);
    TEST_ASSERT_NOT_EQUAL(0, player.groupIdForName("layer3"));

    auto isChasePoolStart = [&](const CapturedPacket& pkt) {
                                return (pkt.type == Protocol::PACKET_ANIMATION_START) && (pkt.groupId > layer1Group);
                            };

    int chaseStartsBeforeBarrier = 0;
    int chaseStartsMidCycle = 0;

    for (int i = 0; i < sink.count; i++) {
        if (isChasePoolStart(sink.pkts[i])) chaseStartsBeforeBarrier++;
    }

    for (uint32_t t = 1; t <= 4000; t++) {
        int before = sink.count;

        player.tick(t);

        for (int i = before; i < sink.count; i++) {
            if (!isChasePoolStart(sink.pkts[i])) continue;

            if (t < 3180) chaseStartsMidCycle++;
        }
    }

    // One CHASE sweep at play start (duplicated START for bus reliability), then none until t=3180.
    TEST_ASSERT_TRUE(chaseStartsBeforeBarrier >= 1);
    TEST_ASSERT_EQUAL_INT(0, chaseStartsMidCycle);
    TEST_ASSERT_TRUE(player.isPlaying());
}

static const char *USER_WHEEL_BARRIER_SCENE =
    R"({
  "schemaVersion": 7,
  "name": "wheel_barrier",
  "loop": true,
  "layers": [
    {
      "group": "layer3",
      "panels": "all",
      "sequence": [
        {
          "type": "BREATHE",
          "colorFrom": "#0000AA",
          "color": "#0000FF",
          "duration": 3180,
          "pingpong": true
        }
      ]
    },
    {
      "group": "layer1",
      "panels": "all",
      "sequence": [
        {
          "runner": "WHEEL",
          "color": { "palette": 255 },
          "duration": 764,
          "thickness": 41,
          "lines": 1,
          "source": "all"
        }
      ]
    }
  ]
})";

void test_user_scene_wheel_sync_barrier()
{
    SceneRecord res = {};
    char errMsg[128];
    bool ok = parseScene(USER_WHEEL_BARRIER_SCENE, strlen(USER_WHEEL_BARRIER_SCENE), res, errMsg, sizeof(errMsg));

    TEST_ASSERT_TRUE_MESSAGE(ok, errMsg);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;
    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(
        res.layers,
        res.layerCount,
        res.loop,
        res.palette,
        res.baseColors,
        0,
        res.speed,
        res.background
    );

    uint8_t layer1Group = player.groupIdForName("layer1");

    TEST_ASSERT_NOT_EQUAL(0, layer1Group);

    for (int i = 0; i < sink.count; i++) {
        if (sink.pkts[i].type != Protocol::PACKET_ANIMATION_PREPARE) continue;

        if (sink.pkts[i].groupId != layer1Group) continue;

        TEST_ASSERT_EQUAL_UINT8(FLAG_LOOP, sink.pkts[i].flags & FLAG_LOOP);
    }

    int wheelStartsMidCycle = 0;

    for (uint32_t t = 1; t <= 4000; t++) {
        int before = sink.count;

        player.tick(t);

        for (int i = before; i < sink.count; i++) {
            if (sink.pkts[i].type != Protocol::PACKET_ANIMATION_START) continue;

            if (sink.pkts[i].groupId != layer1Group) continue;

            if (t < 3180) wheelStartsMidCycle++;
        }
    }

    TEST_ASSERT_EQUAL_INT(0, wheelStartsMidCycle);
    TEST_ASSERT_TRUE(player.isPlaying());
}

static const char *BOUNCE_ROUND_TRIP_SCENE =
    R"({
  "schemaVersion": 7,
  "name": "bounce",
  "loop": true,
  "layers": [{
    "group": "b1",
    "panels": "all",
    "sequence": [{
      "runner": "BOUNCE",
      "color": "#FF0000",
      "duration": 800,
      "width": 2,
      "source": "root"
    }]
  }]
})";

// BOUNCE must reflect within the step window (forward + reverse), not wait for scene loop.
void test_bounce_reverse_fires_mid_step()
{
    SceneRecord res = {};
    char errMsg[128];
    bool ok = parseScene(BOUNCE_ROUND_TRIP_SCENE, strlen(BOUNCE_ROUND_TRIP_SCENE), res, errMsg, sizeof(errMsg));

    TEST_ASSERT_TRUE_MESSAGE(ok, errMsg);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;
    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(
        res.layers,
        res.layerCount,
        res.loop,
        res.palette,
        res.baseColors,
        0,
        res.speed,
        res.background
    );

    uint8_t bounceGroup = player.groupIdForName("b1");

    TEST_ASSERT_NOT_EQUAL(0, bounceGroup);

    auto isBounceStart = [&](const CapturedPacket& pkt) {
                             return (pkt.type == Protocol::PACKET_ANIMATION_START) && (pkt.groupId == bounceGroup);
                         };

    int startsBeforeMid = 0;

    for (int i = 0; i < sink.count; i++) {
        if (isBounceStart(sink.pkts[i])) startsBeforeMid++;
    }

    TEST_ASSERT_TRUE(startsBeforeMid >= 1);

    int startsMidStep = 0;

    for (uint32_t t = 1; t < 400; t++) {
        int before = sink.count;

        player.tick(t);

        for (int i = before; i < sink.count; i++) {
            if (isBounceStart(sink.pkts[i])) startsMidStep++;
        }
    }

    TEST_ASSERT_EQUAL_INT(0, startsMidStep);

    int before = sink.count;

    player.tick(400);

    int reverseStarts = 0;

    for (int i = before; i < sink.count; i++) {
        if (isBounceStart(sink.pkts[i])) reverseStarts++;
    }

    TEST_ASSERT_TRUE(reverseStarts >= 1);
}

// Every unicast (addressed) acked send must be followed by exactly one hop-clear pace, so a
// multi-panel burst can't outrun the tree's store-and-forward drain and collide/retransmit. The
// solid scene resolves "all" to 3 panels → 3 unicast PREPAREs, each paced by sendPrepareToPanel().
void test_unicast_sends_are_paced()
{
    SceneRecord res = {};
    char errMsg[64];
    bool ok = parseScene(SOLID_SCENE, strlen(SOLID_SCENE), res, errMsg, sizeof(errMsg));

    TEST_ASSERT_TRUE_MESSAGE(ok, errMsg);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;

    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(
        res.layers,
        res.layerCount,
        res.loop,
        res.palette,
        res.baseColors,                             /*nowMs=*/
        0,
        res.speed,
        res.background
    );

    TEST_ASSERT_EQUAL_INT(3, sink.countOfType(Protocol::PACKET_ANIMATION_PREPARE));
    // Core invariant: exactly one hop-clear pace per unicast (addressed) send, and none on the
    // general-call floods. Holds regardless of how many unicast sends a scene load emits.
    TEST_ASSERT_TRUE(sink.paceForHopCount > 0);
    TEST_ASSERT_EQUAL_INT(sink.unicastSendCount(), sink.paceForHopCount);
}

static const char *LOOP_SEAM_SCENE =
    R"({
  "schemaVersion": 7,
  "name": "loop seam",
  "loop": true,
  "layers": [
    { "group": "breathe", "panels": "all", "sequence": [
        { "type": "BREATHE", "colorFrom": "#FB00FF", "color": "#609DFF", "duration": 1000, "pingpong": true }
    ] },
    { "group": "chase", "panels": "all", "sequence": [
        { "runner": "CHASE", "color": "#00FF00", "duration": 1000, "source": "root" }
    ] }
  ]
})";

// Loop seam: a layer the barrier re-arms in the same tick must NOT be stopped first. Its
// PREPARE reuses the group's panel slot, so stopping would free the slot and leave the panel
// composing nothing for that layer until START lands — a black flash every cycle.
void test_loop_restart_does_not_stop_refired_layer()
{
    SceneRecord res = {};
    char errMsg[128];
    bool ok = parseScene(LOOP_SEAM_SCENE, strlen(LOOP_SEAM_SCENE), res, errMsg, sizeof(errMsg));

    TEST_ASSERT_TRUE_MESSAGE(ok, errMsg);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;
    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(res.layers, res.layerCount, res.loop, res.palette, res.baseColors, 0, res.speed, res.background);

    uint8_t breatheGroup = player.groupIdForName("breathe");
    uint8_t chaseGroup   = player.groupIdForName("chase");

    TEST_ASSERT_NOT_EQUAL(0, breatheGroup);
    TEST_ASSERT_NOT_EQUAL(0, chaseGroup);

    int beforeSeam = sink.count;

    for (uint32_t t = 1; t <= 1200; t++) {
        player.tick(t);
    }

    TEST_ASSERT_EQUAL_INT(0, sink.stopCountForGroup(breatheGroup, beforeSeam));
    TEST_ASSERT_EQUAL_INT(0, sink.stopCountForGroup(chaseGroup, beforeSeam));
    // The restart still re-arms the layer: one PREPARE per resolved panel.
    TEST_ASSERT_EQUAL_INT(3, sink.prepareCountForGroup(breatheGroup, beforeSeam));
    TEST_ASSERT_TRUE(player.isPlaying());
}

static const char *GATED_LOOP_SCENE =
    R"({
  "schemaVersion": 7,
  "name": "gated loop",
  "loop": true,
  "layers": [
    { "group": "lead", "panels": "all", "sequence": [
        { "type": "SOLID", "color": "#FF0000", "duration": 500 }
    ] },
    { "group": "follow", "panels": "all", "startAfter": "lead", "sequence": [
        { "type": "SOLID", "color": "#0000FF", "duration": 500 }
    ] }
  ]
})";

// A layer the restart leaves gated (startAfter) is re-armed as WAITING, not fired, so it still
// owes its panels the STOP — otherwise its last frame would keep covering lower layers until
// its dependency releases it.
void test_loop_restart_stops_gated_layer()
{
    SceneRecord res = {};
    char errMsg[128];
    bool ok = parseScene(GATED_LOOP_SCENE, strlen(GATED_LOOP_SCENE), res, errMsg, sizeof(errMsg));

    TEST_ASSERT_TRUE_MESSAGE(ok, errMsg);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;
    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(res.layers, res.layerCount, res.loop, res.palette, res.baseColors, 0, res.speed, res.background);

    uint8_t leadGroup   = player.groupIdForName("lead");
    uint8_t followGroup = player.groupIdForName("follow");

    TEST_ASSERT_NOT_EQUAL(0, leadGroup);
    TEST_ASSERT_NOT_EQUAL(0, followGroup);

    // t=500: "lead" finishes while "follow" is still to run — the barrier can't trip, so the
    // STOP goes out immediately (one per resolved panel).
    for (uint32_t t = 1; t <= 500; t++) {
        player.tick(t);
    }

    TEST_ASSERT_EQUAL_INT(3, sink.stopCountForGroup(leadGroup));

    // t=1000: both done → loop restart. "lead" re-fires (no STOP), "follow" goes back to
    // WAITING and must be stopped.
    int beforeSeam = sink.count;

    for (uint32_t t = 501; t <= 1000; t++) {
        player.tick(t);
    }

    TEST_ASSERT_EQUAL_INT(3, sink.stopCountForGroup(followGroup, beforeSeam));
    TEST_ASSERT_EQUAL_INT(0, sink.stopCountForGroup(leadGroup, beforeSeam));
    TEST_ASSERT_EQUAL_INT(3, sink.prepareCountForGroup(leadGroup, beforeSeam));
}

static const char *MIXED_SEQUENCE_LOOP_SCENE =
    R"({
  "schemaVersion": 7,
  "name": "mixed sequence",
  "loop": true,
  "layers": [
    { "group": "mixed", "panels": "all", "sequence": [
        { "runner": "CHASE", "color": "#00FF00", "duration": 500, "source": "root" },
        { "type": "SOLID", "color": "#FF0000", "duration": 500 }
    ] }
  ]
})";

// Suppression at the seam is keyed on the slot being reclaimed, not merely on the layer being
// re-armed. Here the last step (SOLID) holds the layer's slot but step 0 (CHASE) spawns on
// pooled group_ids, so the restart never replaces it — the STOP must still go out, or the
// frozen SOLID frame would cover lower layers for the whole of step 0 on every later cycle.
void test_loop_restart_stops_layer_whose_first_step_uses_no_slot()
{
    SceneRecord res = {};
    char errMsg[128];
    bool ok = parseScene(MIXED_SEQUENCE_LOOP_SCENE, strlen(MIXED_SEQUENCE_LOOP_SCENE), res, errMsg, sizeof(errMsg));

    TEST_ASSERT_TRUE_MESSAGE(ok, errMsg);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;
    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(res.layers, res.layerCount, res.loop, res.palette, res.baseColors, 0, res.speed, res.background);

    uint8_t mixedGroup = player.groupIdForName("mixed");

    TEST_ASSERT_NOT_EQUAL(0, mixedGroup);

    // t=500 hands over to SOLID, which PREPAREs on the layer's own group.
    for (uint32_t t = 1; t <= 500; t++) {
        player.tick(t);
    }

    TEST_ASSERT_EQUAL_INT(3, sink.prepareCountForGroup(mixedGroup));

    int beforeSeam = sink.count;

    for (uint32_t t = 501; t <= 1000; t++) {
        player.tick(t);
    }

    TEST_ASSERT_EQUAL_INT(3, sink.stopCountForGroup(mixedGroup, beforeSeam));
}

void setUp()
{
}

void tearDown()
{
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_solid_scene_emits_prepare_and_start);
    RUN_TEST(test_stop_emits_control_and_clears_playing);
    RUN_TEST(test_user_scene_sync_barrier);
    RUN_TEST(test_user_scene_wheel_sync_barrier);
    RUN_TEST(test_bounce_reverse_fires_mid_step);
    RUN_TEST(test_unicast_sends_are_paced);
    RUN_TEST(test_loop_restart_does_not_stop_refired_layer);
    RUN_TEST(test_loop_restart_stops_gated_layer);
    RUN_TEST(test_loop_restart_stops_layer_whose_first_step_uses_no_slot);

    return UNITY_END();
}
