// Amalgamation TU — the native env doesn't auto-compile library .cpp, so pull the pure
// discovery-driver logic in here directly (same pattern as test_scene_player/_engine.cpp).
#include "Core/Relay/PanelDiscoveryDriver.cpp"
#include "Core/Relay/PanelDiscovery.cpp"
#include "Core/Common/ProtocolMeta.cpp"
#include "Utils/Crc.cpp"
