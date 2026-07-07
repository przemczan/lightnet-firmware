#include "PanelFlasher.hpp"

#if defined(LIGHTNET_TARGET_CONTROLLER) && !defined(SIM_MODE)

    #include "../../Utils/Debug.hpp"

    // Always-on status log — not gated by DEBUG so the user can see flasher progress
    // even in release builds via any serial monitor.
    #define FLOG(fmt, ...) Serial.printf("[FLASHER] " fmt "\n", ## __VA_ARGS__)

    PanelFlasher::PanelFlasher(
        PanelsController *     ctrl,
        PanelsInitializer *    init,
        RelayBootloaderClient *relayBoot
    )
        : ctrl(ctrl), init(init), relayBoot(relayBoot)
    {
        memset(&status, 0, sizeof(status));
        status.state = State::IDLE;
    }

    void PanelFlasher::startFlashing(const char *path)
    {
        File f = Lightnet::Fs::open(path, "r");

        if (!f) {
            setError("cannot open firmware file");

            return;
        }

        firmwareSize = f.size();
        f.close();

        if (firmwareSize == 0 || firmwareSize > 28 * 1024) {
            setError("firmware size invalid");

            return;
        }

        strncpy(firmwarePath, path, sizeof(firmwarePath) - 1);
        firmwarePath[sizeof(firmwarePath) - 1] = '\0';

        totalPages = (uint16_t)((firmwareSize + 127) / 128);

        memset(&status, 0, sizeof(status));
        status.state       = State::IDLE;
        status.totalPanels = (uint8_t)init->getPanels()->getSize();

        if (status.totalPanels == 0) {
            setError("no panels discovered");

            return;
        }

        FLOG(
            "start: %u panels, %u bytes, %u pages",
            status.totalPanels,
            (unsigned)firmwareSize,
            totalPages
        );

        transition(State::ENTER_BL);
    }

    void PanelFlasher::run()
    {
        uint32_t now = millis();

        switch (status.state) {
            case State::IDLE:
            case State::DONE:
            case State::ERROR:
                return;

            case State::ENTER_BL:
            {
                uint8_t addr = currentPanelAddress();

                FLOG("ENTER_BL panel %d @ 0x%02X", status.panelIdx, addr);
                ctrl->enterBootloader(addr);
                transition(State::WAIT_BL);
                break;
            }

            case State::WAIT_BL:
            {
                if (now - stateEnteredAt < ENTER_BL_SETTLE_MS) {
                    return; // wait for the ENTER_BOOTLOADER packet to reach the panel
                }

                uint8_t addr = currentPanelAddress();

                if (relayBoot->connect(addr, 1, 0)) {
                    FLOG("bootloader ready @ panel %u", addr);
                    currentPage = 0;
                    flashFile = Lightnet::Fs::open(firmwarePath, "r");

                    if (!flashFile) {
                        setError("firmware file open failed");
                        break;
                    }

                    transition(State::FLASHING);
                } else if (now - stateEnteredAt > WAIT_TIMEOUT_MS) {
                    setError("bootloader connect timeout");
                }

                break;
            }

            case State::FLASHING:
            {
                uint8_t pageBuf[128];

                memset(pageBuf, 0xFF, sizeof(pageBuf));

                size_t got = flashFile.read(pageBuf, 128);

                if (got == 0 && currentPage < totalPages) {
                    flashFile.close();
                    setError("firmware read error");
                    break;
                }

                bool ok = relayBoot->writePage(currentPanelAddress(), currentPage * 128, pageBuf);

                if (!ok) {
                    flashFile.close();

                    char msg[64];

                    snprintf(msg, sizeof(msg), "write failed page %u", currentPage);
                    setError(msg);
                    break;
                }

                D_PRINTFLN("[FLASHER] page %u/%u", currentPage + 1, totalPages);
                currentPage++;
                status.progressPct = (uint8_t)((uint32_t)currentPage * 100 / totalPages);

                if (currentPage >= totalPages) {
                    flashFile.close();

                    // No read-back verify over the relay (see RelayBootloaderClient's class
                    // comment) — each page's own chunk CRCs are already the integrity check.
                    FLOG("write done (%u pages)", totalPages);
                    relayBoot->startApp(currentPanelAddress());
                    transition(State::NEXT_PANEL);
                }

                break;
            }

            case State::NEXT_PANEL:
                advancePanel();
                break;
        }
    }

    // ---------------------------------------------------------------------------
    // Private helpers
    // ---------------------------------------------------------------------------

    void PanelFlasher::transition(State next)
    {
        status.state    = next;
        stateEnteredAt  = millis();
        D_PRINTFLN("[FLASHER] -> state %d", (int)next);
    }

    void PanelFlasher::setError(const char *msg)
    {
        if (flashFile) flashFile.close();

        FLOG("ERROR: %s", msg);
        strncpy(status.errorMsg, msg, sizeof(status.errorMsg) - 1);
        status.errorMsg[sizeof(status.errorMsg) - 1] = '\0';
        status.hasError = true;
        status.state    = State::ERROR;
    }

    void PanelFlasher::advancePanel()
    {
        status.panelIdx++;
        status.progressPct = 0;

        if (status.panelIdx >= status.totalPanels) {
            FLOG("all panels flashed — restart controller to re-run discovery");
            status.state = State::DONE;

            return;
        }

        transition(State::ENTER_BL);
    }

    // getPanels() is ordered by discovery (pre-order DFS: a panel is always discovered before
    // any of its descendants), so this walks it back to front -- leaves first, root last. See
    // the class comment for why that order, not discovery order, is what keeps a mid-campaign
    // reboot from orphaning the panels still waiting to be flashed.
    uint8_t PanelFlasher::currentPanelAddress() const
    {
        uint16_t reverseIdx = status.totalPanels - 1 - status.panelIdx;
        Panel *panel = init->getPanels()->get(reverseIdx);

        return panel ? (uint8_t)panel->index : 0;
    }

#endif  // LIGHTNET_TARGET_CONTROLLER && !SIM_MODE
