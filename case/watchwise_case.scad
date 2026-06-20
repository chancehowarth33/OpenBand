// ============================================================
//  WatchWise V1 — Two-Piece Screw Case
// ============================================================
//
//  HARDWARE
//  ────────
//  4x  M1.6 × 4mm flat-head self-tapping screws  (no nuts, no drilling)
//  M1.6 shaft = 1.6mm fits through existing 2.0mm PCB corner holes
//  with 0.2mm clearance each side.
//  Search: "M1.6 4mm countersunk self tapping screws" on Amazon.
//
//  ASSEMBLY
//  ────────
//  1. Place top piece strap-face down on table
//  2. Drop PCB in (bosses align over the 4 corner holes)
//  3. Set bottom piece on PCB, wrist face up
//  4. Drive 4 screws from wrist face — through bottom piece →
//     through PCB holes → self-tap into bosses in top piece
//  5. Flip over. Done.
//
//  CROSS-SECTION (assembled):
//
//   ╔══════════════════════════════╗  ← outer strap face  (COMPLETELY SMOOTH)
//   ║              CYBLE          ║  top_face = 0.4mm
//   ║  ┌─boss─┐        ┌─boss─┐  ║  top_gap  = 1.9mm
//   ║  │▓pilot│        │▓pilot│  ║  (▓ = screw threads into here)
//   ║  │▓▓▓▓▓│        │▓▓▓▓▓│  ║
//   ╠══╪══↑══╪══PCB═══╪══↑══╪══╣  ← parting line
//   ║  │screw│  bot_gap│screw│  ║  bot_gap  = 0.6mm
//   ╚██╪═════╪█████████╪═════╪██╝  ← wrist face (countersunk screw heads)
//                                    bot_face = 1.0mm
//
//  TOTAL ASSEMBLED HEIGHT: 4.31mm
// ============================================================

$fn = 64;

// ── Dimensions ───────────────────────────────────────────────
case_od  = 38.0;
wall_t   = 1.0;
case_id  = case_od - 2 * wall_t;   // 36mm

pcb_t    = 0.41;
bot_face = 1.0;    // wrist face — 0.6mm head recess + 0.4mm floor
bot_gap  = 0.6;    // clearance below PCB for wrist-side components (0402 = ~0.5mm)
top_gap  = 1.9;    // CYBLE module clearance (~1.8mm tall)
top_face = 0.4;    // outer strap face — solid, completely smooth, no holes

bot_h = bot_face + bot_gap;   // 1.6mm
top_h = top_gap  + top_face;  // 2.3mm
// Assembled: 1.6 + 0.41 (PCB) + 2.3 = 4.31mm

// ── M1.6 × 4mm flat-head self-tapping (DIN 965) ─────────────
// Head: ⌀3.0mm, height 0.6mm — sits flush in countersink
// Shaft: 3.4mm — slides through PCB 2mm hole, threads into boss
//   0.4mm  remaining bot_face
//   0.6mm  bot_gap
//   0.41mm PCB
//   1.99mm into boss pilot  (total = 3.4mm ✓)
// Screw tip lands 0.31mm from outer strap face — safe
csk_top_d = 3.2;   // countersink top ⌀ (head 3.0mm + 0.2mm clearance)
csk_h     = 0.6;   // countersink depth = M1.6 head height
screw_d   = 1.9;   // clearance hole ⌀ for M1.6 shaft (1.6mm + 0.3mm)

// ── Screw boss (inside top piece, receives screw from below) ──
// Pilot opens at PCB end, blind at ceiling end.
// M1.6 self-tapping into PLA: 1.25mm pilot, min 0.75mm wall each side.
// boss_od = 2.8mm: far edge at 16.3+1.4=17.7mm < case_id/2=18mm ✓
boss_od  = 2.8;
boss_h   = top_gap;   // 1.9mm
pilot_d  = 1.25;      // M1.6 pilot for self-tapping into PLA
pilot_h  = 1.8;       // blind from PCB end (0.1mm solid at ceiling)

// ── PCB mounting hole positions ──────────────────────────────
// (KiCad coords − PCB centre (128.92, 106.93), Y-axis flipped)
mh_pos = [
  [-10.92, -12.07],
  [ -9.22,  13.43],
  [  9.20,  13.43],
  [ 10.98, -12.07],
];

// ── Wrist-face openings ──────────────────────────────────────
sensor_w  = 5.9;   sensor_ht = 3.6;
batt_pos  = [10.32, -5.34];  batt_sz = [3.1, 3.55];
char_pos  = [10.32, -8.98];  char_sz = [3.1, 3.55];
prog_pos  = [-13.06,  4.82]; prog_sz = [4.0, 8.5];

module rect_slot(pos, sz, depth, rnd=0.2) {
    translate([pos[0], pos[1], -0.01])
        linear_extrude(depth + 0.02)
            offset(r=rnd, $fn=32)
                square([sz[0]-2*rnd, sz[1]-2*rnd], center=true);
}

// ── BOTTOM PIECE (wrist side) ────────────────────────────────
// Sensor window + connector slots.
// 4 countersunk holes for M1.6 screw heads, flush with wrist face.
module bottom_piece() {
    difference() {
        cylinder(h=bot_h, d=case_od);
        translate([0, 0, bot_face])
            cylinder(h=bot_h, d=case_id);
        rect_slot([0,0], [sensor_w, sensor_ht], bot_face, rnd=0.15);
        rect_slot(batt_pos, batt_sz, bot_face);
        rect_slot(char_pos, char_sz, bot_face);
        rect_slot(prog_pos, prog_sz, bot_face);
        for (p = mh_pos)
            translate([p[0], p[1], 0]) {
                // Countersink taper — head sits flush with skin surface
                cylinder(h=csk_h, d1=csk_top_d, d2=screw_d, $fn=32);
                // Clearance hole below countersink
                translate([0, 0, csk_h])
                    cylinder(h=bot_face - csk_h + 0.01, d=screw_d, $fn=32);
            }
    }
}

// ── Screw boss ───────────────────────────────────────────────
// z=0 = ceiling end (solid 0.1mm), z=boss_h = open PCB end
module screw_boss() {
    difference() {
        cylinder(h=boss_h, d=boss_od, $fn=32);
        // Pilot from PCB end going up, blind at ceiling
        translate([0, 0, boss_h - pilot_h])
            cylinder(h=pilot_h + 0.01, d=pilot_d, $fn=32);
    }
}

// ── TOP PIECE (outer/strap side) ────────────────────────────
// Outer face is completely smooth — no holes, no features.
// 4 bosses hang from ceiling. Screw threads in from PCB side.
module top_piece() {
    union() {
        difference() {
            cylinder(h=top_h, d=case_od);
            translate([0, 0, top_face])
                cylinder(h=top_h, d=case_id);
        }
        // Bosses added after shell difference so they aren't cavity-subtracted
        for (p = mh_pos)
            translate([p[0], p[1], top_face])
                screw_boss();
    }
}

// ── Assembly preview — uncomment + F5 ────────────────────────
/*
color("SteelBlue", 0.9) bottom_piece();
translate([0, 0, bot_h + pcb_t])
    translate([0, 0, top_h]) rotate([180, 0, 0])
        color("Gainsboro", 0.9) top_piece();
*/

// ── Print layout — both open-end UP ─────────────────────────
bottom_piece();
translate([case_od + 5, 0, 0]) top_piece();

// ============================================================
//  EXPORT FOR BAMBU STUDIO
//  1. OpenSCAD → Render (F6)
//  2. File → Export as STL → "watchwise_bottom.stl"
//  3. Comment out bottom_piece(), un-comment top_piece() alone,
//     re-render → "watchwise_top.stl"
//  4. Drag both into Bambu Studio
//
//  TUNING
//  Screw won't start threading: decrease pilot_d by 0.1mm
//  Screw strips out easily:     increase pilot_d by 0.1mm
//  Head not flush with wrist:   increase csk_h by 0.05mm
// ============================================================
