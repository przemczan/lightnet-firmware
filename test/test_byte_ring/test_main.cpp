// Native unit tests for lib/Lightnet/Core/Common/ByteRing.hpp
// Run with: pio test -e native -f test_byte_ring

#include <unity.h>
#include "Core/Common/ByteRing.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

void test_empty_pop_returns_false()
{
    ByteRing<8> ring;
    uint8_t out;

    TEST_ASSERT_TRUE(ring.empty());
    TEST_ASSERT_FALSE(ring.pop(out));
}

void test_push_pop_roundtrip()
{
    ByteRing<8> ring;
    uint8_t out;

    TEST_ASSERT_TRUE(ring.push(0x42));
    TEST_ASSERT_FALSE(ring.empty());
    TEST_ASSERT_TRUE(ring.pop(out));
    TEST_ASSERT_EQUAL_UINT8(0x42, out);
    TEST_ASSERT_TRUE(ring.empty());
}

void test_fifo_order()
{
    ByteRing<8> ring;
    uint8_t out;

    TEST_ASSERT_TRUE(ring.push(1));
    TEST_ASSERT_TRUE(ring.push(2));
    TEST_ASSERT_TRUE(ring.push(3));

    TEST_ASSERT_TRUE(ring.pop(out));
    TEST_ASSERT_EQUAL_UINT8(1, out);
    TEST_ASSERT_TRUE(ring.pop(out));
    TEST_ASSERT_EQUAL_UINT8(2, out);
    TEST_ASSERT_TRUE(ring.pop(out));
    TEST_ASSERT_EQUAL_UINT8(3, out);
    TEST_ASSERT_FALSE(ring.pop(out));
}

void test_full_rejects_extra_push()
{
    // Capacity N holds N-1 usable bytes (one slot always kept empty to disambiguate full/empty).
    ByteRing<4> ring;

    TEST_ASSERT_TRUE(ring.push(1));
    TEST_ASSERT_TRUE(ring.push(2));
    TEST_ASSERT_TRUE(ring.push(3));
    TEST_ASSERT_FALSE_MESSAGE(ring.push(4), "ring of capacity 4 holds only 3 usable bytes");
}

void test_wraparound_integrity()
{
    ByteRing<4> ring;
    uint8_t out;

    // Push/pop repeatedly past the physical end of the buffer, checking order survives the wrap.
    for (int cycle = 0; cycle < 5; cycle++) {
        TEST_ASSERT_TRUE(ring.push((uint8_t)(cycle * 2)));
        TEST_ASSERT_TRUE(ring.push((uint8_t)(cycle * 2 + 1)));

        TEST_ASSERT_TRUE(ring.pop(out));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(cycle * 2), out);
        TEST_ASSERT_TRUE(ring.pop(out));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(cycle * 2 + 1), out);
    }

    TEST_ASSERT_TRUE(ring.empty());
}

void test_reset_clears_ring()
{
    ByteRing<8> ring;
    uint8_t out;

    ring.push(1);
    ring.push(2);
    ring.reset();

    TEST_ASSERT_TRUE(ring.empty());
    TEST_ASSERT_FALSE(ring.pop(out));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_empty_pop_returns_false);
    RUN_TEST(test_push_pop_roundtrip);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_full_rejects_extra_push);
    RUN_TEST(test_wraparound_integrity);
    RUN_TEST(test_reset_clears_ring);

    return UNITY_END();
}
