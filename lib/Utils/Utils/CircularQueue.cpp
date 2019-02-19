#include "CircularQueue.hpp"

CircularQueue::CircularQueue(size_t bufferSize)
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

bool CircularQueue::enqueue(void *data, size_t size)
{
    size_t dataSize = size + sizeof(size_t);

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

bool CircularQueue::dequeue(void *&data, size_t &size)
{
    if (!this->itemsCount) {
        return false;
    }

    this->itemsCount--;
    this->readData(data, size);

    return true;
}

void CircularQueue::writeData(void *data, size_t size)
{
    uint8_t sizeBytes = sizeof(size_t);

    while (sizeBytes--) {
        this->writePointer[sizeBytes] = size >> (8 * sizeBytes);
    }

    this->writePointer += sizeof(size_t);
    memcpy(this->writePointer, (uint8_t *)data, size);
    this->writePointer += size;
}

void CircularQueue::readData(void *&data, size_t &size)
{
    if (this->readPointer == this->softTail) {
        this->readPointer = this->head;
        this->softTail = this->tail;
    }

    size = 0;
    uint8_t sizeBytes = sizeof(size_t);

    while (sizeBytes--) {
        size += this->readPointer[sizeBytes] << (8 * sizeBytes);
    }

    this->readPointer += sizeof(size_t);
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

size_t CircularQueue::size()
{
    return this->itemsCount;
}

void CircularQueue::dumpMeta()
{
    PRINTF("\nWri: %u, Rea: %u, Tai: %u, Sof: %u, Cnt: %u\n",
           (uintptr_t)this->writePointer - (uintptr_t)this->head,
           (uintptr_t)this->readPointer - (uintptr_t)this->head,
           (uintptr_t)this->tail - (uintptr_t)this->head,
           (uintptr_t)this->softTail - (uintptr_t)this->head,
           this->itemsCount
    );
}
