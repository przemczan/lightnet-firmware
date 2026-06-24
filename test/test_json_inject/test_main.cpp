#include <unity.h>
#include <string.h>
#include "Utils/JsonInject.hpp"

using namespace Lightnet;

void test_upsert_inserts_missing_id()
{
    const char *body = "{\"schemaVersion\":1,\"name\":\"x\",\"layers\":[]}";
    char out[256];
    int n = jsonUpsertStringField(body, strlen(body), "id", "abcd1234", out, sizeof(out));

    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":\"abcd1234\""));
}

void test_upsert_replaces_existing_id()
{
    const char *body = "{\"id\":\"oldid12\",\"name\":\"x\"}";
    char out[256];
    int n = jsonUpsertStringField(body, strlen(body), "id", "newid123", out, sizeof(out));

    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":\"newid123\""));
    TEST_ASSERT_NULL(strstr(out, "oldid12"));
}

void test_read_top_level_string()
{
    const char *body = "{\"id\":\"abcd1234\",\"name\":\"Scene\"}";
    char id[16] = { 0 };

    TEST_ASSERT_TRUE(jsonReadTopLevelString(body, strlen(body), "id", id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("abcd1234", id);
}

void test_upsert_escapes_quote_in_value()
{
    const char *body = "{\"schemaVersion\":1,\"name\":\"x\",\"layers\":[]}";
    char out[256];
    int n = jsonUpsertStringField(body, strlen(body), "name", "test\"", out, sizeof(out));

    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"test\\\"\""));

    char name[16] = { 0 };

    TEST_ASSERT_TRUE(jsonReadTopLevelString(out, (size_t)n, "name", name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("test\"", name);
}

void setUp(void)
{
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_upsert_inserts_missing_id);
    RUN_TEST(test_upsert_replaces_existing_id);
    RUN_TEST(test_read_top_level_string);
    RUN_TEST(test_upsert_escapes_quote_in_value);

    return UNITY_END();
}
