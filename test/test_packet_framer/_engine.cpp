// Amalgamation TU — the native env doesn't auto-compile library .cpp, so pull the pure
// packet-framing logic in here directly (same pattern as test_scene_player/_engine.cpp).
#include "Core/Relay/PacketFramer.cpp"
#include "Core/Common/ProtocolMeta.cpp"
#include "Utils/Crc.cpp"
