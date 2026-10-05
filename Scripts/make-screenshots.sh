#!/bin/bash
# Regenerates the README screenshots in docs/images.
#
#   Scripts/make-screenshots.sh
#
# Builds the app, writes a demo notes folder to /tmp, and runs a copy of the
# app in screenshot mode (WHITEPRINT_SCREENSHOTS, see
# Sources/WhiteprintApp/Debug/ScreenshotMode.swift). The app captures only its
# own windows, so no Screen Recording permission is needed. Its windows come
# to the front for about half a minute; don't type meanwhile.
#
# The copy runs isolated from a Whiteprint you may have open: its own bundle
# id, socket, defaults suite and home folder (CFFIXED_USER_HOME), so your
# notes, preferences, Keychain and Claude config are never read or written.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=/tmp/wp-shots
NOTES=$WORK/notes
MATERIAL=$WORK/material
RAW=$WORK/raw
APP=$WORK/Whiteprint.app
OUT=$ROOT/docs/images

step() { echo "• $*"; }

note() { # note <relative path> ; body on stdin
    mkdir -p "$(dirname "$NOTES/$1")"
    cat > "$NOTES/$1"
}

write_notes() {
    step "Writing demo notes to $NOTES"
    rm -rf "$NOTES"
    mkdir -p "$NOTES"

    note "Reading list.wprint" <<'EOF'
---
whiteprint: 1
title: Reading list
---
# Reading list

- [x] *Designing Data-Intensive Applications*, chapters 5–7
- [x] The Raft paper, sections 1–5
- [ ] *Computer Networking: A Top-Down Approach*, chapter 3
- [ ] Jepsen's write-up on PostgreSQL 12

> Read for structure first, then go back for the details.
EOF

    note "Projects/Home server.wprint" <<'EOF'
---
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
EOF

    note "Courses/Databases/Normalization.wprint" <<'EOF'
---
whiteprint: 1
title: Normalization
---
# Normalization

## Normal forms

1. **1NF**: atomic values, no repeating groups.
2. **2NF**: 1NF, and no attribute depends on part of a composite key.
3. **3NF**: 2NF, and no transitive dependencies on the key.

> A table is in BCNF when every determinant is a candidate key.
EOF

    note "Courses/Networks/Week 4 – Routing.wprint" <<'EOF'
---
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
EOF

    note "Courses/Networks/Lecture 3 – TCP.wprint" <<'EOF'
---
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
EOF

    # In the format StudyPlanRenderer writes for build_study_plan.
    note "Courses/Networks/Study plan – Networks midterm.wprint" <<'EOF'
---
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
EOF

    note "System design.wprint" <<'EOF'
---
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
EOF

    # The startup note is the most recently modified one.
    touch -t 202610050900 "$NOTES"/*.wprint "$NOTES"/*/*.wprint "$NOTES"/*/*/*.wprint
    touch -t 202610051000 "$NOTES/System design.wprint"
}

write_material() {
    step "Writing course material to $MATERIAL"
    rm -rf "$MATERIAL"
    mkdir -p "$MATERIAL"
    local text=$WORK/lecture.txt
    cat > "$text" <<'EOF'
Lecture 3: TCP

TCP provides a reliable, in-order byte stream between two processes on top of
the unreliable IP layer. Lost segments are detected with acknowledgements and
retransmitted.

Connection setup uses a three-way handshake: SYN, SYN-ACK, ACK. Both sides pick
an initial sequence number.

Flow control: the receiver advertises its free buffer space (rwnd) so the
sender never overruns it.

Congestion control: the sender keeps a congestion window (cwnd). Slow start
doubles it every round trip; congestion avoidance grows it by one segment per
round trip (AIMD). Three duplicate ACKs trigger fast retransmit.
EOF
    cupsfilter -m application/pdf "$text" > "$MATERIAL/Lecture 3 – TCP.pdf" 2>/dev/null
    cat > "$WORK/routing.txt" <<'EOF'
Week 4: Routing

Link-state routing (OSPF): every router floods its links, builds the full
graph and runs Dijkstra's algorithm.

Distance-vector routing (RIP): routers exchange distance tables with their
neighbours and update them with Bellman-Ford. Slow convergence can cause
count-to-infinity; poisoned reverse helps.
EOF
    textutil -convert docx "$WORK/routing.txt" -output "$MATERIAL/Week 4 – Routing.docx"
}

# A copy of the app with its own bundle id, so it shares nothing (defaults,
# saved windows, Keychain items) with an installed or running Whiteprint.
make_app_copy() {
    step "Building Whiteprint.app"
    "$ROOT/Scripts/build.sh" app >/dev/null
    rm -rf "$APP"
    ditto "$ROOT/.build/Whiteprint.app" "$APP"
    plutil -replace CFBundleIdentifier -string io.github.l1203012.whiteprint.screenshots "$APP/Contents/Info.plist"
    codesign --force --sign - "$APP/Contents/MacOS/whiteprint-mcp" 2>/dev/null
    codesign --force --sign - "$APP" 2>/dev/null
}

run_tour() {
    step "Running the screenshot tour (windows will come to the front)"
    rm -rf "$RAW" "$WORK/home"
    mkdir -p "$RAW" "$WORK/home"
    rm -f /tmp/wp-shots.sock
    CFFIXED_USER_HOME=$WORK/home \
    WHITEPRINT_NOTES_DIR=$NOTES \
    WHITEPRINT_SOCKET=/tmp/wp-shots.sock \
    WHITEPRINT_DEFAULTS_SUITE=whiteprint-screenshots \
    WHITEPRINT_SCREENSHOTS=$RAW \
    WHITEPRINT_SCREENSHOTS_MATERIAL=$MATERIAL \
    WHITEPRINT_SCREENSHOTS_DMG_BACKGROUND=$ROOT/Resources/DMG/background@2x.png \
        perl -e 'alarm shift; exec @ARGV' 180 "$APP/Contents/MacOS/Whiteprint" -ApplePersistenceIgnoreState YES
}

# Shrinks PNGs to palette images: 224 colours for opaque pixels, and black at
# 32 alpha levels for shadows and rounded corners. Needs Python's Pillow.
quantize() {
    python3 - "$@" <<'PY'
import sys
from PIL import Image

COLORS, LEVELS = 224, 31
for path in sys.argv[1:]:
    im = Image.open(path).convert("RGBA")
    alpha = im.getchannel("A")
    q = im.convert("RGB").quantize(colors=COLORS, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG)
    palette = q.getpalette()[: COLORS * 3]
    palette += [0] * (COLORS * 3 - len(palette))
    index = Image.frombytes("L", im.size, q.tobytes())
    step = 255 / LEVELS
    shade = alpha.point(lambda v: COLORS + int(round(v / step)))
    opaque = alpha.point(lambda v: 255 if v == 255 else 0)
    out = Image.frombytes("P", im.size, Image.composite(index, shade, opaque).tobytes())
    out.putpalette(palette + [0, 0, 0] * (LEVELS + 1))
    out.save(path, optimize=True, transparency=bytes([255] * COLORS + [int(round(i * step)) for i in range(LEVELS + 1)]))
PY
}

# Downsizes to at most 1600 px wide, shrinks and copies into docs/images.
publish() {
    step "Optimising into $OUT"
    mkdir -p "$OUT"
    local file name width
    for file in "$RAW"/*.png; do
        name=$(basename "$file")
        width=$(sips -g pixelWidth "$file" | awk '/pixelWidth/ { print $2 }')
        cp "$file" "$OUT/$name"
        if [ "$width" -gt 1600 ]; then
            sips --resampleWidth 1600 "$OUT/$name" >/dev/null
        fi
        quantize "$OUT/$name" 2>/dev/null || echo "  $name: kept as is (python3 with Pillow makes it smaller)"
    done
    sips -s format png "$ROOT/Resources/App/AppIcon.icns" --resampleWidth 256 --out "$OUT/icon.png" >/dev/null
    ls -l "$OUT"
}

write_notes
write_material
make_app_copy
run_tour
publish
echo "✓ docs/images"
