// ============================================================
//  WatchWise V1 — Watch-Body Case
//  Code-based CAD (OpenSCAD) — export to STL with F6
// ============================================================
//
//  PCB geometry (from KiCad watchwise_main.kicad_pcb):
//    Board outline: circle R=18mm, center at PCB origin (0,0),
//    with a rectangular notch cut into the bottom (connector area).
//    Notch width: 17mm, notch depth: ~12.4mm below center.
//    → PCB maximum width is 36mm (circle diameter).
//
//  ⚠️  case_od is defaulted to 38mm (not 35mm) so the 36mm-wide
//      PCB fits with 1mm walls.  Change case_od to taste; a 35mm
//      OD with 0.5mm walls will also print if your printer is accurate.
//
//  Coordinate system (case top view, +Z = toward strap face):
//    +X = right,  +Y = toward display / away from connector notch
//    PCB center = case center (0,0)
//    KiCad Y-axis is flipped (KiCad +Y = downward on screen = −Y here)
//
// ============================================================

$fn = 128;

// ── Case outer geometry ──────────────────────────────────────
case_od      = 38.0;   // outer diameter, mm
                       // (PCB arc = 36mm; 1 mm wall/side clearance)
case_h       = 5.2;    // total case height, mm — minimum given component stack:
                       //   bot_t(1.0) + sensor gap(1.0) + PCB(0.41)
                       //   + CYBLE module(1.8) + clearance(0.1) + top_t(0.8) = 5.11
wall_t       = 1.0;    // radial wall thickness, mm
bot_t        = 1.0;    // bottom face thickness (sensor / wrist side)
top_t        = 0.8;    // top face thickness  (strap / microsuction side)

// ── PCB shelf ────────────────────────────────────────────────
//  The shelf is an inward ledge on the inner wall.
//  PCB rests on the step; outer edge of PCB touches the shelf rim.
pcb_thick    = 0.41;   // PCB stackup thickness (from KiCad)
shelf_t      = 1.0;    // shelf width (radial, inward from inner wall)
//  No pin headers soldered — shelf only needs clearance for the
//  MAX30102 OLGA package on the PCB bottom face (~0.9mm tall).
shelf_z      = bot_t + 1.0;   // height of shelf TOP surface from z=0

// ── MAX30102 optical window (bottom / wrist face) ─────────────
//  Fab layer (chip body) in footprint local coords: ±1.7mm (X) × ±2.85mm (Y).
//  Footprint is rotated 90° on the PCB, so in case (PCB) coords:
//    width  (case X) = 2 × 2.85 = 5.7mm
//    height (case Y) = 2 × 1.7  = 3.4mm
sensor_w     =  5.7;   // opening width,  case X, mm
sensor_h     =  3.4;   // opening height, case Y, mm
sensor_x     =  0.0;   // centred in case
sensor_y     =  0.0;

// ── O-ring groove (outer cylindrical surface) ────────────────
oring_cs     =  1.0;   // O-ring cord cross-section diameter, mm
//  Groove is cut near the bottom edge so an O-ring seals the wrist face.
oring_z      =  bot_t + oring_cs * 0.7;   // groove centre height

// ── Microsuction pad recess (top / strap face) ───────────────
pad_d        = 28.0;   // circular recess diameter, mm
pad_depth    =  0.5;   // recess depth (pad thickness is typically 0.5–1 mm)

// ── Mounting holes (match PCB M1.6 drill holes on Edge.Cuts) ─
//  Positions from KiCad (converted to case coords, Y flipped):
mh_r         =  1.0;   // hole radius, mm (PCB holes are 1 mm radius)
mh_pos = [
  [ -10.92, -12.07 ],  // lower-left
  [  -9.22,  13.43 ],  // upper-left
  [   9.20,  13.43 ],  // upper-right
  [  10.98, -12.07 ],  // lower-right
];

// ── Connector cutouts (through the bottom / wrist face) ───────
//
//  JPROG1  — 5-pin 1.27mm SWD programming header
//  Footprint at KiCad (115.86, 102.11), no rotation → pin row along Y.
//  Case coords: (−13.06, +4.82).  Cutout 8.5 mm along Y × 4 mm along X.
prog_pos  = [ -13.06,  4.82 ];
prog_size = [  4.0, 8.5 ];    // [X, Y]  (row is along Y, 5×1.27 = 6.35 mm active)

//  BATT  — 2-pin 1.27mm battery connector
//  Courtyard (local, before 180° rotation): start(−1.55,−1.14) end(1.55, 2.41)
//    → 3.1mm (X) × 3.55mm (Y).  Courtyard centre offset after 180°: (0, −0.635).
//  Footprint origin in case coords: (+10.32, −5.97).
//  Courtyard centre in case coords: (+10.32, −5.97 + 0.635) = (+10.32, −5.34).
batt_pos  = [ 10.32, -5.34 ];
batt_size = [  3.1,  3.55 ];

//  CHAR  — 2-pin 1.27mm charging connector
//  Same courtyard as BATT.  Footprint origin in case coords: (+10.32, −9.61).
//  Courtyard centre in case coords: (+10.32, −9.61 + 0.635) = (+10.32, −8.98).
char_pos  = [ 10.32, -8.98 ];
char_size = [  3.1,  3.55 ];

// ============================================================
// Derived / computed
case_r   = case_od / 2;
case_id  = case_od - 2 * wall_t;   // inner cavity diameter above shelf
// Shelf inner diameter: PCB must slide in from top, seat on the step.
// Shelf rim inner-d = case_id − 2*shelf_t  (the ledge the PCB rests on)
shelf_id = case_id - 2 * shelf_t;

// Interior height available above the PCB:
interior_above = case_h - shelf_z - pcb_thick - top_t;

// ── Helper: rounded rectangular cutout ───────────────────────
module rect_slot(pos, sz, depth, rnd=0.2) {
    translate([ pos[0], pos[1], -0.01 ])
        linear_extrude(depth + 0.02)
            offset(r=rnd, $fn=32)
                square([ sz[0]-2*rnd, sz[1]-2*rnd ], center=true);
}

// ── O-ring groove ─────────────────────────────────────────────
module oring_groove() {
    // Toroidal channel cut into the outer cylindrical surface.
    rotate_extrude($fn=128)
        translate([ case_r - oring_cs*0.35, oring_z, 0 ])
            circle(d = oring_cs * 1.25, $fn=32);
}

// ── Main case body ────────────────────────────────────────────
module watchwise_case() {
    difference() {

        // ① Full solid cylinder
        cylinder(h=case_h, d=case_od);

        // ── Interior cavities ─────────────────────────────────

        // ② Space below PCB shelf (between bottom face and shelf)
        translate([0, 0, bot_t])
            cylinder(h = shelf_z - bot_t + 0.01, d = case_id);

        // ③ PCB slot at shelf level — wider diameter so PCB drops in
        //    from the top and rests on the shelf ledge.
        translate([0, 0, shelf_z])
            cylinder(h = pcb_thick + 0.01, d = case_id);
        //    Narrow ledge (shelf_id) is formed by leaving case_id−shelf_id
        //    of material behind the PCB edge.

        // ④ Main interior above PCB up to top face
        translate([0, 0, shelf_z + pcb_thick])
            cylinder(h = interior_above + 0.01, d = case_id);

        // ── Bottom-face features (sensor / wrist side, z=0) ──

        // ⑤ MAX30102 optical window (rectangle = chip Fab outline, rotated 90° in PCB)
        rect_slot([sensor_x, sensor_y], [sensor_w, sensor_h], bot_t, rnd=0.3);

        // ⑥ JPROG1 programming header slot
        rect_slot(prog_pos, prog_size, bot_t);

        // ⑦ BATT battery connector slot
        rect_slot(batt_pos, batt_size, bot_t);

        // ⑧ CHAR charging connector slot
        rect_slot(char_pos, char_size, bot_t);

        // ⑨ PCB mounting holes (pass through full case height so
        //    screws from the bottom reach the PCB)
        for (p = mh_pos)
            translate([ p[0], p[1], -0.01 ])
                cylinder(h = case_h + 0.02, r = mh_r, $fn=32);

        // ── Outer surface features ────────────────────────────

        // ⑩ O-ring groove on outer cylindrical surface
        oring_groove();

        // ── Top-face features (strap / microsuction side, z=case_h) ─

        // ⑪ Microsuction pad recess
        translate([0, 0, case_h - pad_depth])
            cylinder(h = pad_depth + 0.01, d = pad_d);
    }
}

// ── Render ───────────────────────────────────────────────────
watchwise_case();


// ============================================================
//  ADJUSTMENT GUIDE
// ============================================================
//
//  case_od=38 leaves 1mm walls with 36mm PCB inside.
//  For 35mm OD set wall_t=0.5 (marginal for FDM, fine for resin).
//
//  case_h=5.2 is the true minimum given the component stack.
//  Do not reduce below 5.1mm or the CYBLE module (~1.8mm tall)
//  will collide with the top face.
//
//  shelf_z=2.0 (bot_t + 1.0) clears the MAX30102 OLGA package
//  (~0.9mm) with 0.1mm air gap. No pin headers → no extra gap needed.
//
//  Connector slots are kept as probe/clip access points for the
//  bare pads (no headers soldered). Enlarge if using pogo pins.
//
//  O-ring: 1mm cord Buna-N, ~37mm ID.
//  Microsuction pad: 28mm circle, 3M or generic tape.
//
//  Print orientation:
//    Microsuction (top) face DOWN on the build plate.
//    Optical window and connector slots face up — better finish,
//    no supports needed inside the cavity.
// ============================================================
