// RelayBootloader — the relay network's own OTA bootloader (hardware redesign plan §8 step 5),
// speaking the relay's own UART/PacketMeta framing for panels flashed over the point-to-point
// relay trunk. Lives entirely in the 4 KB boot section at BOOTLOADER_START (0x7000 on
// ATmega328P/PB) and is a completely separate compiled image from the application — nothing here
// links against LightnetPanel, EdgeUartTransport, or any other app-side class.
//
// UNVALIDATED HARDWARE — no bench spike has run. Builds clean and fits the boot section (see
// env:atmega328pb_bootloader), but nothing here has been exercised on real silicon.
//
// Entry: BootloaderBridge (Panel/BootloaderBridge.hpp) writes this panel's assigned index and
// parent edge to EEPROM, then software-jumps to word address 0x3800 (byte address
// BOOTLOADER_START) — see BootloaderProtocol.hpp for the exact EEPROM layout. That address is
// this image's own reset vector (`--section-start=.text=BOOTLOADER_START` places the whole
// vector table there, confirmed via avr-objdump — verify, don't assume, on every AVR link
// question in this codebase), so both a real hardware reset (BOOTRST fuse) and BootloaderBridge's
// software jump land the same way: through the standard init chain (.init1's SP/zero-reg setup,
// .init3's WDT disable below, then avr-libc's own __do_copy_data/__do_clear_bss) and into main().
// main() checks the EEPROM magic and falls straight through to the application unless the magic
// says otherwise. Every mutable global below is still assigned explicitly at the top of main()
// rather than trusting a C++ static initializer, on the belt-and-suspenders theory that this is
// cheap and avr-libc's init chain being reachable here shouldn't become a load-bearing assumption
// for correctness.
//
// Design (see the hardware redesign plan §8 step 5): only one panel is ever resident in its
// bootloader at a time, and every other panel keeps running its normal application — full
// PanelDiscovery/PanelRouter, already discovered and connected. So OTA traffic reaches this
// panel exactly like any other addressed setup packet already does: flooded downstream through
// unmodified intermediate app-mode panels, address-filtered here. This bootloader itself does
// no relaying at all — it only ever listens/replies on the one edge (its own parent edge)
// persisted at jump time, which is also why its own children are transiently unreachable for
// the duration of this panel's flash (a documented, accepted cost, not a bug).
//
// Single-edge, fully polled, no ISRs: request/response over one edge means nothing else
// legitimately arrives during the ~4.5 ms an SPM page write blocks for, so there is no need for
// EdgeFrameReceiver's multi-edge wake/claim machinery here — this reuses only Lightnet::
// PacketFramer (constructed with validateProtocolVersion=false, since flashing must survive an
// app-side protocol version mismatch) for wire framing/CRC, and reimplements a trimmed,
// single-fixed-edge send/receive pair rather than the mux-switching EdgeUartTransport.
//
// Guarded on LIGHTNET_BUILD_RELAY_BOOTLOADER (defined only by env:atmega328p(b)_bootloader
// in platformio.ini): this file lives under lib/Lightnet/Panel/bootloader/, so PlatformIO's
// Library Dependency Finder would otherwise also sweep it into the normal panel app/controller
// builds — the exact per-library trap the hardware redesign plan's §11.4 already hit once with
// LightnetPanel.cpp/Gamma.cpp. Compiling to an empty translation unit everywhere else avoids a
// second main() and a second, incompatible ISR/register setup fighting the real one.
#ifdef LIGHTNET_BUILD_RELAY_BOOTLOADER

#ifndef F_CPU
    #define F_CPU 16000000UL
#endif

#ifndef BOOTLOADER_START
    #define BOOTLOADER_START 0x7000
#endif

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <string.h>

#include "BootloaderProtocol.hpp"
#include "../../Core/Relay/PacketFramer.hpp"
#include "../../Common/Protocol.hpp"
#include "../../Utils/Crc.hpp"

namespace {
    const uint8_t BOOTLOADER_VERSION = 1;

    // Give up and boot the application if the controller never makes contact — otherwise a
    // panel whose controller crashed or was aborted mid-flash would stay resident forever.
    // Coarse (Timer0-overflow-tick based, ~16.4 ms/tick at /1024 prescale — see timerTick()),
    // not a real-time deadline.
    const uint16_t IDLE_TIMEOUT_TICKS = 900;  // ~15 s

    // Every global below is left at its all-zero .bss default and assigned for real (as executed
    // code, not a static initializer) at the top of main() — belt-and-suspenders against relying
    // on avr-libc's own __do_copy_data (confirmed present and reachable from this image's own
    // reset vector, see the file comment) actually running before main(), and consistent with
    // BootloaderBridge's own pre-jump SRAM sweep, which zeroes this exact range unconditionally.
    uint8_t parentEdge;
    uint16_t assignedIndex;

    // One SPM page, streamed in from BOOTLOADER_CHUNK_SIZE-sized wire chunks and committed once
    // a chunk completes it.
    uint8_t pageBuf[SPM_PAGESIZE];
    uint16_t pageStart;  // 0xFFFF (set in main()): no page loaded yet
    bool pageDirty;

    uint16_t idleTicks;  // ticks since the last byte of real activity

    // -------------------------------------------------------------------------------------
    // UART — single fixed edge, fully polled (no RXCIE0/ISR; see class comment above).
    // -------------------------------------------------------------------------------------

    void uartInit()
    {
        uint16_t ubrr = (uint16_t)((F_CPU / (16UL * BootloaderProtocol::UART_BAUD)) - 1);

        UBRR0H = (uint8_t)(ubrr >> 8);
        UBRR0L = (uint8_t)ubrr;
        UCSR0A = 0;
        UCSR0B = (1 << RXEN0) | (1 << TXEN0);  // no RXCIE0 -- this bootloader never enables interrupts
        UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  // 8N1
    }

    // Selects which edge the shared RX mux reads -- same bit-decode as EdgeUartTransport::
    // selectRxEdge(), called exactly once here (this bootloader never switches edges).
    void selectRxEdge(uint8_t edgeIndex)
    {
        DDRC |= (1 << PC3) | (1 << PC2);

        if (edgeIndex & 0x01) {
            PORTC |= (1 << PC3);
        } else {
            PORTC &= ~(1 << PC3);
        }

        if (edgeIndex & 0x02) {
            PORTC |= (1 << PC2);
        } else {
            PORTC &= ~(1 << PC2);
        }
    }

    // Permanently gates the shared TX line onto edgeIndex for this bootloader's entire resident
    // lifetime, unlike EdgeUartTransport's per-send gating. Safe because this is the only edge
    // this panel ever uses while resident (no other traffic origin exists to collide with), and
    // USART0 idles high (UART mark state) whenever nothing is queued to send -- electrically
    // identical to any other idle gap between EdgeUartTransport sends.
    //
    // PD2/PD3/PD4 (edges 0/1/2's tri-state buffer enables, docs/hardware/schematics/Panel.png)
    // are consecutive bit positions, so the enable bit is computed rather than looked up from a
    // const lookup table -- AVR's classic Harvard-architecture trap: a const array lives in SRAM
    // (.data) by default unless explicitly placed in PROGMEM, needing a flash-to-SRAM copy at
    // boot that this image's init chain should provide (see the file comment) but that a global
    // this small has no real reason to depend on.
    void enableTxEdge(uint8_t edgeIndex)
    {
        DDRD |= (1 << PD2) | (1 << PD3) | (1 << PD4);
        PORTD &= ~((1 << PD2) | (1 << PD3) | (1 << PD4));
        PORTD |= (uint8_t)(1 << (PD2 + edgeIndex));
    }

    void sendByte(uint8_t value)
    {
        while (!(UCSR0A & (1 << UDRE0))) {
        }

        UDR0 = value;
    }

    // Sends a preamble byte (see EdgeUartTransport::sendOnEdge's class comment -- absorbs the
    // receiving neighbour's own mux-settling/wake-to-claim latency) then the frame itself.
    void sendFrame(const Protocol::PacketMeta *packet, uint8_t size)
    {
        UCSR0A |= (1 << TXC0);  // clear any stale flag from a previous send before waiting on it
        sendByte(0xFF);

        const uint8_t *bytes = (const uint8_t *)packet;

        for (uint8_t i = 0; i < size; i++) {
            sendByte(bytes[i]);
        }

        while (!(UCSR0A & (1 << TXC0))) {
        }
    }

    bool tryReadByte(uint8_t *out)
    {
        if (!(UCSR0A & (1 << RXC0))) {
            return false;
        }

        *out = UDR0;

        return true;
    }

    void uartDisable()
    {
        UCSR0B = 0;
        PORTD &= ~((1 << PD2) | (1 << PD3) | (1 << PD4));
    }

    // -------------------------------------------------------------------------------------
    // Coarse elapsed-time tracking -- free-running Timer0 overflow, polled (no ISR), purely to
    // bound IDLE_TIMEOUT_TICKS. Not shared with the application's own PanelClock (a completely
    // separate binary) and torn down before jumping to it.
    // -------------------------------------------------------------------------------------

    void timerInit()
    {
        TCCR0B = (1 << CS02) | (1 << CS00);  // F_CPU / 1024
    }

    void timerDisable()
    {
        TCCR0B = 0;
    }

    // Call once per main-loop iteration; returns the number of TOV0 overflows since the last
    // call (0 or 1 in practice, since the loop body is short relative to ~16.4 ms/tick).
    uint8_t timerTick()
    {
        if (!(TIFR0 & (1 << TOV0))) {
            return 0;
        }

        TIFR0 = (1 << TOV0);

        return 1;
    }

    // -------------------------------------------------------------------------------------
    // Flash page buffering + SPM commit. No VIRTUAL_BOOT_SECTION-style vector-table rewriting
    // is needed here — ATmega328P/PB has a real hardware boot section (ASRE/RWWSRE).
    // -------------------------------------------------------------------------------------

    void commitPage()
    {
        if (!pageDirty || pageStart >= BOOTLOADER_START) {
            pageDirty = false;

            return;
        }

        boot_spm_busy_wait();
        boot_page_erase(pageStart);
        boot_spm_busy_wait();

        for (uint16_t offset = 0; offset < SPM_PAGESIZE; offset += 2) {
            uint16_t word = pageBuf[offset] | (pageBuf[offset + 1] << 8);

            boot_page_fill(pageStart + offset, word);
        }

        boot_page_write(pageStart);
        boot_spm_busy_wait();
        boot_rww_enable();

        pageDirty = false;
    }

    void loadPage(uint16_t newPageStart)
    {
        if (newPageStart == pageStart) {
            return;
        }

        commitPage();
        pageStart = newPageStart;
        memset(pageBuf, 0xFF, sizeof(pageBuf));
    }

    // -------------------------------------------------------------------------------------
    // OTA packet handlers
    // -------------------------------------------------------------------------------------

    void sendPong()
    {
        Protocol::PacketBootloaderPong pong =
            Protocol::makePacket<Protocol::PacketBootloaderPong>(Protocol::PACKET_BOOTLOADER_PONG, assignedIndex);

        pong.bootloaderVersion = BOOTLOADER_VERSION;
        pong.pageSize          = SPM_PAGESIZE;
        pong.flashSize         = BOOTLOADER_START;

        sendFrame(Protocol::packetMeta(pong), sizeof(pong));
    }

    void sendWriteAck(uint16_t address, Protocol::bootloaderWriteStatus_t status)
    {
        Protocol::PacketBootloaderWriteAck ack =
            Protocol::makePacket<Protocol::PacketBootloaderWriteAck>(Protocol::PACKET_BOOTLOADER_WRITE_ACK, assignedIndex);

        ack.address = address;
        ack.status  = (uint8_t)status;

        sendFrame(Protocol::packetMeta(ack), sizeof(ack));
    }

    void handleWriteChunk(const Protocol::PacketBootloaderWriteChunk *pkt)
    {
        bool lengthOk = (pkt->length >= 1) && (pkt->length <= Protocol::BOOTLOADER_CHUNK_SIZE);
        bool crcOk    = lengthOk
                        && (crc16(const_cast<uint8_t *>(pkt->data), pkt->length) == pkt->dataCrc);

        if (!crcOk) {
            sendWriteAck(pkt->address, Protocol::BOOTLOADER_WRITE_BAD_CRC);

            return;
        }

        bool addrOk = ((uint32_t)pkt->address + pkt->length) <= BOOTLOADER_START;

        if (!addrOk) {
            sendWriteAck(pkt->address, Protocol::BOOTLOADER_WRITE_BAD_ADDRESS);

            return;
        }

        uint16_t thisPageStart = pkt->address - (pkt->address % SPM_PAGESIZE);

        loadPage(thisPageStart);

        uint16_t offset = pkt->address - thisPageStart;

        memcpy(pageBuf + offset, pkt->data, pkt->length);
        pageDirty = true;

        if (offset + pkt->length == SPM_PAGESIZE) {
            commitPage();
        }

        sendWriteAck(pkt->address, Protocol::BOOTLOADER_WRITE_OK);
    }

    // -------------------------------------------------------------------------------------
    // Init/jump plumbing: naked .init1/.init3 sections and jump_to_app(). This panel's
    // ATmega328P/PB has a real hardware boot section (ASRE/RWWSRE), so no VIRTUAL_BOOT_SECTION
    // style vector-table rewriting is needed: jumping to 0x0000 lands on the application's own,
    // untouched reset vector.
    // -------------------------------------------------------------------------------------

    typedef void (*jump_to_app_t)(void) __attribute__((noreturn));

    jump_to_app_t jump_to_app = (jump_to_app_t)0x0000;
}  // namespace

void init1(void) __attribute__((naked, section(".init1")));
void init1(void)
{
    asm volatile ("clr __zero_reg__");
}

#if defined(__AVR_ATmega88__) || defined(__AVR_ATmega168__) \
    || defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__)
    void disable_wdt_timer(void) __attribute__((naked, section(".init3")));
    void disable_wdt_timer(void)
    {
        MCUSR  = 0;
        WDTCSR = (1 << WDCE) | (1 << WDE);
        WDTCSR = (0 << WDE);
    }

#endif

int main(void) __attribute__((OS_main, section(".init9")));
int main(void)
{
    cli();
    MCUSR = 0;
    wdt_disable();

    uint16_t magic = eeprom_read_word((const uint16_t *)BootloaderProtocol::EEPROM_MAGIC_ADDR);

    if (magic != BootloaderProtocol::ENTRY_MAGIC) {
        jump_to_app();
    }

    // Consume the magic immediately -- a stray reset while resident must not re-enter next boot.
    eeprom_write_word((uint16_t *)BootloaderProtocol::EEPROM_MAGIC_ADDR, 0);

    assignedIndex = eeprom_read_word((const uint16_t *)BootloaderProtocol::EEPROM_PANEL_INDEX_ADDR);
    parentEdge    = eeprom_read_byte((const uint8_t *)BootloaderProtocol::EEPROM_PARENT_EDGE_ADDR);

    if (parentEdge == BootloaderProtocol::NO_PARENT_EDGE) {
        jump_to_app();  // no valid edge persisted -- can't safely proceed
    }

    pageStart = 0xFFFF;  // sentinel: no page loaded yet
    pageDirty = false;
    idleTicks = 0;

    uartInit();
    selectRxEdge(parentEdge);
    enableTxEdge(parentEdge);
    timerInit();

    Lightnet::PacketFramer framer(/* validateProtocolVersion = */ false);

    while (true) {
        uint8_t elapsed = timerTick();

        if (elapsed) {
            idleTicks = (uint16_t)(idleTicks + elapsed);

            if (idleTicks >= IDLE_TIMEOUT_TICKS) {
                break;  // give up -- controller never made contact
            }
        }

        uint8_t value;

        if (!tryReadByte(&value)) {
            continue;
        }

        idleTicks = 0;

        if (!framer.pushByte(value)) {
            continue;
        }

        const Protocol::PacketMeta *frame  = framer.frame();
        uint16_t target = frame->header.targetPanelIndex;

        if (target != 0 && target != assignedIndex) {
            continue;  // real traffic, just not addressed to this panel
        }

        switch (frame->header.type) {
            case Protocol::PACKET_BOOTLOADER_PING:
                sendPong();
                break;

            case Protocol::PACKET_BOOTLOADER_WRITE_CHUNK:
                handleWriteChunk((const Protocol::PacketBootloaderWriteChunk *)frame);
                break;

            case Protocol::PACKET_BOOTLOADER_START_APP:
                commitPage();
                uartDisable();
                timerDisable();
                jump_to_app();
                break;

            default:
                break;  // ordinary app-mode traffic passing through -- not ours, ignore
        }
    }

    commitPage();
    uartDisable();
    timerDisable();
    jump_to_app();
}

#endif  // LIGHTNET_BUILD_RELAY_BOOTLOADER
