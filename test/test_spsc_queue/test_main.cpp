// Native unit tests for lib/Lightnet/Core/Util/SpscByteQueue.hpp
// Run with: pio test -e native -f test_spsc_queue

#include <unity.h>
#include <deque>
#include <vector>
#include <cstdlib>
#include <cstring>
#include "Core/Util/SpscByteQueue.hpp"

using namespace Lightnet;

// ---- basics ---------------------------------------------------------------

void test_empty_pop_returns_zero()
{
    SpscByteQueue<32> q;
    uint8_t out[32];

    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_UINT16(0, q.pop(out, sizeof(out)));
}

void test_push_pop_roundtrip()
{
    SpscByteQueue<32> q;
    const uint8_t in[] = { 1, 2, 3, 4, 5 };
    uint8_t out[32] = { 0 };

    TEST_ASSERT_TRUE(q.push(in, sizeof(in)));
    TEST_ASSERT_FALSE(q.empty());

    uint16_t n = q.pop(out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT16(sizeof(in), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, sizeof(in));
    TEST_ASSERT_TRUE(q.empty());
}

void test_fifo_order()
{
    SpscByteQueue<64> q;
    const uint8_t a[] = { 10, 11 };
    const uint8_t b[] = { 20, 21, 22 };
    const uint8_t c[] = { 30 };
    uint8_t out[64];

    TEST_ASSERT_TRUE(q.push(a, sizeof(a)));
    TEST_ASSERT_TRUE(q.push(b, sizeof(b)));
    TEST_ASSERT_TRUE(q.push(c, sizeof(c)));

    TEST_ASSERT_EQUAL_UINT16(sizeof(a), q.pop(out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, out, sizeof(a));
    TEST_ASSERT_EQUAL_UINT16(sizeof(b), q.pop(out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, out, sizeof(b));
    TEST_ASSERT_EQUAL_UINT16(sizeof(c), q.pop(out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(c, out, sizeof(c));
    TEST_ASSERT_TRUE(q.empty());
}

void test_zero_length_rejected()
{
    SpscByteQueue<32> q;

    TEST_ASSERT_FALSE(q.push("", 0));
    TEST_ASSERT_TRUE(q.empty());
}

// One byte is reserved, so usable capacity is CapacityBytes-1, of which 2 bytes
// are the length prefix. Cap 16 → a 13-byte record fits, 14 does not.
void test_full_rejects_largest_plus_one()
{
    SpscByteQueue<16> q;
    uint8_t big[16];

    for (uint8_t i = 0; i < sizeof(big); i++) big[i] = i;

    TEST_ASSERT_FALSE(q.push(big, 14));         // 14 + 2 = 16 > 15 free
    TEST_ASSERT_TRUE(q.push(big, 13));          // 13 + 2 = 15 == free
    TEST_ASSERT_FALSE(q.push(big, 1));          // ring now full
}

// A record whose PAYLOAD physically straddles the wrap boundary must reassemble correctly.
void test_wraparound_integrity()
{
    SpscByteQueue<16> q;
    uint8_t out[16];

    // Push then drain a 10-byte record so the write offset advances to 12 (r=w=12).
    const uint8_t pad[]  = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9 }; // 10 + 2 = 12 B
    const uint8_t span[] = { 1, 2, 3, 4, 5, 6 };                                            // 6 + 2 = 8 B

    TEST_ASSERT_TRUE(q.push(pad, sizeof(pad)));                     // occupies [0..11], w=12
    TEST_ASSERT_EQUAL_UINT16(sizeof(pad), q.pop(out, sizeof(out))); // r=12, w=12

    // header at [12,13]; payload at [14,15,0,1,2,3] — genuinely straddles index 15→0.
    TEST_ASSERT_TRUE(q.push(span, sizeof(span)));
    TEST_ASSERT_EQUAL_UINT16(sizeof(span), q.pop(out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(span, out, sizeof(span));
    TEST_ASSERT_TRUE(q.empty());
}

// Realistic panel size: a 70-byte record (PacketSetPalette) must always fit an 80-byte ring.
void test_panel_sized_max_record()
{
    SpscByteQueue<80> q;
    uint8_t in[70], out[80];

    for (uint16_t i = 0; i < sizeof(in); i++) in[i] = (uint8_t)(i * 7 + 1);

    // Repeat across many wrap positions to be sure it never fragments-fails.
    for (int iter = 0; iter < 200; iter++) {
        TEST_ASSERT_TRUE(q.push(in, sizeof(in)));
        TEST_ASSERT_EQUAL_UINT16(sizeof(in), q.pop(out, sizeof(out)));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, sizeof(in));
    }
}

// ---- randomized fuzz against a reference model ----------------------------

void test_fuzz_against_reference()
{
    SpscByteQueue<48> q;
    std::deque<std::vector<uint8_t> > ref;
    uint8_t out[48];

    srand(12345);

    for (int step = 0; step < 200000; step++) {
        // Bias toward pushing so the ring fills and wraps repeatedly.
        bool doPush = (rand() % 3) != 0;

        if (doPush) {
            uint16_t len = (uint16_t)(1 + rand() % 20);
            std::vector<uint8_t> rec(len);

            for (uint16_t i = 0; i < len; i++) rec[i] = (uint8_t)rand();

            if (q.push(rec.data(), len)) {
                ref.push_back(rec);
            }

            // push failing (full) is fine — model just doesn't record it.
        } else {
            uint16_t n = q.pop(out, sizeof(out));

            if (n == 0) {
                TEST_ASSERT_TRUE(ref.empty());
            } else {
                TEST_ASSERT_FALSE(ref.empty());

                const std::vector<uint8_t>& exp = ref.front();

                TEST_ASSERT_EQUAL_UINT16(exp.size(), n);
                TEST_ASSERT_EQUAL_UINT8_ARRAY(exp.data(), out, n);
                ref.pop_front();
            }
        }
    }

    // Drain the rest and confirm exact correspondence.
    while (true) {
        uint16_t n = q.pop(out, sizeof(out));

        if (n == 0) break;

        TEST_ASSERT_FALSE(ref.empty());

        const std::vector<uint8_t>& exp = ref.front();

        TEST_ASSERT_EQUAL_UINT16(exp.size(), n);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(exp.data(), out, n);
        ref.pop_front();
    }

    TEST_ASSERT_TRUE(ref.empty());
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

    RUN_TEST(test_empty_pop_returns_zero);
    RUN_TEST(test_push_pop_roundtrip);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_zero_length_rejected);
    RUN_TEST(test_full_rejects_largest_plus_one);
    RUN_TEST(test_wraparound_integrity);
    RUN_TEST(test_panel_sized_max_record);
    RUN_TEST(test_fuzz_against_reference);

    return UNITY_END();
}
