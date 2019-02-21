#pragma once

#include <Arduino.h>
#include "Macros.hpp"
#include "Mem.hpp"

#define SIZE_BYTES sizeof(uint16_t)

class CircularQueue
{
    private:
        uint8_t *head;
        uint8_t *tail;
        uint8_t *softTail;
        uint8_t *writePointer;
        uint8_t *readPointer;
        uint16_t itemsCount = 0;
        uint16_t bufferSize;

        void writeData(void *data, uint16_t size);
        void readData(void *&data, uint16_t &size);

    public:
        CircularQueue(uint16_t bufferSize);
        ~CircularQueue();
        bool enqueue(void *data, uint16_t size);
        bool dequeue(void *&data, uint16_t &size);
        uint16_t size();
        bool empty();
        void reset();
        void dumpMeta();
};
