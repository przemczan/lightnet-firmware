---
icon: material/chip
---

# Firmware

The Lightnet firmware lives in one source tree and compiles into **two binaries**: one for the ESP-class **controller** and one for the ATmega-class **panels**. Pick the section that matches what you're trying to do.

!!! tip "First-time builders, start at the hub"
    If you haven't flashed Lightnet before, work through the **[Get Started](../getting-started/index.md)** guide first — it sequences the hardware build, toolchain install, and first flash. The pages below are the deep reference you'll come back to after that.

<div class="grid cards" markdown>

-   :material-information-outline: **Overview**

    ---

    Architecture in one page — controller vs panel, discovery, animation system, external interfaces.

    [:material-arrow-right: Overview](overview.md)

-   :material-rocket-launch-outline: **Build & flash**

    ---

    PlatformIO environments, panel fuses, and the `pio` commands you need day-to-day.

    [:material-arrow-right: Build & Flash](getting-started.md)

-   :material-developer-board: **Hardware**

    ---

    Pin assignments for ESP8266 / ESP32 controllers and for ATmega panels; topology rules and fuses.

    [:material-arrow-right: Hardware](hardware.md)

-   :material-sitemap: **Architecture**

    ---

    Library structure, the internal I²C protocol, the animation framework, discovery, and controller boot.

    [:material-arrow-right: Architecture](architecture.md)

-   :material-api: **API Reference**

    ---

    External HTTP and WebSocket APIs — every endpoint, the binary packet format, and the CRC.

    [:material-arrow-right: API Reference](api.md)

-   :material-animation-play: **Animations & Scenes**

    ---

    Palettes, groups, scenes, layers, animation types, color references, JSON schema, worked examples.

    [:material-arrow-right: Animations & Scenes](animations/concepts.md)

-   :material-cloud-download-outline: **OTA & Updates**

    ---

    Panel updates via the twiboot bootloader; serial firmware upload; controller self-update.

    [:material-arrow-right: OTA & Updates](ota.md)

-   :material-bug-outline: **Troubleshooting**

    ---

    Debug macros, common user-facing symptoms, and how to inspect panel state over WebSocket.

    [:material-arrow-right: Troubleshooting](troubleshooting.md)

-   :material-flask-outline: **Testing**

    ---

    Native host-side unit tests, what's testable without a device, and how to add a new suite.

    [:material-arrow-right: Testing](testing.md)

</div>

---

[:fontawesome-brands-github: przemczan/lightnet-firmware](https://github.com/przemczan/lightnet-firmware){ .md-button }
