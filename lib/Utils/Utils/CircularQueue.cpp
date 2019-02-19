#include "CircularQueue.hpp"

CircularQueue::CircularQueue(uint16_t bufferSize)
{
    this->bufferSize = bufferSize;
    this->head = (uint8_t *)malloc(bufferSize);
    memset(this->head, 0, bufferSize);
    this->reset();
}

CircularQueue::~CircularQueue()
{
    free(this->head);
}

bool CircularQueue::enqueue(void *data, uint16_t size) volatile
{
    uint16_t dataSize = size + SIZE_BYTES;

    if (this->writePointer == this->readPointer && this->itemsCount) {
        // write pointer reached read pointer, buffer is full
        return false;
    }

    if (this->writePointer >= this->readPointer) {
        if ((uintptr_t)this->tail - (uintptr_t)this->writePointer < dataSize
            && (uintptr_t)this->readPointer - (uintptr_t)this->head < dataSize
        ) {
            // no space till the end and from the beginning of buffer
            return false;
        }
        if ((uintptr_t)this->tail - (uintptr_t)this->writePointer < dataSize) {
            this->softTail = this->writePointer;
            this->writePointer = this->head;
        }
    } else {
        if ((uintptr_t)this->readPointer - (uintptr_t)this->writePointer < dataSize) {
            // no space before read buffer
            return false;
        }
    }

    this->itemsCount++;
    this->writeData(data, size);

    return true;
}

bool CircularQueue::dequeue(void *&data, uint16_t &size) volatile
{
    if (!this->itemsCount) {
        return false;
    }

    this->itemsCount--;
    this->readData(data, size);

    return true;
}

void CircularQueue::writeData(void *data, uint16_t size) volatile
{
    this->writePointer[0] = size;
    this->writePointer[1] = size >> 8;
    this->writePointer += SIZE_BYTES;
    memcpyToVolatile(this->writePointer, (uint8_t *)data, size);
    this->writePointer += size;
}

void CircularQueue::readData(void *&data, uint16_t &size) volatile
{
    if (this->readPointer == this->softTail) {
        this->readPointer = this->head;
        this->softTail = this->tail;
    }

    size = this->readPointer[0] + (this->readPointer[1] << 8);
    this->readPointer += SIZE_BYTES;
    data = (void *)this->readPointer;
    this->readPointer += size;
}

void CircularQueue::reset() volatile
{
    this->tail = this->head + bufferSize;
    this->softTail = this->tail;
    this->readPointer = this->head;
    this->writePointer = this->head;
    this->itemsCount = 0;
}

uint16_t CircularQueue::size() volatile
{
    return this->itemsCount;
}

void CircularQueue::dumpMeta() volatile
{
    PRINTF("\nWri: %u, Rea: %u, Tai: %u, Sof: %u, Cnt: %u\n",
        (uintptr_t)this->writePointer - (uintptr_t)this->head,
        (uintptr_t)this->readPointer - (uintptr_t)this->head,
        (uintptr_t)this->tail - (uintptr_t)this->head,
        (uintptr_t)this->softTail - (uintptr_t)this->head,
        this->itemsCount
    );
}
