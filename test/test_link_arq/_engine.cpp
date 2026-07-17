// Amalgamation TU — the native env doesn't auto-compile library .cpp, so pull the pure
// link-ARQ logic in here directly (same pattern as test_packet_framer/_engine.cpp).
#include "Core/Relay/LinkArq.cpp"
#include "Core/Common/ProtocolMeta.cpp"
#include "Utils/Crc.cpp"
