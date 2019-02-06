#include "Config.hpp"
#include <Arduino.h>
#include "LightnetPanel.hpp"

void setup()
{
    #if DEBUG
    Serial.begin(115200);
    #endif

    PRINTLN("");

    LNPanel.addEdge(8);
    LNPanel.addEdge(9);
    //LNPanel.addEdge(10);

    LNPanel.configure({
        .redPinNo = 3,
        .greenPinNo = 3,
        .bluePinNo = 3,
        .interruptPinNo = 2
    });

    PRINTLN("===> [PANEL]");
}

void loop()
{
    LNPanel.run();
}

/**

#include <Arduino.h>

volatile bool led = true;
volatile uint8_t pinNo = 8;
volatile bool busState = true;
volatile bool hasPing = false;
volatile bool busIsDisabled = false;

void onInterrupt()
{
    if (busIsDisabled) {
        return;
    }

    uint8_t state = digitalRead(pinNo);

    if (!busState && state) {
        hasPing = true;
    }

    busState = state;

    digitalWrite(13, led);
    led = led ? false : true;
}

void ping()
{
    delay(10);

    busIsDisabled = true;

    pinMode(pinNo, OUTPUT);
    digitalWrite(pinNo, LOW);

    delay(10);

    pinMode(pinNo, INPUT_PULLUP);

    busIsDisabled = false;
}

bool getAndResetPingStatus()
{
    noInterrupts();
    bool state = hasPing;
    hasPing = false;
    interrupts();

    return state;
}

void setup()
{
    pinMode(13, OUTPUT);
    pinMode(2, INPUT);
    attachInterrupt(digitalPinToInterrupt(2), onInterrupt, CHANGE);

    ping();
}

void loop()
{
    if (getAndResetPingStatus()) {
        ping();
    }
}
*/
