#pragma once

// Codec traits contract for Database<Codec>.
//
// struct MyCodec {
//     typedef MyModel Model;
//     static constexpr uint8_t  MODEL_VERSION = 1;
//     static constexpr size_t RECORD_SIZE     = 64;
//
//     static uint8_t serialize(const MyModel& record, uint8_t *buffer, size_t capacity);
//     static uint8_t deserialize(const uint8_t *buffer, size_t length, MyModel& recordOut);
// };
//
// serialize/deserialize return 0 on success, non-zero codec-specific error otherwise.
// serialize must write exactly RECORD_SIZE bytes (zero-pad the tail if needed).

#include <stddef.h>
#include <stdint.h>
