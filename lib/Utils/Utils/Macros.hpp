#pragma once

#include "Debug.hpp"

#define SET_BIT(x, y)                       ((x) |= (1 << (y)))
#define CLEAR_BIT(x, y)                     ((x) &= ~(1 << (y)))
#define BIT_IS_SET(x, y)                    ((x) & (1 << (y)))
#define BIT_IS_CLEAR(x, y)                  (!BIT_IS_SET(x, y))
#define READ_BIT(val, bit)                 (BIT_IS_SET(val, bit) >> bit)
