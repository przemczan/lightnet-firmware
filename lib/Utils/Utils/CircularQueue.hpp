#pragma once

#include <Arduino.h>
#include "Macros.hpp"
#include <Mem.hpp>

#define SIZE_BYTES sizeof(size_t)

class CircularQueue
{
    private:
        uint8_t *head;
        uint8_t *tail;
        uint8_t *softTail;
        uint8_t *writePointer;
        uint8_t *readPointer;
        size_t itemsCount = 0;
        size_t bufferSize;

        void writeData(void *data, size_t size);
        void readData(void *&data, size_t &size);

    public:
        CircularQueue(size_t bufferSize);
        ~CircularQueue();
        bool enqueue(void *data, size_t size);
        bool dequeue(void *&data, size_t &size);
        size_t size();
        void reset();
        void dumpMeta();
};
