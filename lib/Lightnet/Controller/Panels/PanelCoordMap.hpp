#pragma once

#include <stdint.h>

namespace Lightnet {

// Coordinate for a panel (fixed-point: 1/100 unit)
struct PanelCoord {
    uint8_t  panelIndex;  // panel I2C address
    int16_t  x;           // fixed-point, 1/100 unit  (-327 to +327 units)
    int16_t  y;           // fixed-point, 1/100 unit
};

class PanelCoordMap {
public:
    PanelCoordMap(uint8_t maxEntries = 30);
    ~PanelCoordMap();

    // Register or update a panel's coordinates
    void setCoord(uint8_t panelIndex, int16_t x, int16_t y);

    // Query coordinates
    bool getCoord(uint8_t panelIndex, int16_t& x, int16_t& y) const;

    // Get all coordinates
    const PanelCoord* getAll() const { return coords; }
    uint8_t getCount() const { return count; }

private:
    PanelCoord* coords;
    uint8_t     count;
    uint8_t     maxEntries;

    int findIndex(uint8_t panelIndex) const;
};

}  // namespace Lightnet
