#include "PanelFlasher.hpp"

#ifdef LIGHTNET_TARGET_CONTROLLER

#ifdef ARDUINO_ARCH_ESP32
    #include <SPIFFS.h>
#else
    #include <FS.h>
#endif
#include "../Utils/Debug.hpp"

PanelFlasher::PanelFlasher(PanelsController *ctrl, PanelsInitializer *init,
                             TwibootClient *twiboot)
    : ctrl(ctrl), init(init), twiboot(twiboot)
{
    memset(&status, 0, sizeof(status));
    status.state = State::IDLE;
}

void PanelFlasher::startFlashing(const char *path)
{
    File f = SPIFFS.open(path, "r");
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

    PRINTF("[FLASHER] start: %u panels, %u bytes, %u pages\n",
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

        case State::ENTER_BL: {
            uint8_t addr = currentPanelAddress();
            PRINTF("[FLASHER] ENTER_BL panel %d @ 0x%02X\n", status.panelIdx, addr);
            ctrl->enterBootloader(addr);
            transition(State::WAIT_BL);
            break;
        }

        case State::WAIT_BL: {
            if (now - stateEnteredAt < ENTER_BL_SETTLE_MS) {
                return;  // wait for watchdog reset to fire
            }

            uint8_t addr = currentPanelAddress();
            TwibootClient::ChipInfo info;

            if (twiboot->connect(addr, &info, 1, 0)) {
                PRINTF("[FLASHER] twiboot ready, pageSize=%u flashSize=%u\n",
                       info.pageSize, info.flashSize);
                currentPage = 0;
                transition(State::FLASHING);
            } else if (now - stateEnteredAt > TWIBOOT_WAIT_TIMEOUT_MS) {
                setError("twiboot connect timeout");
            }
            break;
        }

        case State::FLASHING: {
            uint8_t addr = currentPanelAddress();

            // Open firmware file and seek to current page
            File f = SPIFFS.open(firmwarePath, "r");
            if (!f) {
                setError("firmware file lost during flash");
                break;
            }

            uint8_t pageBuf[128];
            memset(pageBuf, 0xFF, sizeof(pageBuf));  // pad with 0xFF

            f.seek(currentPage * 128);
            size_t got = f.read(pageBuf, 128);
            f.close();

            if (got == 0 && currentPage < totalPages) {
                setError("firmware read error");
                break;
            }

            // programFlash() for a single page — pass a single-page slice
            bool ok = twiboot->programFlash(addr, pageBuf, 128, nullptr);
            if (!ok) {
                char msg[64];
                snprintf(msg, sizeof(msg), "write failed page %u", currentPage);
                setError(msg);
                break;
            }

            currentPage++;
            status.progressPct = (uint8_t)((uint32_t)currentPage * 100 / totalPages);

            if (currentPage >= totalPages) {
                transition(State::VERIFY);
            }
            break;
        }

        case State::VERIFY: {
            // Re-read firmware and verify — done page-by-page to avoid large RAM buffer.
            // For simplicity, re-open and re-stream the file.
            uint8_t addr = currentPanelAddress();
            File f = SPIFFS.open(firmwarePath, "r");
            bool ok = true;

            if (!f) {
                // Verification skipped if file not accessible — log and continue
                PRINTLN("[FLASHER] verify skipped (file unavailable)");
            } else {
                uint8_t fileBuf[128], flashBuf[128];
                for (uint16_t p = 0; p < totalPages && ok; p++) {
                    memset(fileBuf, 0xFF, sizeof(fileBuf));
                    f.read(fileBuf, 128);
                    ok = twiboot->verifyFlash(addr, fileBuf, 128);
                    yield();
                }
                f.close();
                if (!ok) {
                    setError("verify mismatch");
                    break;
                }
            }

            if (!twiboot->startApp(addr)) {
                setError("startApp failed");
                break;
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
    PRINTF("[FLASHER] → state %d\n", (int)next);
}

void PanelFlasher::setError(const char *msg)
{
    PRINTF("[FLASHER] ERROR: %s\n", msg);
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
        PRINTLN("[FLASHER] all panels flashed — restart controller to re-run discovery");
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
