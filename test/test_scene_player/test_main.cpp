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

using namespace Lightnet;

// --- Mocks -----------------------------------------------------------------

struct CapturedPacket {
    uint8_t address;
    uint8_t type;
    uint8_t size;
    bool    wantAck;
};

struct MockSink : public IPacketSink {
    static const int MAX = 256;
    CapturedPacket   pkts[MAX];
    int              count = 0;

    void send(uint8_t address, Protocol::packetType_t type, const void *packet, uint8_t size, bool wantAck) override
    {
        (void)packet;

        if (count < MAX) {
            pkts[count] = { address, (uint8_t)type, size, wantAck };
            count++;
        }
    }

    // pace() inherited as a no-op — no bus to settle.

    int countOfType(uint8_t t) const
    {
        int n = 0;

        for (int i = 0; i < count; i++) if (pkts[i].type == t) n++;

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
    SceneParseResult res;
    bool ok = parseScene(SOLID_SCENE, strlen(SOLID_SCENE), res);

    TEST_ASSERT_TRUE_MESSAGE(ok, res.errMsg);
    TEST_ASSERT_TRUE(res.valid);
    TEST_ASSERT_EQUAL_UINT8(1, res.layerCount);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;

    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(res.layers, res.layerCount, res.loop, res.name,
                       res.palette, res.baseColors, /*nowMs=*/ 0, res.speed, res.background);

    // "all" resolves to the 3 mock panels → one PREPARE each. The general-call START is
    // deliberately double-sent for bus reliability (shared seq_id), so it appears twice.
    TEST_ASSERT_EQUAL_INT(3, sink.countOfType(Protocol::PACKET_ANIMATION_PREPARE));
    TEST_ASSERT_EQUAL_INT(2, sink.countOfType(Protocol::PACKET_ANIMATION_START));
    // Scene start also pushes the compositor base + a clearing black.
    TEST_ASSERT_EQUAL_INT(1, sink.countOfType(Protocol::PACKET_SET_BACKGROUND));
    TEST_ASSERT_TRUE(player.isPlaying());
}

// stop() broadcasts a general-call ANIM_CTRL_STOP and clears the playing flag.
void test_stop_emits_control_and_clears_playing()
{
    SceneParseResult res;

    parseScene(SOLID_SCENE, strlen(SOLID_SCENE), res);

    MockSink sink;
    MockPalette palette;
    MockTopo topo;
    AnimationScheduler scheduler(sink);
    ScenePlayer player(scheduler, palette, topo);

    player.loadAndPlay(res.layers, res.layerCount, res.loop, res.name,
                       res.palette, res.baseColors, 0, res.speed, res.background);

    int before = sink.count;

    player.stop();

    TEST_ASSERT_FALSE(player.isPlaying());
    TEST_ASSERT_TRUE(sink.count > before);
    TEST_ASSERT_TRUE(sink.countOfType(Protocol::PACKET_ANIMATION_CONTROL) >= 1);
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

    return UNITY_END();
}
