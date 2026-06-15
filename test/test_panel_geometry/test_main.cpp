// Native unit tests for lib/Lightnet/Core/Controller/PanelGeometry.hpp
// Run with: pio test -e native -f test_panel_geometry
//
// A chain of three triangles (all N=3), anchored at panel 1:
//
//        panel1.edge1 — panel2.edge0 ,  panel2.edge1 — panel3.edge0
//
// Hand-computed centroids in the canonical (visualizer) frame, edgeLength 100:
//   P1 = (50.0, 28.87)   P2 = (100.0, 57.74)   P3 = (150.0, 28.87)
// (matching the mobile PanelsLayoutService layout the user authors against).

#include <unity.h>
#include <string.h>
#include "Core/Controller/PanelGraph.hpp"
#include "Core/Controller/PanelGeometry.hpp"
#include "Core/Controller/TopologyIndex.hpp"

using namespace Lightnet;

static const uint8_t PANELS[]     = { 1, 2, 3 };
static const uint8_t EDGECOUNTS[] = { 3, 3, 3 };

static const TopoLink LINKS[] = {
    { 1, 1, 2, 0 },
    { 2, 1, 3, 0 },
};

static PanelGraph graph;
static PanelGeometry geo;
static TopologyIndex topo; // chain 1—2—3 rooted at panel 1: leaves are panels 1 and 3

void test_centroids_match_visualizer_frame()
{
    float x, y;

    TEST_ASSERT_TRUE(geo.centroidOf(1, x, y));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 28.87f, y);

    TEST_ASSERT_TRUE(geo.centroidOf(2, x, y));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 57.74f, y);

    TEST_ASSERT_TRUE(geo.centroidOf(3, x, y));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 150.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 28.87f, y);
}

void test_field_horizontal_axis()
{
    // Project onto 0° (x): P1=50, P2=100, P3=150 → evenly spaced over span 100.
    uint8_t coord[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeGeometricField(geo, PANELS, 3, 0.0f, false, 4, coord);

    TEST_ASSERT_EQUAL_UINT8(4, maxC);
    TEST_ASSERT_EQUAL_UINT8(0, coord[0]);
    TEST_ASSERT_EQUAL_UINT8(2, coord[1]);
    TEST_ASSERT_EQUAL_UINT8(4, coord[2]);
}

void test_field_reverse_flips()
{
    uint8_t coord[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeGeometricField(geo, PANELS, 3, 0.0f, true, 4, coord);

    TEST_ASSERT_EQUAL_UINT8(4, maxC);
    TEST_ASSERT_EQUAL_UINT8(4, coord[0]);
    TEST_ASSERT_EQUAL_UINT8(2, coord[1]);
    TEST_ASSERT_EQUAL_UINT8(0, coord[2]);
}

void test_field_vertical_axis_is_2d()
{
    // Project onto 90° (y): P1=P3=28.87 (source), P2=57.74 (far) → a genuinely 2-D field,
    // impossible with graph hop-distance alone.
    uint8_t coord[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeGeometricField(geo, PANELS, 3, 90.0f, false, 4, coord);

    TEST_ASSERT_EQUAL_UINT8(4, maxC);
    TEST_ASSERT_EQUAL_UINT8(0, coord[0]);
    TEST_ASSERT_EQUAL_UINT8(4, coord[1]);
    TEST_ASSERT_EQUAL_UINT8(0, coord[2]);
}

void test_single_panel_is_uniform()
{
    static const uint8_t one[]  = { 1 };
    static const uint8_t oneEc[] = { 3 };
    PanelGraph g1;
    PanelGeometry g;

    TEST_ASSERT_TRUE(g1.build(one, 1, nullptr, 0));
    TEST_ASSERT_TRUE(g.build(g1, oneEc, 0));

    uint8_t coord[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeGeometricField(g, one, 1, 30.0f, false, 4, coord);

    TEST_ASSERT_EQUAL_UINT8(0, maxC);   // zero span → no gradient
    TEST_ASSERT_EQUAL_UINT8(0, coord[0]);
}

void test_empty_build_is_invalid()
{
    PanelGraph g1;
    PanelGeometry g;

    g1.build(nullptr, 0, nullptr, 0);
    TEST_ASSERT_FALSE(g.build(g1, nullptr, 0));
    TEST_ASSERT_FALSE(g.valid());
}

// ---- Radial (ripple) field: each panel spans a [near, far] band = (centroid distance to the
// nearest source) ∓ the panel's circumradius (N=3 ⇒ R≈57.74), quantised by the largest far edge.

void test_center_field_from_root()
{
    // source:root = panel 1 (50,28.87). Centroid dists: P1=0, P2=57.74, P3=100; R≈57.74.
    // bands [near,far]: P1[0,57.7] P2[0,115.5] P3[42.3,157.7]; maxFar=157.7 → /157.7 ×4.
    uint8_t near[LIGHTNET_MAX_PANELS], far[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeGeometricCenterField(geo, topo, PANELS, 3,
                                               SRC_ROOT, 0, false, 4, near, far);

    TEST_ASSERT_EQUAL_UINT8(4, maxC);
    TEST_ASSERT_EQUAL_UINT8(0, near[0]);
    TEST_ASSERT_EQUAL_UINT8(1, far[0]);
    TEST_ASSERT_EQUAL_UINT8(0, near[1]);
    TEST_ASSERT_EQUAL_UINT8(3, far[1]);
    TEST_ASSERT_EQUAL_UINT8(1, near[2]);
    TEST_ASSERT_EQUAL_UINT8(4, far[2]);
}

void test_center_field_leaves_is_multi_source()
{
    // Rooted at the centre (panel 2), the leaves are panels 1 and 3. Each panel takes the MIN
    // distance to either leaf, so both ends sit at the near ring and the centre reaches the far
    // ring — two ripples converging from the leaves (the multi-source feature). Geometry is
    // root-independent. Bands: P1[0,57.7] P2[0,115.5] P3[0,57.7]; maxFar=115.5 → /115.5 ×4.
    TopologyIndex centreRooted;

    centreRooted.build(graph, 2);

    uint8_t near[LIGHTNET_MAX_PANELS], far[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeGeometricCenterField(geo, centreRooted, PANELS, 3,
                                               SRC_LEAVES, 0, false, 4, near, far);

    TEST_ASSERT_EQUAL_UINT8(4, maxC);
    TEST_ASSERT_EQUAL_UINT8(0, near[0]);
    TEST_ASSERT_EQUAL_UINT8(2, far[0]);
    TEST_ASSERT_EQUAL_UINT8(0, near[1]);
    TEST_ASSERT_EQUAL_UINT8(4, far[1]);
    TEST_ASSERT_EQUAL_UINT8(0, near[2]);
    TEST_ASSERT_EQUAL_UINT8(2, far[2]);
}

void test_center_field_panel_and_reverse()
{
    // source:panel:2 (centre). Bands (unreversed): P1[0,4] P2[0,2] P3[0,4] over maxFar=115.5.
    // reverse maps [n,f] → [maxC-f, maxC-n], so the rings collapse toward the source.
    uint8_t near[LIGHTNET_MAX_PANELS], far[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeGeometricCenterField(geo, topo, PANELS, 3,
                                               SRC_PANEL, 2, true, 4, near, far);

    TEST_ASSERT_EQUAL_UINT8(4, maxC);
    TEST_ASSERT_EQUAL_UINT8(0, near[0]);
    TEST_ASSERT_EQUAL_UINT8(4, far[0]);
    TEST_ASSERT_EQUAL_UINT8(2, near[1]);
    TEST_ASSERT_EQUAL_UINT8(4, far[1]);
    TEST_ASSERT_EQUAL_UINT8(0, near[2]);
    TEST_ASSERT_EQUAL_UINT8(4, far[2]);
}

void test_center_field_all_ripples_from_geometric_center()
{
    // source:all collapses to ONE ripple from the geometric centre — the average centroid
    // (100, 38.49), not "every panel is its own source" (which would light all bands [0,maxC]
    // uniformly → all-at-once). Centroid dists to centre: P1≈50.9, P2≈19.2, P3≈50.9; R≈57.74.
    // bands: P1[0,108.7] P2[0,77.0] P3[0,108.7]; maxFar≈108.7 → /108.7 ×4.
    // The centre panel (P2) sits closest, so its far ring is smaller — a real spatial gradient.
    uint8_t near[LIGHTNET_MAX_PANELS], far[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeGeometricCenterField(geo, topo, PANELS, 3,
                                               SRC_ALL, 0, false, 4, near, far);

    TEST_ASSERT_EQUAL_UINT8(4, maxC);
    TEST_ASSERT_EQUAL_UINT8(0, near[0]);
    TEST_ASSERT_EQUAL_UINT8(4, far[0]);
    TEST_ASSERT_EQUAL_UINT8(0, near[1]);
    TEST_ASSERT_EQUAL_UINT8(3, far[1]); // centre panel: closest to the centre → smaller far ring
    TEST_ASSERT_EQUAL_UINT8(0, near[2]);
    TEST_ASSERT_EQUAL_UINT8(4, far[2]);
}

// ---- Angular (wheel) field: each panel's bearing in turns [0,1), counter-clockwise
// from +x, from the centre = average centroid of the source set (single-panel here).

void test_wheel_field_from_panel_center()
{
    // source:panel:2 (centre = P2 = (100,57.74)). P1 bears 210° (turns≈0.583), P2 — the
    // centre itself — is pinned to 0 (sits at the hub), P3 bears 330° (turns≈0.917).
    float turns[LIGHTNET_MAX_PANELS];

    TEST_ASSERT_TRUE(computeWheelField(geo, topo, PANELS, 3, SRC_PANEL, 2, false, turns));

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5833f, turns[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, turns[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9167f, turns[2]);
}

void test_wheel_field_reverse_flips_bearing()
{
    // reverse maps t → 1-t, preserving the [0,1) invariant (0 stays 0 — the hub doesn't flip).
    float turns[LIGHTNET_MAX_PANELS];

    TEST_ASSERT_TRUE(computeWheelField(geo, topo, PANELS, 3, SRC_PANEL, 2, true, turns));

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.4167f, turns[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, turns[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0833f, turns[2]);
}

void test_wheel_field_from_root()
{
    // source:root (centre = P1 = (50,28.87), the hub — pinned to 0). P2 bears 30°
    // (turns≈0.083); P3 lies due east of P1 at the same height → bearing 0.
    float turns[LIGHTNET_MAX_PANELS];

    TEST_ASSERT_TRUE(computeWheelField(geo, topo, PANELS, 3, SRC_ROOT, 0, false, turns));

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, turns[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0833f, turns[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, turns[2]);
}

void test_wheel_field_no_overlap_returns_false()
{
    // A valid geometry built over a disjoint panel set (e.g. a different device): none of
    // the chain's panels — including its root, the only possible source — have a centroid
    // here, so the centre is undefined and the field can't be computed.
    static const uint8_t other[]   = { 9 };
    static const uint8_t otherEc[] = { 3 };
    PanelGraph g1;
    PanelGeometry g;

    TEST_ASSERT_TRUE(g1.build(other, 1, nullptr, 0));
    TEST_ASSERT_TRUE(g.build(g1, otherEc, 0));

    float turns[LIGHTNET_MAX_PANELS];

    TEST_ASSERT_FALSE(computeWheelField(g, topo, PANELS, 3, SRC_ROOT, 0, false, turns));
}

void test_world_verts_match_centroid()
{
    float vx[8], vy[8];
    uint8_t n;

    TEST_ASSERT_TRUE(geo.worldVertsOf(1, vx, vy, n));
    TEST_ASSERT_EQUAL_UINT8(3, n);

    float cx = (vx[0] + vx[1] + vx[2]) / 3.0f;
    float cy = (vy[0] + vy[1] + vy[2]) / 3.0f;

    float ex, ey;

    TEST_ASSERT_TRUE(geo.centroidOf(1, ex, ey));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, ex, cx);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, ey, cy);
}

void test_world_verts_missing_panel()
{
    float vx[8], vy[8];
    uint8_t n;

    TEST_ASSERT_FALSE(geo.worldVertsOf(9, vx, vy, n));
}

void test_adjacent_panels_touch_not_overlap()
{
    // Panels 1 and 2 share a seam — layout() places them flush, not overlapping.
    float v1x[8], v1y[8], v2x[8], v2y[8];
    uint8_t n1, n2;

    TEST_ASSERT_TRUE(geo.worldVertsOf(1, v1x, v1y, n1));
    TEST_ASSERT_TRUE(geo.worldVertsOf(2, v2x, v2y, n2));

    TEST_ASSERT_FALSE(convexPolygonsOverlap(v1x, v1y, n1, v2x, v2y, n2));
}

void test_chain_ends_dont_overlap()
{
    // Panels 1 and 3 sit two hops apart in the zig-zag chain — their circumcircles are
    // close enough to intersect, but the actual triangles don't (real layouts shouldn't
    // false-positive on a simple chain).
    float v1x[8], v1y[8], v3x[8], v3y[8];
    uint8_t n1, n3;

    TEST_ASSERT_TRUE(geo.worldVertsOf(1, v1x, v1y, n1));
    TEST_ASSERT_TRUE(geo.worldVertsOf(3, v3x, v3y, n3));

    TEST_ASSERT_FALSE(convexPolygonsOverlap(v1x, v1y, n1, v3x, v3y, n3));
}

void test_coincident_polygons_overlap()
{
    float vx[8], vy[8];
    uint8_t n;

    TEST_ASSERT_TRUE(geo.worldVertsOf(1, vx, vy, n));
    TEST_ASSERT_TRUE(convexPolygonsOverlap(vx, vy, n, vx, vy, n));
}

void test_shifted_polygon_overlap_detected()
{
    // Nudge panel 1's triangle by an amount well inside its ~58-unit circumradius —
    // a genuine interior overlap, not merely a shared seam.
    float vx[8], vy[8];
    uint8_t n;

    TEST_ASSERT_TRUE(geo.worldVertsOf(1, vx, vy, n));

    float sx[8], sy[8];

    for (uint8_t i = 0; i < n; i++) {
        sx[i] = vx[i] + 10.0f;
        sy[i] = vy[i] + 10.0f;
    }

    TEST_ASSERT_TRUE(convexPolygonsOverlap(vx, vy, n, sx, sy, n));
}

void setUp(void)
{
    graph.build(PANELS, 3, LINKS, 2);
    geo.build(graph, EDGECOUNTS, 0); // anchor 0 → lowest index (panel 1)
    topo.build(graph, 0);            // root 0 → lowest index (panel 1)
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_centroids_match_visualizer_frame);
    RUN_TEST(test_field_horizontal_axis);
    RUN_TEST(test_field_reverse_flips);
    RUN_TEST(test_field_vertical_axis_is_2d);
    RUN_TEST(test_single_panel_is_uniform);
    RUN_TEST(test_empty_build_is_invalid);
    RUN_TEST(test_center_field_from_root);
    RUN_TEST(test_center_field_leaves_is_multi_source);
    RUN_TEST(test_center_field_panel_and_reverse);
    RUN_TEST(test_center_field_all_ripples_from_geometric_center);

    RUN_TEST(test_wheel_field_from_panel_center);
    RUN_TEST(test_wheel_field_reverse_flips_bearing);
    RUN_TEST(test_wheel_field_from_root);
    RUN_TEST(test_wheel_field_no_overlap_returns_false);

    RUN_TEST(test_world_verts_match_centroid);
    RUN_TEST(test_world_verts_missing_panel);
    RUN_TEST(test_adjacent_panels_touch_not_overlap);
    RUN_TEST(test_chain_ends_dont_overlap);
    RUN_TEST(test_coincident_polygons_overlap);
    RUN_TEST(test_shifted_polygon_overlap_detected);

    return UNITY_END();
}
