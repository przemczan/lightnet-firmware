// Amalgamation TU for the host scene-engine test.
//
// The native env doesn't auto-compile library .cpp, so (as with test_panel_anim including
// AnimationPlayer.cpp) we pull the shared scene-engine translation units in here. After the
// IPacketSink / IPaletteResolver / ITopologyProvider decoupling these are all Arduino-free,
// which is exactly what this test proves: the controller scene engine runs on the host.
#include "Core/Controller/Scene/ScenePlayer.cpp"
#include "Core/Controller/Scene/SceneParser.cpp"
#include "Core/Controller/Scene/AnimationScheduler.cpp"
#include "Core/Controller/Scene/AnimationRunner.cpp"
#include "Core/Common/ProtocolMeta.cpp"
#include "Utils/Crc.cpp"
