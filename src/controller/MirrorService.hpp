#pragma once
#ifdef LIGHTNET_TARGET_CONTROLLER

    // Flush mirrored animation packets to WebSocket clients, ~30fps gated. Defined in main.cpp.
    // Call it from blocking sections (e.g. the demos) so live preview keeps streaming while the
    // main loop is busy. A no-op until the mirror + WebSocket server exist.
    void serviceMirror();

#endif
