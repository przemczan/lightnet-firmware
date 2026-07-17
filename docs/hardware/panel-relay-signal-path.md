# Panel relay UART: TX gating, RX mux, and wake path

Shared USART0 (`TXD0`=PD1, `RXD0`=PD0) fans out to 3 edges through two independent, asymmetric
paths: TX is gated per-edge by tri-state buffers (`PEx`), RX is time-multiplexed by a
CD74HC4052 mux (`S0`/`S1`, called `RXSx` here). Wake sensing bypasses the mux entirely — each
edge has its own direct PCINT line. Source: `lib/Lightnet/Panel/EdgeUartTransport.cpp/.hpp`,
`lib/Lightnet/Panel/LightnetPanel.cpp`.

## 1. Physical signal topology

```mermaid
flowchart LR
    TXD0["TXD0 (PD1)\nshared USART TX"]
    RXD0["RXD0 (PD0)\nshared USART RX"]

    TXD0 --> PE0{"PE0 gate — PD2\nactive-low OE"}
    TXD0 --> PE1{"PE1 gate — PD3\nactive-low OE"}
    TXD0 --> PE2{"PE2 gate — PD4\nactive-low OE"}

    PE0 -->|"driven low only during\nsendOnEdge(0)"| W0["Edge 0 wire"]
    PE1 -->|"driven low only during\nsendOnEdge(1)"| W1["Edge 1 wire"]
    PE2 -->|"driven low only during\nsendOnEdge(2)"| W2["Edge 2 wire"]

    W0 --> MUX["CD74HC4052\nRXS0=PC3, RXS1=PC2"]
    W1 --> MUX
    W2 --> MUX
    MUX -->|"selectRxEdge(edge)\nroutes exactly one"| RXD0

    W0 -.->|"direct, bypasses mux"| PB1["PB1 PCINT — edge 0 wake"]
    W1 -.->|"direct, bypasses mux"| PB2["PB2 PCINT — edge 1 wake"]
    W2 -.->|"direct, bypasses mux"| PB3["PB3 PCINT — edge 2 wake"]

    classDef idle fill:#eee,stroke:#999
    class PE0,PE1,PE2 idle
```

Idle state: all three `PEx` gates deasserted (OE high → Hi-Z, nothing driving any edge wire);
`RXSx` sits parked on whichever edge was last claimed. Only one `PEx` is ever asserted at a
time, only for the duration of one `sendOnEdge()` call — this is the "single-active-flow"
invariant the whole design leans on.

## 2. TX → wake → mux-reselect → frame sequence

```mermaid
sequenceDiagram
    participant MainLoop as Main loop (tick)
    participant Tx as EdgeUartTransport::sendOnEdge()
    participant PEx as PEx gate (target edge)
    participant Wire as Edge wire
    participant PCINT as PCINTx ISR (wake)
    participant RxIsr as USART0_RX_vect ISR
    participant Mux as RXD0 mux (RXS0/RXS1)
    participant Ring as rxRing
    participant Recv as EdgeFrameReceiver

    MainLoop->>Tx: sendOnEdge(edge, packet)
    Tx->>Tx: transmitting = true
    Tx->>PEx: setEdgeEnable(edge, true) — OE low, Hi-Z -> active
    Tx->>Wire: shift out 2x 0xFF preamble + frame bytes

    Note over PCINT: coupled wake on ANOTHER edge's<br/>sense line — isTransmitting()==true -> discarded
    Note over RxIsr: any byte physically read back<br/>while transmitting — self-echo mask, discarded

    Tx->>Tx: wait TXC0 (last byte fully shifted)
    Tx->>PEx: setEdgeEnable(edge, false) — OE high, tri-state again
    Tx->>Tx: transmitting = false

    Note over Wire,PCINT: genuine reply arrives on the probed edge
    Wire->>PCINT: PCINTx transition
    PCINT->>PCINT: isTransmitting()? no -> latch edge bit into pendingWakeMask

    MainLoop->>MainLoop: pollWake() drains pendingWakeMask (cli/sei)
    MainLoop->>Recv: receiver.onEdgeWake(edge, now)
    Recv-->>MainLoop: accept / ignore (already mid-frame on another edge?)
    alt accepted
        MainLoop->>Mux: selectRxEdge(edge) — swing RXS0/RXS1
        Mux->>Mux: RXD0 now sourced from `edge`'s wire
    end

    Wire->>RxIsr: byte arrives on RXD0 (post-mux)
    RxIsr->>RxIsr: transmitting? no -> push byte to rxRing

    MainLoop->>Ring: pollBytes() drains ring, bounded by MAX_BYTES_PER_POLL
    Ring->>Recv: receiver.onByte(value, now)
    Recv-->>MainLoop: frame complete -> dispatcher.onFrameArrived(fromEdge, frame)
```

## Gating rules at a glance

| Signal | Asserted when | Purpose |
|---|---|---|
| `PEx` (PD2/3/4, active-low) | Only for the exact duration of `sendOnEdge(edge)` on that edge | Puts this panel's TX onto exactly one wire; every other edge stays Hi-Z |
| `transmitting` flag | Same window as the asserted `PEx` | Masks `onRxByte()` (self-echo) and `onEdgeWakeIsr()` (crosstalk-coupled phantom wake on a neighbouring sense line) for that whole window |
| `RXSx` (PC3/PC2, mux select) | Whatever `selectRxEdge()` last set | Chooses which edge's wire feeds the single shared `RXD0` — only that edge's incoming bytes are visible to the USART RX path at all |
| `pendingWakeMask` | Edge bits set by PCINT transitions not masked by `transmitting` | Per-edge latch, main-loop-only consumer (`pollWake()`), so mux reselection never races the ISRs |

`pollProbeClaim()` (`LightnetPanel.cpp`) re-parks `RXSx` on `driver.probingEdge()` every tick
while a probe is outstanding, independent of the wake path — a probe reply's edge is already
known from protocol rules, so it doesn't have to depend on a PCINT wake surviving crosstalk.
