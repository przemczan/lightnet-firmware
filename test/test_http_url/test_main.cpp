// Native unit tests for lib/Lightnet/Controller/API/http/HttpUrl.hpp
// Run with: pio test -e native -f test_http_url

#include <unity.h>
#include <string.h>
#include "Controller/API/http/HttpUrl.hpp"

using namespace Lightnet::Http;

// ---------------------------------------------------------------------------
// isSafeName
// ---------------------------------------------------------------------------

void test_isSafeName_accepts_alphanumeric()
{
    TEST_ASSERT_TRUE(isSafeName("foo"));
    TEST_ASSERT_TRUE(isSafeName("Foo123"));
    TEST_ASSERT_TRUE(isSafeName("ABC_def-ghi"));
}

void test_isSafeName_rejects_null_and_empty()
{
    TEST_ASSERT_FALSE(isSafeName(nullptr));
    TEST_ASSERT_FALSE(isSafeName(""));
}

void test_isSafeName_rejects_path_traversal()
{
    TEST_ASSERT_FALSE(isSafeName("../etc"));
    TEST_ASSERT_FALSE(isSafeName("foo/bar"));
    TEST_ASSERT_FALSE(isSafeName(".."));
}

void test_isSafeName_rejects_special_chars()
{
    TEST_ASSERT_FALSE(isSafeName("foo bar"));
    TEST_ASSERT_FALSE(isSafeName("foo.json"));
    TEST_ASSERT_FALSE(isSafeName("foo@bar"));
    TEST_ASSERT_FALSE(isSafeName("foo;bar"));
}

void test_isSafeName_enforces_max_length_18()
{
    TEST_ASSERT_TRUE(isSafeName("abcdefghijklmnopqr"));
    TEST_ASSERT_FALSE(isSafeName("abcdefghijklmnopqrs"));
}

void test_isSafeId_accepts_lowercase_alnum()
{
    TEST_ASSERT_TRUE(isSafeId("abcd1234"));
    TEST_ASSERT_TRUE(isSafeId("1234567890"));
}

void test_isSafeId_rejects_invalid()
{
    TEST_ASSERT_FALSE(isSafeId("ABCDEFGH"));
    TEST_ASSERT_FALSE(isSafeId("abc"));
    TEST_ASSERT_FALSE(isSafeId("abcd-1234"));
    TEST_ASSERT_FALSE(isSafeId(nullptr));
}

void test_idFromUrl_alias()
{
    char out[24];

    TEST_ASSERT_TRUE(idFromUrl("/api/palettes/abcd1234", "/api/palettes/", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abcd1234", out);
}

// ---------------------------------------------------------------------------
// nameFromUrl
// ---------------------------------------------------------------------------

void test_nameFromUrl_extracts_simple_segment()
{
    char out[24];

    TEST_ASSERT_TRUE(nameFromUrl("/api/palettes/foo", "/api/palettes/", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("foo", out);
}

void test_nameFromUrl_extracts_segment_before_slash()
{
    char out[24];

    TEST_ASSERT_TRUE(nameFromUrl("/api/scenes/myname/play", "/api/scenes/", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("myname", out);
}

void test_nameFromUrl_rejects_prefix_mismatch()
{
    char out[24];

    TEST_ASSERT_FALSE(nameFromUrl("/api/scenes/foo", "/api/palettes/", out, sizeof(out)));
}

void test_nameFromUrl_rejects_empty_segment()
{
    char out[24];

    TEST_ASSERT_FALSE(nameFromUrl("/api/palettes/", "/api/palettes/", out, sizeof(out)));
    TEST_ASSERT_FALSE(nameFromUrl("/api/palettes//x", "/api/palettes/", out, sizeof(out)));
}

void test_nameFromUrl_rejects_overflow()
{
    char out[4]; // Room for 3 chars + null

    TEST_ASSERT_FALSE(nameFromUrl("/api/x/12345", "/api/x/", out, sizeof(out)));
    TEST_ASSERT_TRUE(nameFromUrl("/api/x/abc", "/api/x/", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
}

void test_nameFromUrl_rejects_null_inputs()
{
    char out[24];

    TEST_ASSERT_FALSE(nameFromUrl(nullptr, "/api/x/", out, sizeof(out)));
    TEST_ASSERT_FALSE(nameFromUrl("/api/x/foo", nullptr, out, sizeof(out)));
    TEST_ASSERT_FALSE(nameFromUrl("/api/x/foo", "/api/x/", nullptr, sizeof(out)));
    TEST_ASSERT_FALSE(nameFromUrl("/api/x/foo", "/api/x/", out, 0));
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

void setUp(void)
{
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_isSafeName_accepts_alphanumeric);
    RUN_TEST(test_isSafeName_rejects_null_and_empty);
    RUN_TEST(test_isSafeName_rejects_path_traversal);
    RUN_TEST(test_isSafeName_rejects_special_chars);
    RUN_TEST(test_isSafeName_enforces_max_length_18);
    RUN_TEST(test_isSafeId_accepts_lowercase_alnum);
    RUN_TEST(test_isSafeId_rejects_invalid);
    RUN_TEST(test_idFromUrl_alias);

    RUN_TEST(test_nameFromUrl_extracts_simple_segment);
    RUN_TEST(test_nameFromUrl_extracts_segment_before_slash);
    RUN_TEST(test_nameFromUrl_rejects_prefix_mismatch);
    RUN_TEST(test_nameFromUrl_rejects_empty_segment);
    RUN_TEST(test_nameFromUrl_rejects_overflow);
    RUN_TEST(test_nameFromUrl_rejects_null_inputs);

    return UNITY_END();
}
