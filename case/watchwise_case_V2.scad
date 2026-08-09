// ============================================================
//  WatchWise V1 — Two-Piece Screw Case with NATO Band Lugs
// ============================================================
//
//  DESIGN
//  ──────
//  Solid rectangular lugs are fused to the disc at 3 o'clock (+x)
//  and 9 o'clock (−x).  Each lug has TWO slots cut through it in
//  the y direction — one near the disc edge (inner) and one near
//  the lug tip (outer).  A 22mm band threads UP through the inner
//  slot, OVER the solid bar between the slots, and DOWN through
//  the outer slot — exactly like a NATO strap pass-through.
//
//  Top view:
//
//    ┌──────────┐               ┌──────────┐
//    │ ▭     ▭  │   disc body   │  ▭     ▭ │
//    │ in   out │◄═════════════►│ out   in │
//    └──────────┘               └──────────┘
//    9 o'clock (−x)             3 o'clock (+x)
//
//  Lug end view (looking from +x):  two open slots, solid bar
//  between them, slit_lip of material on the outer (z=0) face.
//
//  ASSEMBLY
//  ────────
//  1. Place top piece strap-face down
//  2. Drop PCB in, set bottom piece on top, drive 4× M1.6 screws
//  3. Thread band up through inner slot, over the bar, down through
//     outer slot at the 3 o'clock lug; repeat mirror at 9 o'clock;
//     wrap around wrist and buckle
// ============================================================

$fn = 64;

// ── Dimensions ───────────────────────────────────────────────
case_od  = 38.0;
wall_t   = 0.85;                   // thinned from 1.0 to widen interior, OD unchanged
case_id  = case_od - 2 * wall_t;   // 36.3mm — +0.3 for PCB clearance (0.15mm/side)

pcb_t    = 1.0;   // board body thickness (held in back pocket); components add 2mm on front
bot_face = 1.0;
bot_gap  = 1.2;   // back pocket depth — board body + 0402 clearance near MAX30
top_gap  = 2.0;   // front pocket depth — 2mm component clearance
top_face = 0.8;   // thickened from 0.4 to hide screw tips on smooth outer face

bot_h = bot_face + bot_gap;   // 2.2mm
top_h = top_gap  + top_face;  // 2.4mm   → external total 4.6mm

// ── M1.6 × 4mm flat-head self-tapping ───────────────────────
// Screw enters from wrist (bottom) face: clearance through bottom,
// self-taps into the top piece's bosses.
csk_top_d = 3.2;
csk_h     = 0.6;
screw_d   = 1.9;

// ── Screw boss ───────────────────────────────────────────────
boss_od  = 4.0;    // post outer dia → ~1.2mm wall around the 1.55mm pilot
boss_h   = 2.0;    // post height — flush with front rim (top_gap); board sits on top
pilot_d  = 1.55;   // self-tap pilot for M1.6 — slightly wider, easier to drive screw
pilot_h  = 1.5;    // pilot bore depth — leaves 0.5mm solid under bore in the 2mm post

// ── PCB mounting hole positions ──────────────────────────────
mh_pos = [
  [-10.92, -12.07],
  [ -9.22,  13.43],
  [  9.20,  13.43],
  [ 10.98, -12.07],
];

// ── Wrist-face openings ──────────────────────────────────────
sensor_w  = 6.2;   sensor_ht = 3.9;   // MAX30 window — shrunk a bit (was 6.6 × 4.2)
batt_pos  = [10.32, -5.34];  batt_sz = [3.1, 3.55];
char_pos  = [10.32, -8.98];  char_sz = [3.1, 3.55];
prog_pos  = [-13.06,  2.28]; prog_sz = [4.0, 8.5];

// ── LED status windows (TOP / strap face) ───────────────────
// 1.5mm round windows in the top piece, lined up with the P_BATT1
// (power) and P_CHAR1 (charge) LEDs on the PCB.
led_d        = 1.5;             // round hole diameter
led_batt_pos = [14.93, -7.64];  // P_BATT1 centroid in case coords
led_char_pos = [15.86,  2.79];  // P_CHAR1 centroid in case coords

// ── Battery shelf (inside top piece cavity) ──────────────────
// PCB Edge.Cuts has a chord at y=−15.87mm spanning x=±8.5mm (17mm wide).
// The battery sits in the crescent below that flat PCB edge.
// Shelf matches the chord so the PCB still drops in unobstructed.
shelf_y       = -15.60;  // top face of shelf — 34.60mm from top of outer circle
shelf_chord_w =  16.8;   // shelf width, centered at x=0
shelf_t       =   1.5;   // shelf depth in +y (into cavity)
shelf_h       =   1.0;   // shelf height in z

// ── Lug / band slot parameters ───────────────────────────────
band_slot_w = 23.0;  // slot width in y  (22mm band + 0.5mm each side)
tab_wall    = 1.5;   // solid lug wall each side of slot in y
tab_w       = band_slot_w + 2 * tab_wall;  // 26mm total lug width in y
tab_d       = 6.5;   // lug depth in x  (protrudes this far past disc edge)
slit_lip    = 0.9;   // solid material at wrist / strap face (z)
//
// NATO-style TWO-slot lug: band enters the inner slot, passes over the
// back of the disc, and exits the outer slot.  The solid bar between the
// two slots is what the strap wraps around (the "spring bar" equivalent).
//
slot_bar_x    = 2.4;  // x-length (opening) of each slot — band thickness clearance
slot_edge_off = 1.6;  // solid material between a slot and the disc edge / lug tip (was 0.9, was flimsy)
//
// Assembled slot height = (bot_h−slit_lip) + pcb_t + (top_h−slit_lip)
//                       = 0.7 + 0.41 + 1.4 = 2.51mm

module rect_slot(pos, sz, depth, rnd=0.2) {
    translate([pos[0], pos[1], -0.01])
        linear_extrude(depth + 0.02)
            offset(r=rnd, $fn=32)
                square([sz[0]-2*rnd, sz[1]-2*rnd], center=true);
}

// One lug at 3 o'clock (+x).  Use mirror([1,0,0]) for 9 o'clock.
// The lug is a flat slab that OVERLAPS into the disc so it fuses solidly
// to the rounded edge (no floating gap), then protrudes in +x.
// Two band slots cut all the way through the lug in y.
// slit_lip of solid remains at z=0 (outer face: wrist or strap).
tab_overlap = 4.0;                     // how far the slab reaches into the disc (x)
tab_x0      = case_od/2 - tab_overlap;  // inner start (inside disc)
tab_len     = tab_overlap + tab_d;      // total slab length in x

module tab_half(piece_h) {
    difference() {
        // Lug body — rounded outer corners, slab spans full tab_w in y
        translate([tab_x0, -tab_w/2, 0])
            linear_extrude(piece_h)
                offset(r=1.5, $fn=32) offset(r=-1.5)
                    square([tab_len, tab_w]);

        // INNER slot — full through-hole, all layers
        translate([case_od/2 + slot_edge_off, -band_slot_w/2, -0.01])
            cube([slot_bar_x, band_slot_w, piece_h + 0.02]);

        // OUTER slot — full through-hole, all layers
        translate([tab_x0 + tab_len - slot_edge_off - slot_bar_x,
                   -band_slot_w/2, -0.01])
            cube([slot_bar_x, band_slot_w, piece_h + 0.02]);
    }
}

// ── BOTTOM PIECE (wrist side) ────────────────────────────────
module bottom_piece() {
    difference() {
        union() {
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
                        cylinder(h=csk_h, d1=csk_top_d, d2=screw_d, $fn=32);
                        translate([0, 0, csk_h])
                            cylinder(h=bot_face - csk_h + 0.01, d=screw_d, $fn=32);
                    }
            }
            tab_half(bot_h);                  // 3 o'clock
            mirror([1,0,0]) tab_half(bot_h);  // 9 o'clock
        }
        // Re-cut interior to a PERFECT circle so lug overlap can't intrude
        // on the PCB cavity (lug slabs union past the inner wall otherwise).
        translate([0, 0, bot_face])
            cylinder(h=bot_h, d=case_id);
    }
}

// ── Screw boss ───────────────────────────────────────────────
module screw_boss() {
    difference() {
        cylinder(h=boss_h, d=boss_od, $fn=32);
        translate([0, 0, boss_h - pilot_h])
            cylinder(h=pilot_h + 0.01, d=pilot_d, $fn=32);
    }
}

// ── Battery shelf ────────────────────────────────────────────
// Curved ledge along the inner wall of the top piece at y=shelf_y.
// Battery bottom edge rests against it; intersection clips the box
// to the circular cavity profile automatically.
module battery_shelf() {
    intersection() {
        translate([0, 0, top_face])
            cylinder(h=shelf_h, d=case_id, $fn=64);
        translate([-shelf_chord_w/2, shelf_y - shelf_t, top_face])
            cube([shelf_chord_w, shelf_t, shelf_h]);
    }
}

// ── TOP PIECE (outer/strap side) ────────────────────────────
module top_piece() {
    union() {
        difference() {
            union() {
                difference() {
                    cylinder(h=top_h, d=case_od);
                    translate([0, 0, top_face])
                        cylinder(h=top_h, d=case_id);
                }
                tab_half(top_h);                  // 3 o'clock
                mirror([1,0,0]) tab_half(top_h);  // 9 o'clock
            }
            // Re-cut interior to a PERFECT circle (lug overlap removed)
            translate([0, 0, top_face])
                cylinder(h=top_h, d=case_id);
            // LED status windows through strap face, over P_BATT1 & P_CHAR1.
            // x is mirrored (-x) because the top piece flips over the PCB during
            // assembly, so a hole at -x in this frame lands over the +x LEDs.
            translate([-led_batt_pos[0], led_batt_pos[1], -0.01])
                cylinder(h=top_face + 0.02, d=led_d, $fn=24);
            translate([-led_char_pos[0], led_char_pos[1], -0.01])
                cylinder(h=top_face + 0.02, d=led_d, $fn=24);
        }
        // Bosses and shelf added AFTER the cavity re-cut so they remain solid
        for (p = mh_pos)
            translate([p[0], p[1], top_face])
                screw_boss();
        battery_shelf();
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
translate([case_od + tab_d*2 + 5, 0, 0]) top_piece();
