#pragma once

#include <Arduino.h>
#include <stdlib.h>

template<typename T>
class List
{
    private:
        typedef struct {
            T data;
        } item;

        item *items = NULL;
        void (*clearCallback)(T);
        volatile uint16_t size = 0;

    public:
        List(void (*clearCallback)(T) = NULL);
        ~List();
        void push(T data);
        void remove(T data);
        void removeByIndex(uint16_t index);
        T get(uint16_t index);
        bool find(T data);
        bool find(T data, uint16_t *outIndex);
        bool filterOne(bool (*callback)(T), uint16_t *outIndex);
        void clear();
        uint16_t getSize();
};

template<typename T>
List<T>::List(void (*clearCallback)(T))
{
    this->clearCallback = clearCallback;
}

template<typename T>
List<T>::~List()
{
    this->clear();
}

template<typename T>
void List<T>::push(T data)
{
    this->items = (item *)realloc(this->items, (++this->size) * sizeof(item));
    this->items[this->size - 1].data = data;
}

template<typename T>
void List<T>::remove(T data)
{
    unsigned long index;
    if (this->find(data, index)) {
        this->removeByIndex(index);
    }
}

template<typename T>
void List<T>::removeByIndex(uint16_t index)
{
    if (index < this->size) {
        if (index < (this->size - 1)) {
            memmove(
                &this->items[index],
                &this->items[index + 1],
                (this->size - (index + 1)) * sizeof(item)
            );
        }
        this->items = (item *)realloc(this->items, --this->size * sizeof(item));
        if (!this->size) {
            this->items = 0;
        }
    }
}

template<typename T>
T List<T>::get(uint16_t index)
{
    if (index < this->size) {
        return this->items[index].data;
    }

    return NULL;
}

template<typename T>
bool List<T>::find(T data)
{
    uint16_t index = this->size;
    while (index--) {
        if (this->items[index].data == data) {
            return true;
        }
    }
    return false;
}

template<typename T>
bool List<T>::find(T data, uint16_t *outIndex)
{
    uint16_t index = this->size;
    while (index--) {
        if (this->items[index].data == data) {
            *outIndex = index;
            return true;
        }
    }
    return false;
}

template<typename T>
bool List<T>::filterOne(bool (*callback)(T), uint16_t *outIndex)
{
    uint16_t index = this->size;
    while (index--) {
        if (callback(this->items[index])) {
            *outIndex = index;
            return true;
        }
    }
    return false;
}

template<typename T>
void List<T>::clear()
{
    if (this->size) {
        while (this->size--) {
            if (this->clearCallback) {
                this->clearCallback(this->items[this->size].data);
            }
        }
        this->size = 0;
        free(this->items);
        this->items = NULL;
    }
}

template<typename T>
uint16_t List<T>::getSize()
{
    return this->size;
}
