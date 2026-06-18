#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    class IContentReader
    {
        public:
            virtual ~IContentReader()
            {
            }

            // Bytes read; 0 = EOF; <0 = error.
            virtual int read(uint8_t *buf, size_t cap) = 0;
    };
}  // namespace Lightnet
