#ifdef LIGHTNET_TARGET_CONTROLLER
#include "../config.hpp"   // brings in DEMO_MODE before the guard
#if DEMO_MODE

    #include "demo.hpp"

    static Lightnet::DemoRunner *demoRunner = nullptr;

    void initDemos(
    Lightnet::AnimationService&   animService,
    Lightnet::ISceneRepository&   sceneStore,
    Lightnet::ScenePlayer&        scenePlayer,
    Lightnet::AnimationScheduler& scheduler,
    PanelsController&             panels,
    PanelsInitializer&            initializer
    )
    {
        demoRunner = new Lightnet::DemoRunner(animService, sceneStore, scenePlayer,
                                              scheduler, panels, initializer);
        demoRunner->seedScenes();
    }

    void runDemos()
    {
        if (demoRunner) demoRunner->run();
    }

#endif  // DEMO_MODE
#endif  // LIGHTNET_TARGET_CONTROLLER
