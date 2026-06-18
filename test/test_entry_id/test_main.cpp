#include <unity.h>
#include <string.h>
#include "Utils/EntryId.hpp"

using namespace Lightnet;

void test_deterministic_id_stable()
{
    char a[ENTRY_ID_MAX + 1];
    char b[ENTRY_ID_MAX + 1];

    deterministicId("builtin:rainbow", a, sizeof(a));
    deterministicId("builtin:rainbow", b, sizeof(b));

    TEST_ASSERT_EQUAL_STRING(a, b);
    TEST_ASSERT_TRUE(isValidId(a));
    TEST_ASSERT_EQUAL_UINT8(ENTRY_ID_LEN, strlen(a));
}

void test_user_colors_and_one_shot_ids()
{
    TEST_ASSERT_TRUE(isValidId(userColorsId()));
    TEST_ASSERT_TRUE(isValidId(oneShotId()));
    TEST_ASSERT_FALSE(strcmp(userColorsId(), oneShotId()) == 0);
}

void test_is_valid_id()
{
    TEST_ASSERT_TRUE(isValidId("abcd1234"));
    TEST_ASSERT_TRUE(isValidId("12345678"));
    TEST_ASSERT_TRUE(isValidId("1234567890"));
    TEST_ASSERT_FALSE(isValidId("abc"));
    TEST_ASSERT_FALSE(isValidId("ABCDEFGH"));
    TEST_ASSERT_FALSE(isValidId("abcd-1234"));
    TEST_ASSERT_FALSE(isValidId(nullptr));
}

void test_generate_random_id()
{
    char a[ENTRY_ID_MAX + 1];
    char b[ENTRY_ID_MAX + 1];

    TEST_ASSERT_TRUE(generateRandomId(a, sizeof(a), nullptr, nullptr));
    TEST_ASSERT_TRUE(generateRandomId(b, sizeof(b), nullptr, nullptr));
    TEST_ASSERT_TRUE(isValidId(a));
    TEST_ASSERT_TRUE(isValidId(b));
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

    RUN_TEST(test_deterministic_id_stable);
    RUN_TEST(test_user_colors_and_one_shot_ids);
    RUN_TEST(test_is_valid_id);
    RUN_TEST(test_generate_random_id);

    return UNITY_END();
}
