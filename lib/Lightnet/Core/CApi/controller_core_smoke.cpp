/* controller_core_smoke.cpp — host smoke test for the scene-engine C ABI (controller_core_c.h).
 *
 * Mirrors panel_core's panel_core_smoke.cpp: proves the whole scene engine round-trips on the host with no
 * hardware. Registers a 3-panel tree, loads a one-layer SOLID scene, and checks the engine emits
 * the expected PREPARE-per-panel + general-call START packets through the sink callback — exactly
 * the packets the mobile app will feed into its per-panel panel_core players for an offline preview.
 */

#include "controller_core_c.h"

#include <cstdio>
#include <cstring>

// Protocol packet type ids (ProtocolTypes.hpp): ANIMATION_PREPARE = 12, ANIMATION_START = 13.
static int prepareCount = 0;
static int startCount   = 0;

static void onPacket(void *user, uint8_t address, uint8_t type, const uint8_t *bytes, uint8_t len)
{
    (void)user; (void)address; (void)bytes; (void)len;
    if (type == 12) prepareCount++;
    else if (type == 13) startCount++;
}

int main()
{
    scene_handle h = scene_create();
    scene_set_sink(h, onPacket, nullptr);

    // 3-panel line: 1 -- 2 -- 3 (square panels), rooted at 1.
    const uint8_t indices[3]    = { 1, 2, 3 };
    const uint8_t edgeCounts[3] = { 4, 4, 4 };
    const uint8_t links[8]      = { 1, 0, 2, 2,   2, 0, 3, 2 }; // 2 links * {pA,eA,pB,eB}
    scene_set_topology(h, indices, 3, links, 2, edgeCounts, 1);

    const char *json = R"({
      "name": "smoke", "loop": false,
      "layers": [ { "group": 1, "panels": "all", "sequence": [
        { "type": "SOLID", "color": "#FF0000", "duration": 1000 }
      ] } ]
    })";

    if (!scene_load_and_play(h, json, (int)strlen(json), 0)) {
        printf("FAIL: scene_load_and_play: %s\n", scene_last_error(h));
        scene_destroy(h);
        return 1;
    }

    int rc = 0;
    if (prepareCount != 3) { printf("FAIL: expected 3 PREPARE, got %d\n", prepareCount); rc = 1; }
    if (startCount < 1)    { printf("FAIL: expected >=1 START, got %d\n", startCount);   rc = 1; }
    if (!scene_is_playing(h)) { printf("FAIL: scene not playing after load\n");          rc = 1; }

    if (rc == 0) printf("scene smoke OK (PREPARE=%d START=%d)\n", prepareCount, startCount);

    scene_destroy(h);
    return rc;
}
