#pragma once

#include "IContentReader.hpp"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace Lightnet {
    class BufferContentReader : public IContentReader
    {
        public:
            BufferContentReader(const char *data, size_t len) : _data(data), _len(len), _pos(0)
            {
            }

            int read(uint8_t *buf, size_t cap) override
            {
                if (!_data || _pos >= _len) return 0;

                size_t n = _len - _pos;

                if (n > cap) n = cap;

                memcpy(buf, _data + _pos, n);
                _pos += n;

                return (int)n;
            }

        private:
            const char *_data;
            size_t _len;
            size_t _pos;
    };
}  // namespace Lightnet
