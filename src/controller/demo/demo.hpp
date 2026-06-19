#pragma once
#ifdef LIGHTNET_TARGET_CONTROLLER
    #if DEMO_MODE

        #include "DemoRunner.hpp"

        // Thin main-facing interface. The DemoRunner instance lives in demo.cpp.

        // Create the DemoRunner, seed demo scene files on the filesystem if missing.
        // Call once in case 0, after the filesystem.begin() and service construction.
        void initDemos(
            Lightnet::ScenesService&      animService,
            Lightnet::SceneStore&         sceneStore,
            Lightnet::ScenePlayer&        scenePlayer,
            Lightnet::AnimationScheduler& scheduler,
            PanelsController&             panels,
            PanelsInitializer&            initializer
        );

        // Run the next demo in the rotation. Blocks until it completes.
        // Call from the main loop (case 1).
        void runDemos();

    #endif  // DEMO_MODE
#endif  // LIGHTNET_TARGET_CONTROLLER
