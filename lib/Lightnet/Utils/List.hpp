#pragma once

// Append-only dynamic array holding the discovered Panel/Edge tree (PanelsInitializer,
// PanelsTopologyProvider, the HTTP/WS panel endpoints, demos). Pure container — no Arduino
// dependency, so it compiles host-side too. Needs only the C stdlib.
#include <stdint.h>
#include <stdlib.h>

template<typename T>
class List
{
    public:
        ~List()
        {
            this->clear();
        }

        // Appends by value. On allocation failure the item is dropped and the list keeps its
        // previous contents — with exceptions disabled there is nothing better to do than not
        // crash; callers treat the list's size as the source of truth.
        void push(T data)
        {
            if (this->size == this->capacity && !this->grow()) {
                return;
            }

            this->items[this->size] = data;
            this->size++;
        }

        // Pre-allocates storage for at least `n` items in a single step, so a burst of push()
        // calls doesn't reallocate repeatedly. No-op if capacity already suffices or the
        // allocation fails.
        void reserve(uint16_t n)
        {
            if (n > this->capacity) {
                this->reallocateTo(n);
            }
        }

        // Returns a value-initialized T (nullptr for pointer element types) when out of range.
        T get(uint16_t index)
        {
            if (index < this->size) {
                return this->items[index];
            }

            return T();
        }

        void clear()
        {
            free(this->items);
            this->items    = nullptr;
            this->size     = 0;
            this->capacity = 0;
        }

        uint16_t getSize() const
        {
            return this->size;
        }

    private:
        bool grow()
        {
            uint16_t newCapacity = (this->capacity == 0)
                ? 4
                : (uint16_t)(this->capacity * 2);

            if (newCapacity <= this->capacity) {
                return false;  // uint16_t capacity ceiling reached
            }

            return this->reallocateTo(newCapacity);
        }

        bool reallocateTo(uint16_t newCapacity)
        {
            T *newItems = (T *)realloc(this->items, (size_t)newCapacity * sizeof(T));

            if (newItems == nullptr) {
                return false;  // old block is still valid -- keep current contents
            }

            this->items    = newItems;
            this->capacity = newCapacity;

            return true;
        }

        T *items = nullptr;
        uint16_t size = 0;
        uint16_t capacity = 0;
};
