// Native unit tests for lib/Lightnet/Controller/Topology/PanelGeometry.hpp
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
#include "Controller/Topology/PanelGraph.hpp"
#include "Controller/Topology/PanelGeometry.hpp"
#include "Controller/Topology/TopologyIndex.hpp"

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

    return UNITY_END();
}
