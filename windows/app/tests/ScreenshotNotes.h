#pragma once
// Demo notes for ScreenshotTour.cpp (a header: moc stumbles over these raw strings).

namespace {

struct DemoNote {
    const char *path;
    const char *text;
};

const DemoNote demoNotes[] = {
    {"Reading list.wprint", R"WP(---
whiteprint: 1
title: Reading list
---
# Reading list

- [x] *Designing Data-Intensive Applications*, chapters 5–7
- [x] The Raft paper, sections 1–5
- [ ] *Computer Networking: A Top-Down Approach*, chapter 3
- [ ] Jepsen's write-up on PostgreSQL 12

> Read for structure first, then go back for the details.
)WP"},
    {"Projects/Home server.wprint", R"WP(---
whiteprint: 1
title: Home server
---
# Home server

```wp id=d1
flow Router>Pi>NAS
text "Backups every night at 02:00"
```

- [x] Static IP for the Pi
- [ ] Move photo backups off the laptop
)WP"},
    {"Courses/Databases/Normalization.wprint", R"WP(---
whiteprint: 1
title: Normalization
---
# Normalization

## Normal forms

1. **1NF**: atomic values, no repeating groups.
2. **2NF**: 1NF, and no attribute depends on part of a composite key.
3. **3NF**: 2NF, and no transitive dependencies on the key.

> A table is in BCNF when every determinant is a candidate key.
)WP"},
    {"Courses/Networks/Week 4 – Routing.wprint", R"WP(---
whiteprint: 1
title: Week 4 – Routing
---
# Week 4 – Routing

- **Link state** (OSPF): every router knows the whole map, runs Dijkstra.
- **Distance vector** (RIP): routers share tables with neighbours, Bellman-Ford.

```wp id=d1
row A B C
col D
arrow A>B "1"
arrow B>C "2"
arrow A>D "5"
arrow D>C "1"
```
)WP"},
    {"Courses/Networks/Lecture 3 – TCP.wprint", R"WP(---
whiteprint: 1
title: Lecture 3 – TCP
---
# Lecture 3 – TCP

Reliable, ordered byte streams on top of IP. Covers the handshake, flow control and congestion control.

## Three-way handshake

```wp id=d1
flow SYN>SYN_ACK>ACK
text "Client and server agree on initial sequence numbers before any data is sent"
```

## Flow vs. congestion control

- **Flow control** protects the *receiver*: the advertised window `rwnd`.
- **Congestion control** protects the *network*: `cwnd`, slow start, AIMD.
- Effective window = `min(rwnd, cwnd)`.

## To review

- [x] Re-watch the slow start animation
- [ ] Exercise 3.4: Reno vs. Tahoe after a timeout
- [ ] Ask about SACK in Thursday's tutorial

+++page

## Flashcards

```cards id=c1
# TCP basics
Q: What does TCP guarantee that IP does not?
A: Reliable, in-order delivery of a byte stream, with lost segments retransmitted.
ref: Lecture 3 – TCP.pdf · p. 4

Q: Name the three segments of the handshake.
A: SYN, SYN-ACK, ACK.
ref: Lecture 3 – TCP.pdf · p. 7

Q: Flow control or congestion control: which one protects the receiver?
A: Flow control. The receiver advertises its free buffer space as rwnd.
ref: Lecture 3 – TCP.pdf · p. 15

Q: What happens to cwnd after three duplicate ACKs (TCP Reno)?
A: Fast retransmit, then cwnd is halved (fast recovery) instead of dropping to 1 MSS.
ref: Lecture 3 – TCP.pdf · p. 19
```
)WP"},
    {"Courses/Networks/Study plan – Networks midterm.wprint", R"WP(---
whiteprint: 1
title: Networks midterm
---
# Networks midterm

## Overview · ≈ 5 h 30 min in total

Weeks 1–4 of Computer Networks: the layered model, TCP and routing. The midterm leans on TCP (about half the marks in past papers); routing algorithms come up as one long calculation question.

★ must know · ○ good to know · ✕ can skip

## Learning path

### 1. Layers and encapsulation · ≈ 45 min

- ★ **What each layer adds to a packet, and why** — *Lecture 1.pdf · p. 9*
- ○ Circuit vs. packet switching — *Lecture 1.pdf · p. 14*

✕ Can skip: History of ARPANET *(Lecture 1.pdf · p. 2)*

### 2. TCP · ≈ 2 h 30 min

- ★ **Three-way handshake and sequence numbers** — *Lecture 3 – TCP.pdf · p. 7*
- ★ **Flow control (rwnd) vs. congestion control (cwnd)** — *Lecture 3 – TCP.pdf · p. 15*
- ★ **Slow start, AIMD, fast retransmit** — *Lecture 3 – TCP.pdf · p. 18*
- ○ TCP header fields — *Lecture 3 – TCP.pdf · p. 5*

### 3. Routing · ≈ 2 h 15 min

- ★ **Dijkstra by hand on a 5-node graph** — *Week 4 – Routing.docx · p. 3*
- ★ **Link state vs. distance vector** — *Week 4 – Routing.docx · p. 1*
- ○ Count-to-infinity and poisoned reverse — *Week 4 – Routing.docx · p. 6*

+++page

## To-do

- [ ] Problem set 2 — due 2026-10-14 — *Syllabus.pdf · p. 3*
- [ ] Past paper 2025, questions 1–3 — due week 6
- [ ] Revise the Dijkstra worked example — *Week 4 – Routing.docx · p. 3*
)WP"},
    {"System design.wprint", R"WP(---
whiteprint: 1
title: System design
---
# Checkout service

How an order moves from the storefront to the warehouse. Draft for Thursday's design review.

## Request flow

```wp id=d1
flow Web>API>Orders>Postgres
db Postgres
box Payments 28,10 "Payments\n(Stripe)"
box Queue 42,10 "Order events" dashed
arrow Orders>Payments "charge"
arrow Orders>Queue
```

## Open questions

- [x] Pick a queue for order events: **SQS**, see ADR-012
- [x] Idempotency keys on `POST /orders`
- [ ] Retry policy when Payments times out
- [ ] Load test at 3× the Black Friday peak

> Keep the write path synchronous until we have measured it.

+++page

# Data model

```wp id=d1
row orders order_items products
db orders
arrow orders>order_items "1..n"
arrow order_items>products "n..1"
```

Prices are copied into `order_items` at checkout, so later price changes don't rewrite old orders.
)WP"},
};

const char *const drawingExample = R"WP(# Checkout service
flow Web>API>Orders>Postgres
db Postgres
box Payments 28,10 "Payments\n(Stripe)"
box Queue 42,10 "Order events" dashed
arrow Orders>Payments "charge"
arrow Orders>Queue)WP";

} // namespace
