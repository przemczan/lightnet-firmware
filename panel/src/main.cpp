#include <Arduino.h>
#include <Macros.hpp>
#include "Panel.hpp"
#include "Bus.hpp"
#include "Config.hpp"

#define STATE_BOOT 0
#define STATE_READY 1

volatile uint8_t state = STATE_BOOT;

Bus bus;
Panel panel(&bus);

void updateBordersStates()
{
    panel.updateBordersStates();
}

void setup()
{
    Serial.begin(9600);

    panel.addBorder(&PORTB, 0);
    panel.addBorder(&PORTB, 1);
    panel.addBorder(&PORTB, 2);

    Serial.println("Panel SETUP");

    attachInterrupt(0, updateBordersStates, HIGH);
    sei();
}

void loop()
{
    switch (state)
    {
        case STATE_BOOT:
            panel.boot();

            if (panel.isReady()) {
                state = STATE_READY;
                Serial.println("Panel READY");
            }
        break;

        case STATE_READY:

        break;
    }
}
