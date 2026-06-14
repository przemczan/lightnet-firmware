#include "Protocol.hpp"

// setPacketMeta() and validatePacket() moved to the portable core
// (Core/Anim/ProtocolMeta.cpp) so the shared scene engine and mobile C ABI can stamp
// packets without Arduino/FastLED. This TU is intentionally otherwise empty.
