// Native unit tests for lib/Lightnet/Utils/MainLoopQueue.hpp
// Run with: pio test -e native -f test_main_loop_queue
//
// The critical-section lock compiles to a no-op on native, so these tests cover
// the encode/decode/FIFO-order/full-rejection logic single-threaded. Cross-task
// safety (ESP32 barrier) is a device property and is exercised on hardware.

#include <unity.h>
#include <string.h>
#include <vector>
#include "Utils/MainLoopQueue.hpp"

using namespace Lightnet;

// ---------------------------------------------------------------------------
// Test fixtures: tasks record what they were invoked with into globals so the
// test body can assert ordering and argument round-tripping.
// ---------------------------------------------------------------------------

struct Invocation {
    int      tag;
    uint16_t len;
    uint8_t  bytes[MainLoopQueue::MAX_ARGS];
};

static std::vector<Invocation> g_calls;

struct PodArgs {
    uint32_t a;
    uint16_t b;
    uint8_t  c;
};

static void taskRecordRaw(const uint8_t *args, uint16_t len)
{
    Invocation inv{};
    inv.tag = 1;
    inv.len = len;
    if (len > sizeof(inv.bytes)) len = sizeof(inv.bytes);
    memcpy(inv.bytes, args, len);
    g_calls.push_back(inv);
}

static void taskRecordPod(const uint8_t *args, uint16_t len)
{
    Invocation inv{};
    inv.tag = 2;
    inv.len = len;
    memcpy(inv.bytes, args, len);
    g_calls.push_back(inv);
}

void setUp(void)
{
    g_calls.clear();
}

void tearDown(void) {}

// ---------------------------------------------------------------------------
// Basics
// ---------------------------------------------------------------------------

void test_empty_drain_invokes_nothing()
{
    MainLoopQueue q;
    q.drain();
    TEST_ASSERT_EQUAL_UINT(0, g_calls.size());
}

void test_post_then_drain_invokes_with_args()
{
    MainLoopQueue q;
    const uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };

    TEST_ASSERT_TRUE(q.post(&taskRecordRaw, payload, sizeof(payload)));
    q.drain();

    TEST_ASSERT_EQUAL_UINT(1, g_calls.size());
    TEST_ASSERT_EQUAL_INT(1, g_calls[0].tag);
    TEST_ASSERT_EQUAL_UINT(4, g_calls[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, g_calls[0].bytes, 4);
}

void test_pod_args_round_trip()
{
    MainLoopQueue q;
    PodArgs in{ 0x01020304u, 0xAABB, 0x5A };

    TEST_ASSERT_TRUE(q.post(&taskRecordPod, &in, sizeof(in)));
    q.drain();

    TEST_ASSERT_EQUAL_UINT(1, g_calls.size());
    TEST_ASSERT_EQUAL_UINT(sizeof(PodArgs), g_calls[0].len);

    PodArgs out{};
    memcpy(&out, g_calls[0].bytes, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(in.a, out.a);
    TEST_ASSERT_EQUAL_UINT16(in.b, out.b);
    TEST_ASSERT_EQUAL_UINT8(in.c, out.c);
}

void test_zero_length_args_allowed()
{
    MainLoopQueue q;

    TEST_ASSERT_TRUE(q.post(&taskRecordRaw, nullptr, 0));
    q.drain();

    TEST_ASSERT_EQUAL_UINT(1, g_calls.size());
    TEST_ASSERT_EQUAL_UINT(0, g_calls[0].len);
}

// ---------------------------------------------------------------------------
// FIFO ordering and multiple tasks
// ---------------------------------------------------------------------------

void test_fifo_order_preserved()
{
    MainLoopQueue q;

    for (uint8_t i = 0; i < 5; i++) {
        uint8_t v = (uint8_t)(0x10 + i);
        TEST_ASSERT_TRUE(q.post(&taskRecordRaw, &v, 1));
    }
    q.drain();

    TEST_ASSERT_EQUAL_UINT(5, g_calls.size());
    for (uint8_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x10 + i), g_calls[i].bytes[0]);
    }
}

void test_drain_is_idempotent_when_empty_after_use()
{
    MainLoopQueue q;
    uint8_t v = 0x42;

    q.post(&taskRecordRaw, &v, 1);
    q.drain();
    TEST_ASSERT_EQUAL_UINT(1, g_calls.size());

    q.drain(); // second drain — nothing left
    TEST_ASSERT_EQUAL_UINT(1, g_calls.size());
}

void test_interleaved_post_and_drain()
{
    MainLoopQueue q;
    uint8_t a = 1, b = 2;

    q.post(&taskRecordRaw, &a, 1);
    q.drain();
    q.post(&taskRecordRaw, &b, 1);
    q.drain();

    TEST_ASSERT_EQUAL_UINT(2, g_calls.size());
    TEST_ASSERT_EQUAL_UINT8(1, g_calls[0].bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(2, g_calls[1].bytes[0]);
}

// ---------------------------------------------------------------------------
// Rejection paths
// ---------------------------------------------------------------------------

void test_null_fn_rejected()
{
    MainLoopQueue q;
    uint8_t v = 0;
    TEST_ASSERT_FALSE(q.post(nullptr, &v, 1));
}

void test_oversized_args_rejected()
{
    MainLoopQueue q;
    uint8_t big[MainLoopQueue::MAX_ARGS + 1] = {};
    TEST_ASSERT_FALSE(q.post(&taskRecordRaw, big, sizeof(big)));
    q.drain();
    TEST_ASSERT_EQUAL_UINT(0, g_calls.size());
}

void test_full_queue_rejects_then_recovers()
{
    MainLoopQueue q;
    // Each record: HEADER(2) + sizeof(fn) + MAX_ARGS. Post until rejection.
    uint8_t args[MainLoopQueue::MAX_ARGS] = {};
    int posted = 0;

    while (q.post(&taskRecordRaw, args, sizeof(args))) {
        posted++;
        if (posted > 10000) break; // safety; should reject well before this
    }

    TEST_ASSERT_GREATER_THAN_INT(0, posted);   // at least some fit
    TEST_ASSERT_LESS_THAN_INT(10000, posted);  // and it did reject (not infinite)

    // Draining frees the ring; we can post again afterwards.
    q.drain();
    TEST_ASSERT_EQUAL_UINT((size_t)posted, g_calls.size());
    TEST_ASSERT_TRUE(q.post(&taskRecordRaw, args, sizeof(args)));
}

// ---------------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_drain_invokes_nothing);
    RUN_TEST(test_post_then_drain_invokes_with_args);
    RUN_TEST(test_pod_args_round_trip);
    RUN_TEST(test_zero_length_args_allowed);
    RUN_TEST(test_fifo_order_preserved);
    RUN_TEST(test_drain_is_idempotent_when_empty_after_use);
    RUN_TEST(test_interleaved_post_and_drain);
    RUN_TEST(test_null_fn_rejected);
    RUN_TEST(test_oversized_args_rejected);
    RUN_TEST(test_full_queue_rejects_then_recovers);
    return UNITY_END();
}
