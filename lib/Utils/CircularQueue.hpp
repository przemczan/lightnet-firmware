#pragma once

#include <Arduino.h>

#define SIZE_BYTES sizeof(uint16_t)

class CircularQueue
{
    private:
        typedef struct {
            uint16_t length;
            uint8_t data[];
        } item;

        uint8_t *head;
        uint8_t *tail;
        uint16_t bufferSize;
        uint8_t *writePointer;
        uint8_t *readPointer;
        uint16_t itemsCount = 0;

        void writeData(uint8_t *data, uint16_t size);
        uint8_t *readData(uint16_t &size);

    public:
        CircularQueue(uint16_t bufferSize);
        ~CircularQueue();
        bool enqueue(uint8_t *data, uint16_t size);
        uint8_t* dequeue(uint16_t &size);
};

CircularQueue::CircularQueue(uint16_t bufferSize)
{
    this->head = malloc(this->bufferSize = bufferSize);
    this->tail = this->bufferSize + bufferSize;
}

CircularQueue::~CircularQueue()
{
    free(this->head);
}

bool CircularQueue::enqueue(uint8_t *data, uint16_t size)
{
    uint16_t dataSize = size + SIZE_BYTES;

    if (this->writePointer == this->readPointer) {
        if (this->readPointer != this->head) {
            // write pointer reached read pointer, buffer is full
            return false;
        }
    } else if (this->writePointer > this->readPointer) {
        if (this->tail - this->writePointer < dataSize
            || this->readPointer - this->head < dataSize
        ) {
            // no space till the end and from the beginning of buffer
            return false;
        }
        if (this->tail - this->writePointer < dataSize) {
            this->writePointer = this->head;
        }
    } else {
        if (this->readPointer - this->writePointer < dataSize) {
            // no space before read buffer
            return false;
        }
    }

    this->itemsCount++;
    this->writeData(data, size);

    return true;
}

uint8_t *CircularQueue::dequeue(uint16_t &size)
{
    if (!this->itemsCount) {
        size = 0;

        return NULL;
    }

    this->itemsCount--;

    return readData(size);
}

void CircularQueue::writeData(uint8_t *data, uint16_t size)
{
    *(uint16_t *)this->writePointer = size;
    this->writePointer += SIZE_BYTES;
    memcpy(this->writePointer, data, size);
    this->writePointer += SIZE_BYTES;
}

uint8_t *CircularQueue::readData(uint16_t &size)
{
    if (this->readPointer > this->head + this->bufferSize - sizeof(uint16_t)) {
        this->readPointer = this->head;
    }

    size = *(uint16_t *)this->readPointer;

    if (!size) {
        this->readPointer = this->head;
    }

    size = *(uint16_t *)this->readPointer;
    this->readPointer += sizeof(uint16_t);

    uint8_t *dataPointer = this->readPointer;
    this->readPointer += size;

    return dataPointer;
}
