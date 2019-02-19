#pragma once

#include <Arduino.h>
#include "Macros.hpp"
#include <Mem.hpp>

#define SIZE_BYTES sizeof(uint16_t)

class CircularQueue
{
    private:
        uint8_t * head;
        volatile uint8_t * volatile tail;
        volatile uint8_t * volatile softTail;
        volatile uint8_t * volatile writePointer;
        volatile uint8_t * volatile readPointer;
        volatile uint16_t itemsCount = 0;
        volatile uint16_t bufferSize;

        void writeData(void *data, uint16_t size) volatile;
        void readData(void *&data, uint16_t &size) volatile;

    public:
        CircularQueue(uint16_t bufferSize);
        ~CircularQueue();
        bool enqueue(void *data, uint16_t size) volatile;
        bool dequeue(void *&data, uint16_t &size) volatile;
        uint16_t size() volatile;
        void reset() volatile;
        void dumpMeta() volatile;
};
