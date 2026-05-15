# Chip Antenna Design Guide
## Johanson 2450AT18B100E + CY8C6347BZI-BLD53 (PSoC 63 BLE)

Reference document: `../CYBLE-416045-02_device/infineon-an91445-antenna-design-and-rf-layout-guidelines-applicationnotes-en.pdf`

---

## Part Summary

| Item | Detail |
|------|--------|
| Antenna | Johanson Technology 2450AT18B100E |
| Antenna size | 63 × 126 mils (1.6 × 3.2 mm) |
| Frequency | 2.4 GHz BLE (2.402–2.48 GHz) |
| Chip ANT pin | Ball **K1** (116-BGA package) |
| Chip RF output impedance | 50 Ω (integrated balun) |
| Target return loss | S11 ≤ −10 dB across 2.402–2.48 GHz (> 15 dB is good) |

---

## Step 1 — PCB Stackup Decision

Make this decision before layout. It affects RF trace width calculations.

**Recommended: 4-layer**
```
Top layer    — RF IC, all components, RF trace, antenna, decoupling caps
Layer 2      — Solid ground plane
Layer 3      — Power plane
Bottom layer — Non-RF components and signals
```

**Acceptable: 2-layer**
```
Top layer    — RF IC, all components, RF trace, antenna, power, signals
Bottom layer — Solid ground plane (uninterrupted below the entire radio section)
```

For a 2-layer board, keep the board as thin as possible. Thicker boards require wider RF traces, which makes routing harder and increases spurious modes.

---

## Step 2 — Antenna Placement

Place the antenna **before routing anything else**. Its location drives everything around it.

### Rules
- Place at the **edge or corner** of the PCB — the more open space around it, the better it radiates
- Orient so the antenna tip faces away from other components, the battery, and any cables
- The LiPo cell **must not be placed below the antenna** — the metallic foil detunes it severely
- The battery cable or any other cable **must not cross the antenna trace**
- Do not place the antenna close to plastic enclosure walls — plastic increases effective dielectric constant and shifts the resonant frequency down by 100–200 MHz
- No metal (screws, shielding cans, mounting hardware) allowed in the antenna near-field

### Keepout Zone
The 2450AT18B100E requires a copper-free keepout on **all layers** directly under and around the antenna body:

- **Minimum ground clearance:** 0.8 mm from antenna edge to any copper pour
- **Recommended ground clearance:** 2–3 mm — return loss improves measurably at this distance
- No traces, vias, pours, or copper of any kind inside this keepout on any layer

```
  ┌──────────────────────────────┐
  │  ← 2-3 mm clearance min →   │
  │   ┌──────────────────────┐  │
  │   │   2450AT18B100E      │  │  ← antenna body (1.6 × 3.2 mm)
  │   └──────────────────────┘  │
  │                              │
  └──────────────────────────────┘
     NO copper on ANY layer here
```

---

## Step 3 — RF Trace (ANT Pin K1 → Matching Network)

### Trace length
Keep the trace from the ANT pin (K1) to the matching network **as short as possible**.

- If trace length < 3 mm: width is not critical, use the same width as your standard signal traces
- If trace length ≥ 3 mm: **must be a controlled 50 Ω transmission line**

### 50 Ω trace width (if needed)
Use a coplanar waveguide with ground (CPWG). The width depends on your PCB stackup. For standard FR4 (εr = 4.3):

| Substrate thickness (mils) | Trace width W (mils) for 50 Ω CPWG |
|----------------------------|--------------------------------------|
| 20 | 33 |
| 30 | 44 |
| 40 | 52 |
| 50 | 59 |
| 60 | 65 |

Use a free online calculator to verify for your exact stackup (e.g., AppCAD or the Grounded CPW calculator linked in AN91445 Appendix B).

### RF trace layout rules — these matter
- Maintain **constant width** the entire length — any change in width changes impedance
- **No stubs or branches** off the RF trace — a branch acts as a reactive load and kills matching
- **No test points** on the RF trace — they act as stubs
- **No parallel traces** running next to the RF trace — causes mutual coupling
- Avoid 90° bends — use **curved or mitered bends** to maintain uniform width
- Do not route the RF trace to a different layer — keep it on the top layer
- Keep a **clean, uninterrupted ground plane** directly beneath the RF trace with no other traces crossing underneath

---

## Step 4 — Matching Network

### Why you need it
The chip's RF output is not exactly 50 Ω. A matching network is required between the ANT pin and the antenna to maximize power transfer. The 2450AT18B100E also cannot be tuned by length (unlike a PCB trace antenna), so the matching network is the only tuning mechanism.

### Starting component values
These values are derived from AN91445 Section 15.1.3 for a PSoC BLE chip + 2.4 GHz antenna system. Use as a starting point — actual values will need tuning on the real board:

```
ANT pin (K1)
    │
    ├─── shunt C to GND: 0.7 pF (C0G/NP0, 0402)
    │
   [series C: 1.2 pF (C0G/NP0, 0402)]
    │
   [2450AT18B100E]
```

This is a 2-component L-network (shunt capacitor + series capacitor). The shunt cap handles radio-side tuning; the series cap handles antenna-side tuning.

For a 3-component Pi or T network (better flexibility if first-spin values miss), populate all three footprints and start with the two-component values above — place 0 Ω in the unused series position.

> **Note:** Real component values differ from simulation because at 2.4 GHz, pad inductance, lead inductance, and the ground return path all add parasitic reactance. The values above already account for this. If you simulate with Smith V3.10 or similar, the theoretical output will be approximately shunt 0.5 pF + series 3.6 nH — but these will not work as-is on a real board.

### Component selection rules
- **Capacitors:** C0G/NP0 only — X5R/X7R capacitance drifts with temperature and DC bias, which shifts the match
- **Capacitor SRF:** must be well above 2.4 GHz (check datasheet — use 0402 or 0201 for lowest parasitics)
- **Inductors (if used):** high-Q only, SRF well above 2.4 GHz; wire-wound preferred over ceramic for values above ~3 nH
- Use only high-Q components — low-Q components can show a good S11 while actually wasting power as heat, masking a bad match

### Matching network layout rules
- Place shunt components directly on the RF trace with **a ground via immediately adjacent** — do not share vias
- Shunt components go on the RF trace pad itself, not on a branch — branches add stubs
- Place matching components as close to their respective side (chip or antenna) as possible
- The ground via for each shunt component must be **separate** — never share a via between two components

---

## Step 5 — Stitching Vias and Ground Plane

- Place a **ring of stitching vias** connecting the top ground pour to the bottom ground plane around the antenna keepout zone — spacing no more than λ/20 ≈ 6 mm at 2.4 GHz
- The bottom ground plane must be **solid and uninterrupted** below the antenna feed and matching network, but **absent** below the antenna body itself
- Fill all unused top-layer copper with ground pour and stitch it to the bottom plane with vias — this reduces EMI and gives the antenna a better return path
- Do not have split ground planes — current loops from splits add inductance and attenuate the RF

---

## Step 6 — Power Supply Decoupling

Switching noise from the power rails couples directly into the RF front end. Cypress specifies these values for the PSoC 63 BLE (CY8C6347BZI-BLD53):

| Pin | Cap value |
|-----|-----------|
| VRF | 3.3 µF |
| VDCDC | 2.2 µF |
| VBUCK1 | 4.7 µF |
| VDD_NS | 10 µF + 0.1 µF |
| All other VDD pins | 1 µF + 0.1 µF |

Layout rules:
- Place the **smallest value capacitor closest** to the supply pin
- Power supply current must flow **through** the decoupling cap to the pin — not around it
- Each cap gets its **own separate ground via** — never share vias between caps
- Do not place a power via between the cap and the pin

---

## Step 7 — Design for Testability

Add an RF test point branch so you can connect a VNA for tuning without reworking the board:

```
ANT pin ──── [0 Ω] ──────────────────── Matching Network ──── Antenna
                    │
                   [0 Ω DNP]
                    │
               U.FL or MMCX connector (DNP in production)
```

- The 0 Ω resistor in the connector branch is **do-not-populate (DNP)** in production
- When tuning: remove the 0 Ω to the antenna, solder coax to the exposed pad, connect shield to nearby ground
- The connector stub must be routed so that when the 0 Ω is removed, the remaining trace does **not** act as a stub on the antenna path

---

## Tuning Procedure (post-assembly)

Tuning is a two-pass process: bare PCB first, then with enclosure/body.

### Pass 1 — Bare PCB

**Equipment needed:** VNA (or equivalent), semi-rigid 50 Ω coax, 10 pF DC-block capacitor

1. Connect coax to the 50 Ω reference point (test connector or scraped trace point at the matching network)
2. Connect coax shield to exposed ground immediately adjacent — keep this connection as short as possible (3 dB variation in S11 depending on where the shield connects)
3. Place a 10 pF capacitor in series between the coax tip and the board (DC block)
4. Calibrate VNA: 2 GHz to 3 GHz sweep, set markers at 2.402 GHz, 2.440 GHz, 2.480 GHz
5. Perform one-port calibration (open, short, load)
6. Measure S11 — target: dip centered near 2.44 GHz with return loss > 10 dB across 2.402–2.48 GHz
7. Adjust matching network components (shunt cap value and series cap value) to bring the S11 dip to 2.44 GHz

**Target S11 on bare PCB:** tune the dip to slightly **above** 2.44 GHz (around 2.5 GHz) because the enclosure and body contact will shift it down 100–200 MHz.

### Pass 2 — With Enclosure and Body Contact

1. Install the PCB in its plastic enclosure with the coax still connected (drill a small hole if needed)
2. Measure S11 again — the dip will shift lower due to the plastic's dielectric effect
3. Place a hand on the device simulating normal wrist/wear use
4. Adjust matching network components so the dip lands at 2.44 GHz with the enclosure in place and the hand present
5. Verify: S11 ≤ −10 dB (return loss ≥ 10 dB) across 2.402–2.48 GHz. > 15 dB return loss is the target.

### Pass 3 — Radio Side (optional, if range is poor)

1. Disconnect the antenna-side matching components
2. Connect VNA to the 50 Ω reference point
3. Power the chip and put the BLE radio in continuous receive mode
4. Adjust the radio-side (chip-side) matching components so the VNA sees 50 Ω looking into the chip
5. Reconnect antenna side

---

## Checklist

Use this before submitting the board for fabrication.

### Schematic
- [ ] ANT pin (K1 on 116-BGA) connects to matching network, not directly to antenna
- [ ] Matching network has 3-component footprint populated (Pi or T) for tuning flexibility — start with 2 active + 1 × 0 Ω
- [ ] RF test connector branch included with DNP 0 Ω isolator
- [ ] All matching caps specified as C0G/NP0
- [ ] Power supply decoupling values match AN91445 Section 19 requirements for PSoC 63 BLE

### Layout — Antenna
- [ ] Antenna placed at PCB edge or corner
- [ ] Copper keepout zone on ALL layers under and around antenna body
- [ ] Ground clearance ≥ 0.8 mm from antenna edge (2–3 mm preferred)
- [ ] No components, vias, traces, or pours inside the keepout zone
- [ ] Antenna tip faces open space — not toward battery, cables, or enclosure walls
- [ ] Battery is NOT below the antenna

### Layout — RF Trace
- [ ] RF trace from ANT pin (K1) to matching network is < 3 mm, OR is a controlled 50 Ω CPWG
- [ ] RF trace width is constant throughout its entire length
- [ ] No stubs, branches, or test points on the RF trace
- [ ] No parallel traces running adjacent to the RF trace
- [ ] No 90° bends on RF trace (curved or mitered only)
- [ ] RF trace stays on one layer (no layer changes)
- [ ] Solid, uninterrupted ground plane beneath the RF trace on the layer below

### Layout — Matching Network
- [ ] Shunt components placed directly on the RF trace pad (not on a branch)
- [ ] Each shunt component has its own dedicated ground via, placed immediately adjacent
- [ ] Ground vias not shared between components
- [ ] Matching network placed as close to the ANT pin as possible

### Layout — Ground and Vias
- [ ] Stitching via ring around antenna keepout, ≤ 6 mm spacing
- [ ] No split ground planes
- [ ] Bottom ground plane solid and uninterrupted (except under antenna body)
- [ ] Unused top-layer copper filled with ground pour and stitched to bottom plane

### Layout — Power Decoupling
- [ ] Smallest decoupling cap closest to each power supply pin
- [ ] Power flows through the cap to the pin (not around it)
- [ ] Each decoupling cap has a separate ground via
- [ ] VRF: 3.3 µF; VDCDC: 2.2 µF; VBUCK1: 4.7 µF; VDD_NS: 10 µF + 0.1 µF; all others: 1 µF + 0.1 µF

---

## Reference Material

| Document | Location | Relevant Sections |
|----------|----------|-------------------|
| AN91445 — Antenna Design & RF Layout Guidelines | `../CYBLE-416045-02_device/infineon-an91445-...` | Sec 8 (chip antenna), Sec 12 (placement), Sec 14 (matching), Sec 15 (tuning), Appendix A (checklist) |
| CY8C6347BZI-BLD53 Datasheet | `../CYBLE-416045-02_device/CY8C6347BZI-BLD53_...` | Table 1 (pinout — ANT = K1), Sec 7 (BLE radio) |
| Johanson 2450AT18B100E Datasheet | Johanson Technology website | Layout guidelines, recommended matching values |
