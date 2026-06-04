# WatchWise V1 — Watch-Body Case

OpenSCAD-based case for the WatchWise V1 PCB.  Open `watchwise_case.scad` in [OpenSCAD](https://openscad.org), press **F6** to render, then **File → Export → Export as STL**.

---

## Design Summary

| Property | Value |
|---|---|
| Outer diameter | 38mm *(see note below)* |
| Height | 5.2mm |
| Wall thickness | 1.0mm |
| Bottom face (wrist side) | 1.0mm solid |
| Top face (strap side) | 0.8mm solid |

### Faces

- **Bottom / wrist face** — faces the skin. Contains the MAX30102 optical window and connector access slots.
- **Top / strap face** — faces the wristband. Has the microsuction pad recess.

---

## Features

### MAX30102 Optical Window
- **⌀5.5mm** circular through-hole centred in the bottom face.
- Aligns with the MAX30102 PPG/SpO2 sensor, which sits at the PCB centre.

### Internal PCB Shelf
- Inward ledge on the inner wall; PCB drops in from the top and rests on the step.
- Shelf sits at **z = 2.0mm** from the bottom face (1.0mm face + 1.0mm sensor gap).
- The 1.0mm gap below the PCB clears the MAX30102 OLGA package (~0.9mm tall). No pin headers are soldered, so no additional clearance is needed.

### O-Ring Groove
- Toroidal channel cut into the **outer cylindrical surface** near the bottom edge.
- Sized for a **1mm cord Buna-N O-ring** (~37mm ID).
- Seals the wrist/sensor face when the case is pressed against a flat surface.

### Microsuction Pad Recess
- **⌀28mm × 0.5mm deep** circular depression on the top face.
- Accepts a 28mm circular microsuction pad (3M or generic tape).
- The microsuction face holds the module against a soft wristband.

### Connector Cutouts (bottom face)

Positions are derived directly from the KiCad PCB footprint coordinates, converted to case-centre coordinates (KiCad Y-axis flipped).

| Connector | Ref | Pins | Cutout size | Position (from centre) |
|---|---|---|---|---|
| SWD programming | JPROG1 | 5-pin 1.27mm | 4 × 8.5mm | (−13.1mm, +4.8mm) |
| Battery | BATT (J2BATT1) | 2-pin 1.27mm | 4 × 4mm | (+10.3mm, −6.0mm) |
| Charging | CHAR (J1CHAR1) | 2-pin 1.27mm | 4 × 4mm | (+10.3mm, −9.6mm) |

### Mounting Holes
- 4× **⌀2mm** holes passing through the full case height.
- Positions match the four corner drill holes on the PCB Edge.Cuts layer.
- Accepts M1.6 screws to secure PCB to case.

---

## PCB Geometry Note

The PCB outline (from `watchwise_main.kicad_pcb` Edge.Cuts) is a **circle of radius 18mm** (36mm diameter) with a rectangular notch cut into the bottom for the connector area.

> **Why 38mm and not 35mm?**
> The PCB arc reaches a maximum width of **36mm**. A 35mm OD case would leave negative wall thickness. The default `case_od = 38mm` gives 1mm walls with 0.5mm clearance per side — a safe tolerance for FDM printing.
>
> To hit 35mm OD: set `wall_t = 0.5` in the `.scad` file. This is marginal for FDM but works fine for resin printing.

---

## Adjusting the File

All dimensions are top-level parameters in `watchwise_case.scad`.

| Parameter | Default | What it controls |
|---|---|---|
| `case_od` | 38.0 | Outer diameter |
| `case_h` | 4.5 | Total height |
| `wall_t` | 1.0 | Radial wall thickness |
| `bot_t` | 1.2 | Bottom face thickness |
| `top_t` | 1.0 | Top face thickness |
| `shelf_z` | 2.0 | Height of PCB shelf from bottom |
| `sensor_d` | 5.5 | MAX30102 window diameter |
| `pad_d` | 28.0 | Microsuction recess diameter |
| `pad_depth` | 0.5 | Microsuction recess depth |
| `oring_cs` | 1.0 | O-ring cord diameter |

**Connector slot sizes** — add 0.5mm to `prog_size`, `batt_size`, `char_size` if your headers have a larger plastic housing than the bare footprint.

**`shelf_z`** — the 1.0mm sensor gap above `bot_t` is the minimum to clear the MAX30102 package. Only increase this if you add bottom-side components taller than 0.9mm.

---

## Print Orientation

Print with the **microsuction (top) face down** on the build plate.

- Optical window and connector slots face upward during the print → better surface finish, no support material needed inside the cavity.
- The o-ring groove will print cleanly without supports in this orientation.

---

## Bill of Materials (case hardware)

| Item | Spec | Source |
|---|---|---|
| O-ring | 1mm cord, ~37mm ID, Buna-N | McMaster-Carr / Amazon |
| Microsuction pad | ⌀28mm circle, ~0.5mm thick | 3M 5423 or generic |
| Screws | M1.6 × 4mm | McMaster-Carr / AliExpress |
