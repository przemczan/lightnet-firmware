#pragma once

#include <Arduino.h>
#include "Macros.hpp"
#include <Mem.hpp>

#define SIZE_BYTES sizeof(uint16_t)

class CircularQueue
{
    private:
        uint8_t *head;
        volatile uint8_t *tail;
        volatile uint8_t *softTail;
        volatile uint8_t *writePointer;
        volatile uint8_t *readPointer;
        volatile uint16_t itemsCount = 0;
        volatile uint16_t bufferSize;

        void writeData(void *data, uint16_t size);
        void readData(void *&data, uint16_t &size);

    public:
        CircularQueue(uint16_t bufferSize);
        ~CircularQueue();
        bool enqueue(void *data, uint16_t size);
        bool dequeue(void *&data, uint16_t &size);
        uint16_t size();
        void reset();
};
