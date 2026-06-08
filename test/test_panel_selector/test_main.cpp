// Native unit tests for lib/Lightnet/Controller/Topology/PanelSelector.hpp
// Run with: pio test -e native -f test_panel_selector
//
// Same worked topology as test_topology (docs/design/scene-portability.md §2),
// rooted at panel 1. emitPanelIndices yields panels in slot (discovery) order,
// which for this fixture is ascending panel index.

#include <unity.h>
#include <string.h>
#include "Controller/Topology/PanelGraph.hpp"
#include "Controller/Topology/PanelSelector.hpp"

using namespace Lightnet;

static const uint8_t PANELS[] = { 1, 2, 3, 4, 5, 6 };

static const TopoLink LINKS[] = {
    { 1, 0, 2, 0 },
    { 1, 1, 3, 0 },
    { 2, 1, 4, 0 },
    { 3, 1, 5, 0 },
    { 5, 1, 6, 0 },
};

static PanelGraph graph;
static TopologyIndex topo;

// Mock tag resolver: "accent" → panels 1 and 5.
struct MockTags : ITagResolver {
    void panelsForTag(const char *name, const TopologyIndex& t, PanelSet& out) const override
    {
        if (strcmp(name, "accent") == 0) {
            uint8_t s;

            if (t.slotOf(1, s)) out.set(s);

            if (t.slotOf(5, s)) out.set(s);
        }
    }
};

static MockTags mockTags;

static void emitTag(PanelSelector& sel, const char *name)
{
    sel.emit(SEL_TAG);
    sel.emit((uint8_t)strlen(name));

    for (const char *p = name; *p; p++) sel.emit((uint8_t)*p);
}

// Resolve `ops` and assert the emitted panel list equals `expect`.
static void check(const uint8_t *ops, uint8_t n, const uint8_t *expect, uint8_t en)
{
    PanelSelector sel;

    sel.clear();

    for (uint8_t i = 0; i < n; i++) TEST_ASSERT_TRUE(sel.emit(ops[i]));

    PanelSet set;

    TEST_ASSERT_TRUE(resolveSelector(sel, topo, set));

    uint8_t out[LIGHTNET_MAX_PANELS];
    uint8_t c;

    emitPanelIndices(set, topo, out, LIGHTNET_MAX_PANELS, c);

    TEST_ASSERT_EQUAL_UINT8(en, c);

    for (uint8_t i = 0; i < en; i++) TEST_ASSERT_EQUAL_UINT8(expect[i], out[i]);
}

// ---------------------------------------------------------------------------
// Single selectors
// ---------------------------------------------------------------------------

void test_all()
{
    uint8_t ops[] = { SEL_ALL };
    uint8_t e[]   = { 1, 2, 3, 4, 5, 6 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_root()
{
    uint8_t ops[] = { SEL_ROOT };
    uint8_t e[]   = { 1 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_leaves()
{
    uint8_t ops[] = { SEL_LEAVES };
    uint8_t e[]   = { 4, 6 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_branches()
{
    uint8_t ops[] = { SEL_BRANCHES };
    uint8_t e[]   = { 1 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_even_odd()
{
    // canonical positions: p1=0 p2=1 p4=2 p3=3 p5=4 p6=5
    uint8_t even[] = { SEL_EVEN };
    uint8_t ee[]   = { 1, 4, 5 };

    check(even, sizeof(even), ee, sizeof(ee));

    uint8_t odd[] = { SEL_ODD };
    uint8_t eo[]  = { 2, 3, 6 };

    check(odd, sizeof(odd), eo, sizeof(eo));
}

void test_depth()
{
    uint8_t d11[] = { SEL_DEPTH, 1, 1 };
    uint8_t e11[] = { 2, 3 };

    check(d11, sizeof(d11), e11, sizeof(e11));

    uint8_t d12[] = { SEL_DEPTH, 1, 2 };
    uint8_t e12[] = { 2, 3, 4, 5 };

    check(d12, sizeof(d12), e12, sizeof(e12));

    uint8_t d00[] = { SEL_DEPTH, 0, 0 };
    uint8_t e00[] = { 1 };

    check(d00, sizeof(d00), e00, sizeof(e00));
}

void test_subtree()
{
    uint8_t ops[] = { SEL_SUBTREE, 3 };
    uint8_t e[]   = { 3, 5, 6 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_neighbors()
{
    uint8_t n3[] = { SEL_NEIGHBORS, 3 };
    uint8_t e3[] = { 1, 5 };

    check(n3, sizeof(n3), e3, sizeof(e3));

    uint8_t n1[] = { SEL_NEIGHBORS, 1 };
    uint8_t e1[] = { 2, 3 };

    check(n1, sizeof(n1), e1, sizeof(e1));
}

void test_first_last()
{
    uint8_t f2[] = { SEL_FIRST, 2 };
    uint8_t ef[] = { 1, 2 };

    check(f2, sizeof(f2), ef, sizeof(ef));

    uint8_t l2[] = { SEL_LAST, 2 };
    uint8_t el[] = { 5, 6 };

    check(l2, sizeof(l2), el, sizeof(el));
}

void test_fraction()
{
    // fraction:0-0.5 ≈ [0, 128/255] → canonical positions 0,1,2 → panels 1,2,4
    uint8_t ops[] = { SEL_FRACTION, 0, 128 };
    uint8_t e[]   = { 1, 2, 4 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_indices()
{
    uint8_t ops[] = { SEL_INDICES, 3, 1, 3, 5 };
    uint8_t e[]   = { 1, 3, 5 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_indices_skips_missing()
{
    // panel 9 doesn't exist on this device → skipped
    uint8_t ops[] = { SEL_INDICES, 2, 1, 9 };
    uint8_t e[]   = { 1 };

    check(ops, sizeof(ops), e, sizeof(e));
}

// ---------------------------------------------------------------------------
// Composition
// ---------------------------------------------------------------------------

void test_or()
{
    uint8_t ops[] = { SEL_ROOT, SEL_LEAVES, SEL_OR };
    uint8_t e[]   = { 1, 4, 6 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_and()
{
    uint8_t ops[] = { SEL_SUBTREE, 3, SEL_LEAVES, SEL_AND };
    uint8_t e[]   = { 6 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_not()
{
    uint8_t ops[] = { SEL_SUBTREE, 3, SEL_NOT };
    uint8_t e[]   = { 1, 2, 4 };

    check(ops, sizeof(ops), e, sizeof(e));
}

// ---------------------------------------------------------------------------
// Tags (resolved via an injected ITagResolver)
// ---------------------------------------------------------------------------

void test_tag_with_resolver()
{
    PanelSelector sel;

    sel.clear();
    emitTag(sel, "accent");

    PanelSet set;

    TEST_ASSERT_TRUE(resolveSelector(sel, topo, set, &mockTags));

    uint8_t out[LIGHTNET_MAX_PANELS], c;

    emitPanelIndices(set, topo, out, LIGHTNET_MAX_PANELS, c);

    TEST_ASSERT_EQUAL_UINT8(2, c);
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);
    TEST_ASSERT_EQUAL_UINT8(5, out[1]);
}

void test_tag_unknown_is_empty()
{
    PanelSelector sel;

    sel.clear();
    emitTag(sel, "nope");

    PanelSet set;

    TEST_ASSERT_TRUE(resolveSelector(sel, topo, set, &mockTags));
    TEST_ASSERT_EQUAL_UINT8(0, set.popcount(topo.count()));
}

void test_tag_no_resolver_is_empty()
{
    PanelSelector sel;

    sel.clear();
    emitTag(sel, "accent");

    PanelSet set;

    TEST_ASSERT_TRUE(resolveSelector(sel, topo, set)); // no resolver passed
    TEST_ASSERT_EQUAL_UINT8(0, set.popcount(topo.count()));
}

void test_tag_composition()
{
    // accent ∪ leaves = {1,5} ∪ {4,6} = {1,4,5,6}
    PanelSelector sel;

    sel.clear();
    emitTag(sel, "accent");
    sel.emit(SEL_LEAVES);
    sel.emit(SEL_OR);

    PanelSet set;

    TEST_ASSERT_TRUE(resolveSelector(sel, topo, set, &mockTags));

    uint8_t out[LIGHTNET_MAX_PANELS], c;

    emitPanelIndices(set, topo, out, LIGHTNET_MAX_PANELS, c);

    uint8_t e[] = { 1, 4, 5, 6 };

    TEST_ASSERT_EQUAL_UINT8(4, c);

    for (uint8_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT8(e[i], out[i]);
}

void test_tag_truncated_rejected()
{
    // Claims 5 name bytes but only 1 is present → malformed, rejected (no overread).
    PanelSelector sel;

    sel.clear();
    sel.emit(SEL_TAG);
    sel.emit(5);
    sel.emit('a');

    PanelSet set;

    TEST_ASSERT_FALSE(resolveSelector(sel, topo, set, &mockTags));
}

// ---------------------------------------------------------------------------
// Backward compatibility: v2 forms via their RPN mapping
// ---------------------------------------------------------------------------

void test_v2_all()
{
    uint8_t ops[] = { SEL_ALL };
    uint8_t e[]   = { 1, 2, 3, 4, 5, 6 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_v2_index_list()
{
    uint8_t ops[] = { SEL_INDICES, 3, 1, 3, 5 };
    uint8_t e[]   = { 1, 3, 5 };

    check(ops, sizeof(ops), e, sizeof(e));
}

void test_v2_exclude()
{
    // {"exclude":[2]} → all panels minus {2}
    uint8_t ops[] = { SEL_INDICES, 1, 2, SEL_NOT };
    uint8_t e[]   = { 1, 3, 4, 5, 6 };

    check(ops, sizeof(ops), e, sizeof(e));
}

// ---------------------------------------------------------------------------
// Malformed programs are rejected
// ---------------------------------------------------------------------------

void test_rejects_truncated_operand()
{
    PanelSelector sel;

    sel.clear();
    sel.emit(SEL_DEPTH);
    sel.emit(1); // missing second operand

    PanelSet set;

    TEST_ASSERT_FALSE(resolveSelector(sel, topo, set));
}

void test_rejects_dangling_binary_op()
{
    PanelSelector sel;

    sel.clear();
    sel.emit(SEL_ROOT);
    sel.emit(SEL_AND); // only one operand on the stack

    PanelSet set;

    TEST_ASSERT_FALSE(resolveSelector(sel, topo, set));
}

void test_rejects_empty_program()
{
    PanelSelector sel;

    sel.clear();

    PanelSet set;

    TEST_ASSERT_FALSE(resolveSelector(sel, topo, set));
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

void setUp(void)
{
    graph.build(PANELS, 6, LINKS, 5);
    topo.build(graph, 1);
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_all);
    RUN_TEST(test_root);
    RUN_TEST(test_leaves);
    RUN_TEST(test_branches);
    RUN_TEST(test_even_odd);
    RUN_TEST(test_depth);
    RUN_TEST(test_subtree);
    RUN_TEST(test_neighbors);
    RUN_TEST(test_first_last);
    RUN_TEST(test_fraction);
    RUN_TEST(test_indices);
    RUN_TEST(test_indices_skips_missing);

    RUN_TEST(test_or);
    RUN_TEST(test_and);
    RUN_TEST(test_not);

    RUN_TEST(test_tag_with_resolver);
    RUN_TEST(test_tag_unknown_is_empty);
    RUN_TEST(test_tag_no_resolver_is_empty);
    RUN_TEST(test_tag_composition);
    RUN_TEST(test_tag_truncated_rejected);

    RUN_TEST(test_v2_all);
    RUN_TEST(test_v2_index_list);
    RUN_TEST(test_v2_exclude);

    RUN_TEST(test_rejects_truncated_operand);
    RUN_TEST(test_rejects_dangling_binary_op);
    RUN_TEST(test_rejects_empty_program);

    return UNITY_END();
}
