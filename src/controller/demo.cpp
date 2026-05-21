#ifdef LIGHTNET_TARGET_CONTROLLER
#if DEMO_ENABLED

#include "demo.hpp"

static Lightnet::DemoRunner* demoRunner = nullptr;

void initDemos(Lightnet::AnimationService&   animService,
               Lightnet::SceneStore&         sceneStore,
               Lightnet::ScenePlayer&        scenePlayer,
               Lightnet::AnimationScheduler& scheduler,
               PanelsController&             panels,
               PanelsInitializer&            initializer)
{
    demoRunner = new Lightnet::DemoRunner(animService, sceneStore, scenePlayer,
                                          scheduler, panels, initializer);
    demoRunner->seedScenes();
}

void runDemos()
{
    if (demoRunner) demoRunner->run();
}

#endif  // DEMO_ENABLED
#endif  // LIGHTNET_TARGET_CONTROLLER
