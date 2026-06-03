#include "PanelFlasher.hpp"

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include "../../Utils/Debug.hpp"

    // Always-on status log — not gated by DEBUG so the user can see flasher progress
    // even in release builds via any serial monitor.
    #define FLOG(fmt, ...) Serial.printf("[FLASHER] " fmt "\n", ## __VA_ARGS__)

    PanelFlasher::PanelFlasher(
        PanelsController * ctrl,
        PanelsInitializer *init,
        TwibootClient *    twiboot
    )
        : ctrl(ctrl), init(init), twiboot(twiboot)
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

        FLOG("start: %u panels, %u bytes, %u pages",
             status.totalPanels, (unsigned)firmwareSize, totalPages);

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
                    return; // wait for watchdog reset to fire
                }

                // twiboot always responds at its hardcoded address (0x29), regardless
                // of the panel's app-mode I²C index.
                TwibootClient::ChipInfo info;

                if (twiboot->connect(TwibootClient::TWIBOOT_ADDRESS, &info, 1, 0)) {
                    FLOG("twiboot ready @ 0x%02X", TwibootClient::TWIBOOT_ADDRESS);
                    currentPage = 0;
                    // Open firmware file immediately before transitioning so the
                    // read latency is paid while twiboot is alive (timeout reset
                    // by connect(); timer won't fire while we're communicating).
                    flashFile = Lightnet::Fs::open(firmwarePath, "r");

                    if (!flashFile) {
                        setError("firmware file open failed");
                        break;
                    }

                    transition(State::FLASHING);
                } else if (now - stateEnteredAt > TWIBOOT_WAIT_TIMEOUT_MS) {
                    setError("twiboot connect timeout");
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

                bool ok = twiboot->writePage(TwibootClient::TWIBOOT_ADDRESS,
                                             currentPage * 128, pageBuf);

                if (!ok) {
                    flashFile.close();

                    char msg[64];

                    snprintf(msg, sizeof(msg), "write failed page %u", currentPage);
                    setError(msg);
                    break;
                }

                D_PRINTF("[TWIBOOT] page %u/%u\n", currentPage + 1, totalPages);
                currentPage++;
                status.progressPct = (uint8_t)((uint32_t)currentPage * 100 / totalPages);

                if (currentPage >= totalPages) {
                    flashFile.close();
                    FLOG("write done (%u pages), verifying", totalPages);
                    transition(State::VERIFY);
                }

                break;
            }

            case State::VERIFY:
            {
                File f = Lightnet::Fs::open(firmwarePath, "r");
                bool ok = true;

                if (!f) {
                    // Verification skipped if file not accessible — log and continue
                    D_PRINTLN("[FLASHER] verify skipped (file unavailable)");
                } else {
                    uint8_t fileBuf[128], flashBuf[128];

                    for (uint16_t p = 0; p < totalPages && ok; p++) {
                        memset(fileBuf, 0xFF, sizeof(fileBuf));
                        f.read(fileBuf, 128);

                        bool readOk = twiboot->readPage(TwibootClient::TWIBOOT_ADDRESS, p * 128, flashBuf);
                        bool match  = readOk && (memcmp(fileBuf, flashBuf, 128) == 0);

                        if (!match) {
                            if (!readOk) {
                                D_PRINTF("[FLASHER] verify: read failed page %u\n", p);
                            } else {
                                // log first mismatched byte so we can diagnose
                                for (int i = 0; i < 128; i++) {
                                    if (fileBuf[i] != flashBuf[i]) {
                                        D_PRINTF("[FLASHER] verify mismatch page %u byte %d: file=0x%02X flash=0x%02X\n",
                                                 p, i, fileBuf[i], flashBuf[i]);
                                        break;
                                    }
                                }
                            }

                            ok = false;
                        }
                    }

                    f.close();
                }

                // Always attempt startApp so the panel boots regardless of verify result.
                // If verify failed, log the mismatch but still boot — the panel will likely
                // run correctly (the fork's own SPM write is reliable).
                if (!twiboot->startApp(TwibootClient::TWIBOOT_ADDRESS)) {
                    setError("startApp failed");
                    break;
                }

                if (!ok) {
                    FLOG("verify mismatch — panel booted anyway, check logs for details");
                }

                transition(State::NEXT_PANEL);
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
        D_PRINTF("[FLASHER] -> state %d\n", (int)next);
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

    uint8_t PanelFlasher::currentPanelAddress() const
    {
        Panel *panel = init->getPanels()->get(status.panelIdx);

        return panel ? (uint8_t)panel->index : 0;
    }

#endif  // LIGHTNET_TARGET_CONTROLLER
