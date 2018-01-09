#include <Arduino.h>
#include <Wire.h>
#include "Protocol.hpp"
#include "List.hpp"
#include "Crc.hpp"
#include "PanelsInitializer.hpp"

#define STATE_BOOT 0
#define STATE_READY 1

uint8_t state = STATE_BOOT;

void setup() {
    Serial.begin(9600);
    Serial.println("CONTROLLER");

    LNPanelsInitializer.start();
}

void loop() {
    switch (state)
    {
        case STATE_BOOT:
        break;

        case STATE_READY:
        break;
    }
}
