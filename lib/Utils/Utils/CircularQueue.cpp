#include "CircularQueue.hpp"

CircularQueue::CircularQueue(uint16_t bufferSize)
{
    this->bufferSize = bufferSize;
    this->head = (uint8_t *)malloc(bufferSize);
    this->reset();
}

CircularQueue::~CircularQueue()
{
    free(this->head);
}

bool CircularQueue::enqueue(void *data, uint16_t size)
{
    uint16_t dataSize = size + SIZE_BYTES;

    if (this->writePointer == this->readPointer && this->itemsCount) {
        // write pointer reached read pointer, buffer is full
        return false;
    }

    if (this->writePointer >= this->readPointer) {
        if ((unsigned long)this->tail - (unsigned long)this->writePointer < dataSize
            && (unsigned long)this->readPointer - (unsigned long)this->head < dataSize
        ) {
            // no space till the end and from the beginning of buffer
            return false;
        }
        if ((unsigned long)this->tail - (unsigned long)this->writePointer < dataSize) {
            this->softTail = this->writePointer;
            this->writePointer = this->head;
        }
    } else {
        if ((unsigned long)this->readPointer - (unsigned long)this->writePointer < dataSize) {
            // no space before read buffer
            return false;
        }
    }

    this->itemsCount++;
    this->writeData(data, size);

    return true;
}

bool CircularQueue::dequeue(void *&data, uint16_t &size)
{
    if (!this->itemsCount) {
        return false;
    }

    this->itemsCount--;
    this->readData(data, size);

    return true;
}

void CircularQueue::writeData(void *data, uint16_t size)
{
    *(uint16_t *)this->writePointer = size;
    this->writePointer += SIZE_BYTES;
    memcpyToVolatile(this->writePointer, (uint8_t *)data, size);
    this->writePointer += size;
}

void CircularQueue::readData(void *&data, uint16_t &size)
{
    if (this->readPointer == this->softTail) {
        this->readPointer = this->head;
        this->softTail = this->tail;
    }

    size = *(uint16_t *)this->readPointer;
    this->readPointer += SIZE_BYTES;
    data = (void *)this->readPointer;
    this->readPointer += size;
}

void CircularQueue::reset()
{
    this->tail = this->head + bufferSize;
    this->softTail = this->tail;
    this->readPointer = this->head;
    this->writePointer = this->head;
    this->itemsCount = 0;
}

uint16_t CircularQueue::size()
{
    return this->itemsCount;
}
