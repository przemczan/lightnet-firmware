// Amalgamation TU for the host scene-engine test.
//
// The native env doesn't auto-compile library .cpp, so (as with test_panel_anim including
// AnimationPlayer.cpp) we pull the shared scene-engine translation units in here. After the
// IPacketSink / IPaletteResolver / ITopologyProvider decoupling these are all Arduino-free,
// which is exactly what this test proves: the controller scene engine runs on the host.
#include "Core/Controller/ScenePlayer.cpp"
#include "Core/Controller/SceneParser.cpp"
#include "Core/Controller/AnimationScheduler.cpp"
#include "Core/Controller/CompiledSweep.cpp"
#include "Core/Common/ProtocolMeta.cpp"
#include "Utils/Crc.cpp"
