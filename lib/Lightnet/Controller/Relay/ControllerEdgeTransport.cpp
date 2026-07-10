#ifdef LIGHTNET_TARGET_CONTROLLER
#include "ControllerEdgeTransport.hpp"
#include "../../Utils/Debug.hpp"

namespace {
    // Mirrors Panel/EdgeUartTransport.cpp's own PREAMBLE_BYTE_COUNT (see that file's comment) —
    // the controller's trunk edge feeds a panel's muxed RX exactly like any other edge does, so it
    // needs the same slack for the receiving panel's polled mux switch.
    const uint8_t PREAMBLE_BYTE_COUNT = 4;
}

ControllerEdgeTransport::ControllerEdgeTransport(HardwareSerial &serial)
    : serial(serial), outputEnablePin(0xFF)
{
}

void ControllerEdgeTransport::begin(uint8_t outputEnablePin)
{
    this->outputEnablePin = outputEnablePin;

    pinMode(this->outputEnablePin, OUTPUT);
    digitalWrite(this->outputEnablePin, HIGH);  // active-low OE# -- idle disabled/tri-stated
}

void ControllerEdgeTransport::sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size)
{
    (void)edgeIndex;  // the controller has exactly one edge

    // Enable U4 for the duration of this send only -- see the class comment on why leaving it
    // permanently enabled would contend with an incoming panel reply on this shared half-duplex
    // wire.
    digitalWrite(this->outputEnablePin, LOW);

    // Throwaway preamble bytes (0xFF — not a valid packetType_t, so the receiving panel's
    // PacketFramer skips each one as noise instead of desyncing on it): absorb the receiving
    // panel's mux-settling time plus its wake-to-claim reaction latency before the real frame
    // starts.
    uint8_t preamble[PREAMBLE_BYTE_COUNT];

    for (uint8_t i = 0; i < PREAMBLE_BYTE_COUNT; i++) {
        preamble[i] = 0xFF;
    }

    this->serial.write(preamble, PREAMBLE_BYTE_COUNT);
    this->serial.write((const uint8_t *)packet, size);

    // Block until the UART has actually finished shifting the last byte out -- disabling U4
    // before that would truncate it (mirrors Panel/EdgeUartTransport::sendOnEdge()'s own TXC0
    // wait for the same reason).
    this->serial.flush();

    digitalWrite(this->outputEnablePin, HIGH);  // back to tri-stated -- free the wire for a reply

    // Dumps exactly what was just handed to the UART. Deliberately after the OE# release: with
    // a USB CDC host attached these prints take milliseconds, and holding the wire driven
    // through them collides with the panel's immediate reply, destroying it.
    DEBUG_IF(DEBUG_DISCOVERY, {
        _debugPrintTimestamp();
        D_PRINT(DPF("[TRUNK TX] type"), ((const uint8_t *)packet)[0], DPF("bytes:"));

        const uint8_t *bytes = (const uint8_t *)packet;

        for (uint8_t i = 0; i < size; i++) {
            _debugPrintSpace();
            D_PRINT(bytes[i]);
        }

        _debugPrintNewline();
    });
}

bool ControllerEdgeTransport::available()
{
    return this->serial.available() > 0;
}

uint8_t ControllerEdgeTransport::readByte()
{
    return (uint8_t)this->serial.read();
}

ControllerEdgeTransport LNTrunkTransport(Serial1);

#endif  // LIGHTNET_TARGET_CONTROLLER
