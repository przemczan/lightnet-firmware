// Native unit tests for lib/Lightnet/Controller/Topology/PanelSelectorParser.hpp
// Run with: pio test -e native -f test_panel_selector_parser
//
// End-to-end: parse a JSON "panels" value → PanelSelector RPN → resolve against
// the worked topology (docs/design/scene-portability.md §2, rooted at panel 1)
// → assert the emitted panel list.

#include <unity.h>
#include <string.h>
#include "Controller/Topology/PanelSelectorParser.hpp"

using namespace Lightnet;

static const uint8_t PANELS[] = { 1, 2, 3, 4, 5, 6 };

static const TopoLink LINKS[] = {
    { 1, 0, 2, 0 },
    { 1, 1, 3, 0 },
    { 2, 1, 4, 0 },
    { 3, 1, 5, 0 },
    { 5, 1, 6, 0 },
};

static TopologyIndex topo;

static void parseResolve(const char *json, const uint8_t *expect, uint8_t en)
{
    const char *p   = json;
    const char *end = json + strlen(json);
    char err[64] = { 0 };

    PanelSelector sel;
    bool ok = parsePanelSelector(p, end, sel, err, sizeof(err));

    TEST_ASSERT_TRUE_MESSAGE(ok, err);

    PanelSet set;

    TEST_ASSERT_TRUE(resolveSelector(sel, topo, set));

    uint8_t out[LIGHTNET_MAX_PANELS];
    uint8_t c;

    emitPanelIndices(set, topo, out, LIGHTNET_MAX_PANELS, c);

    TEST_ASSERT_EQUAL_UINT8(en, c);

    for (uint8_t i = 0; i < en; i++) TEST_ASSERT_EQUAL_UINT8(expect[i], out[i]);
}

static void parseExpectError(const char *json)
{
    const char *p   = json;
    const char *end = json + strlen(json);
    char err[64] = { 0 };

    PanelSelector sel;

    TEST_ASSERT_FALSE(parsePanelSelector(p, end, sel, err, sizeof(err)));
}

static void parseOk(const char *json)
{
    const char *p   = json;
    const char *end = json + strlen(json);
    char err[64] = { 0 };

    PanelSelector sel;

    TEST_ASSERT_TRUE_MESSAGE(parsePanelSelector(p, end, sel, err, sizeof(err)), err);
}

// ---------------------------------------------------------------------------
// Single string tokens
// ---------------------------------------------------------------------------

void test_all()
{
    uint8_t e[] = { 1, 2, 3, 4, 5, 6 };

    parseResolve("\"all\"", e, sizeof(e));
}

void test_root()
{
    uint8_t e[] = { 1 };

    parseResolve("\"root\"", e, sizeof(e));
}

void test_leaves()
{
    uint8_t e[] = { 4, 6 };

    parseResolve("\"leaves\"", e, sizeof(e));
}

void test_branches()
{
    uint8_t e[] = { 1 };

    parseResolve("\"branches\"", e, sizeof(e));
}

void test_even()
{
    uint8_t e[] = { 1, 4, 5 };

    parseResolve("\"even\"", e, sizeof(e));
}

void test_odd()
{
    uint8_t e[] = { 2, 3, 6 };

    parseResolve("\"odd\"", e, sizeof(e));
}

void test_depth_single()
{
    uint8_t e[] = { 2, 3 };

    parseResolve("\"depth:1\"", e, sizeof(e));
}

void test_depth_range()
{
    uint8_t e[] = { 2, 3, 4, 5 };

    parseResolve("\"depth:1-2\"", e, sizeof(e));
}

void test_depth_zero()
{
    uint8_t e[] = { 1 };

    parseResolve("\"depth:0\"", e, sizeof(e));
}

void test_subtree()
{
    uint8_t e[] = { 3, 5, 6 };

    parseResolve("\"subtree:3\"", e, sizeof(e));
}

void test_neighbors()
{
    uint8_t e[] = { 1, 5 };

    parseResolve("\"neighbors:3\"", e, sizeof(e));
}

void test_first()
{
    uint8_t e[] = { 1, 2 };

    parseResolve("\"first:2\"", e, sizeof(e));
}

void test_last()
{
    uint8_t e[] = { 5, 6 };

    parseResolve("\"last:2\"", e, sizeof(e));
}

void test_fraction()
{
    uint8_t e[] = { 1, 2, 4 };

    parseResolve("\"fraction:0-0.5\"", e, sizeof(e));
}

// ---------------------------------------------------------------------------
// Arrays & composition
// ---------------------------------------------------------------------------

void test_index_array()
{
    uint8_t e[] = { 1, 3, 5 };

    parseResolve("[1,3,5]", e, sizeof(e));
}

void test_exclude()
{
    uint8_t e[] = { 1, 3, 4, 5, 6 };

    parseResolve("{\"exclude\":[2]}", e, sizeof(e));
}

void test_any()
{
    uint8_t e[] = { 1, 4, 6 };

    parseResolve("{\"any\":[\"root\",\"leaves\"]}", e, sizeof(e));
}

void test_all_op()
{
    uint8_t e[] = { 6 };

    parseResolve("{\"all\":[\"subtree:3\",\"leaves\"]}", e, sizeof(e));
}

void test_not()
{
    uint8_t e[] = { 1, 2, 4 };

    parseResolve("{\"not\":\"subtree:3\"}", e, sizeof(e));
}

void test_nested()
{
    // root ∪ (leaves ∩ subtree:3) = {1} ∪ {6} = {1,6}
    uint8_t e[] = { 1, 6 };

    parseResolve("{\"any\":[\"root\",{\"all\":[\"leaves\",\"subtree:3\"]}]}", e, sizeof(e));
}

void test_whitespace_tolerant()
{
    uint8_t e[] = { 2, 3 };

    parseResolve("  \"depth:1-1\" ", e, sizeof(e));
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

void test_err_unknown_token()
{
    parseExpectError("\"bogus\"");
}

void test_err_depth_empty()
{
    parseExpectError("\"depth:\"");
}

void test_tag_parses()
{
    // tag: now parses (resolution needs a device tag resolver, tested elsewhere).
    parseOk("\"tag:accent\"");
}

void test_err_tag_invalid()
{
    parseExpectError("\"tag:bad name\""); // space is not allowed in a tag
}

void test_err_empty_object()
{
    parseExpectError("{}");
}

void test_err_empty_array()
{
    parseExpectError("[]");
}

void test_err_zero_index()
{
    parseExpectError("[0]");
}

void test_err_two_keys()
{
    parseExpectError("{\"any\":[\"root\"],\"not\":\"leaves\"}");
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

void setUp(void)
{
    topo.build(PANELS, 6, LINKS, 5, 1);
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
    RUN_TEST(test_even);
    RUN_TEST(test_odd);
    RUN_TEST(test_depth_single);
    RUN_TEST(test_depth_range);
    RUN_TEST(test_depth_zero);
    RUN_TEST(test_subtree);
    RUN_TEST(test_neighbors);
    RUN_TEST(test_first);
    RUN_TEST(test_last);
    RUN_TEST(test_fraction);

    RUN_TEST(test_index_array);
    RUN_TEST(test_exclude);
    RUN_TEST(test_any);
    RUN_TEST(test_all_op);
    RUN_TEST(test_not);
    RUN_TEST(test_nested);
    RUN_TEST(test_whitespace_tolerant);

    RUN_TEST(test_err_unknown_token);
    RUN_TEST(test_err_depth_empty);
    RUN_TEST(test_tag_parses);
    RUN_TEST(test_err_tag_invalid);
    RUN_TEST(test_err_empty_object);
    RUN_TEST(test_err_empty_array);
    RUN_TEST(test_err_zero_index);
    RUN_TEST(test_err_two_keys);

    return UNITY_END();
}
