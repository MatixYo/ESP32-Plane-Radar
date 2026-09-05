// ====================================================================
// Plane Radar & Aviator Chronometer — Desktop Console Case
// For 2.1" Round 360x360 Display (60mm PCB), ESP32-C3 Super Mini & EC11
// Parametric OpenSCAD Model
// ====================================================================

$fn = 72; // Smooth circular curves

// ====================================================================
// Configuration Parameters (in millimeters)
// ====================================================================

// --- Case Dimensions ---
case_w          = 74.0;   // Overall width
case_h          = 112.0;  // Overall height
case_base_d     = 58.0;   // Depth at table base
corner_r        = 4.0;    // Corner rounding radius
wall            = 2.4;    // Shell wall thickness
tilt_deg        = 18.0;   // Front face tilt angle back from vertical
// --- Display Cutout (GC9B72 2.1" 10-pin module) ---
disp_pcb_dia    = 59.24;  // Exact PCB outer diameter from drawing
disp_collar_od  = 60.4;   // Support collar OD (provides ~0.6mm clearance for 59.24mm PCB)
disp_glass_dia  = 55.52;  // Outer diameter of LCD glass (55.52mm ±0.1mm)
disp_view_dia   = 57.0;   // Aperture window opening (increased to 57.0mm for smooth fit of 56mm glass/LCM)
disp_active_dia = 52.92;  // LCD active display area (2.08" ~ 2.1" active screen)
disp_standoff_h = 2.0;    // Collar height / depth
disp_center_s   = 73.0;   // Center position along angled front face

tab_hole_pitch  = 23.88;  // Exact center-to-center hole pitch from drawing
tab_hole_x      = 11.94;  // tab_hole_pitch / 2 (±11.94mm from center)
tab_hole_y      = -29.8;  // Y position of 2-R1 holes at tab junction
tab_w           = 32.0;   // Tab cutout width (32.0mm for clean vertical wall transition)
tab_notch_w     = 24.5;   // Clearance notch width for 10-pin header (header is 22.86mm wide)


// --- Rotary Encoder (EC11 / KY-040) ---
enc_center_s    = 22.0;   // Center of knob along angled front face
enc_hole_dia    = 7.5;    // M7 threaded bushing hole diameter

// --- ESP32-C3 Super Mini (Internal Base Mount) ---
esp_w           = 18.5;   // Width
esp_l           = 23.0;   // Length
usbc_w          = 11.5;   // USB-C hole width
usbc_h          = 6.5;    // USB-C hole height

// --- Rear Cover Fasteners ---
screw_hole_dia  = 2.8;    // Holes for M2.5 or M3 screws
screw_post_dia  = 7.0;    // Outer diameter of corner screw bosses

// Display Mounting Style:
// "pins"   = Locating peg studs (displej se nasadí na kolíčky a zajistí kapkou tavného lepidla / zatavením)
// "screws" = Pilot holes for M2 self-tapping screws
disp_mount_type = "pins";
disp_pin_dia    = 1.85;   // Locating pin diameter (drawing specifies 2-R1 = Ø2.0mm holes)
disp_pin_h      = 2.8;    // Pin height above the 2.0mm standoff shelf



// ====================================================================
// Select Part to Render:
// "front"      = Main console enclosure (Ready to export/print face-down)
// "back"       = Rear cover plate with ventilation & USB-C cutout
// "test_bezel" = Quick 5-minute test swatch (only screen hole + collar + pins)
// "all"        = Both parts side-by-side for visual check
// ====================================================================
part = "front";

// ====================================================================
// Computed Geometry Helpers
// ====================================================================
// Exact geometric shift for the sphere centers to make front face tilt angle exactly tilt_deg
shift_y = (case_h - 2 * corner_r) * tan(tilt_deg);

// Module for positioning features perpendicular to the tilted front face
// In the children's frame:
//   X = horizontal across face (-X = left, +X = right)
//   Y = vertical along face (+Y = up toward top, -Y = down toward encoder/base)
//   +Z = normal pointing OUTWARDS towards user (Z = 0 is outer front surface)
//   -Z = normal pointing INWARDS into the cavity (Z <= -wall is inside cavity)
module on_front_face(s_pos) {
    // Exact tangent point on front surface at distance s_pos along slanted face
    face_y = corner_r * (1 - cos(tilt_deg)) + s_pos * sin(tilt_deg);
    face_z = corner_r * (1 + sin(tilt_deg)) + s_pos * cos(tilt_deg);
    translate([case_w / 2, face_y, face_z])
        rotate([90 - tilt_deg, 0, 0])
            children();
}
// 3D Solid Outer Shell with 18-degree tilted front and crisp vertical/flat rear rim
module outer_shell() {
    hull() {
        // Front 4 rounded corners (spheres for smooth 3D rounded front face & 18° tilt)
        translate([corner_r, corner_r, corner_r])
            sphere(r = corner_r);
        translate([case_w - corner_r, corner_r, corner_r])
            sphere(r = corner_r);
        translate([corner_r, shift_y + corner_r, case_h - corner_r])
            sphere(r = corner_r);
        translate([case_w - corner_r, shift_y + corner_r, case_h - corner_r])
            sphere(r = corner_r);

        // Rear vertical cylinders (keeps outer side corners rounded R=4mm, but ensures
        // the rear opening plane Y = case_base_d and top/bottom planes are 100% crisp and flat)
        translate([corner_r, case_base_d - corner_r, 0])
            cylinder(r = corner_r, h = case_h);
        translate([case_w - corner_r, case_base_d - corner_r, 0])
            cylinder(r = corner_r, h = case_h);
    }
}

// Inner Cavity subtracted from the rear
module inner_cavity() {
    hull() {
        // Front inner corners (matching the 18° tilt)
        translate([wall + corner_r/2, wall + corner_r/2, wall])
            cylinder(r = corner_r/2, h = 1);
        translate([case_w - wall - corner_r/2, wall + corner_r/2, wall])
            cylinder(r = corner_r/2, h = 1);
        translate([wall + corner_r/2, shift_y + wall + corner_r/2, case_h - wall])
            cylinder(r = corner_r/2, h = 1);
        translate([case_w - wall - corner_r/2, shift_y + wall + corner_r/2, case_h - wall])
            cylinder(r = corner_r/2, h = 1);

        // Rear inner opening (extends straight back through Y = case_base_d + 10)
        translate([wall + corner_r/2, case_base_d + 10, wall])
            cylinder(r = corner_r/2, h = 1);
        translate([case_w - wall - corner_r/2, case_base_d + 10, wall])
            cylinder(r = corner_r/2, h = 1);
        translate([wall + corner_r/2, case_base_d + 10, case_h - wall])
            cylinder(r = corner_r/2, h = 1);
        translate([case_w - wall - corner_r/2, case_base_d + 10, case_h - wall])
            cylinder(r = corner_r/2, h = 1);
    }
}

module front_enclosure() {
    col_size        = 8.0;   // Width & height of square corner column
    hole_depth      = 14.0;  // Screw hole depth from rear plate
    cover_t         = 1.6;   // Rear cover thickness
    col_rear_y      = case_base_d - cover_t - 0.4; // Recessed by cover_t + 0.4mm for flush seating

    difference() {
        // ============================================================
        // 1. ALL POSITIVE SOLIDS (Hollow shell + Columns + Collar + Rails)
        // ============================================================
        union() {
            // Main hollow shell
            difference() {
                outer_shell();
                inner_cavity();
            }

            // 4 Corner Columns for rear cover screws (solid pillars clipped within shell)
            intersection() {
                outer_shell();
                union() {
                    // Bottom-Left & Bottom-Right Corners
                    translate([0, 0, 0])
                        cube([wall + col_size, col_rear_y, wall + col_size]);
                    translate([case_w - wall - col_size, 0, 0])
                        cube([wall + col_size, col_rear_y, wall + col_size]);

                    // Top-Left & Top-Right Corners
                    translate([0, 0, case_h - wall - col_size])
                        cube([wall + col_size, col_rear_y, wall + col_size]);
                    translate([case_w - wall - col_size, 0, case_h - wall - col_size])
                        cube([wall + col_size, col_rear_y, wall + col_size]);
                }
            }

            // Internal Display Support Collar, Tabs & Mounting Bosses/Pins
            on_front_face(disp_center_s)
                display_internal_features();

            // Internal ESP32-C3 Mount Rails (Continuous from front wall)
            esp_x = case_w / 2 - esp_w / 2;
            esp_y = case_base_d - wall - esp_l - 2;
            esp_rail_h = 7.0;
            rails_y_start = wall;
            rails_y_len = case_base_d - wall - 2 - rails_y_start;

            translate([esp_x - 1.6, rails_y_start, wall]) {
                // Left guide rail
                cube([1.6, rails_y_len, esp_rail_h]);
                // Right guide rail
                translate([esp_w + 1.6, 0, 0])
                    cube([1.6, rails_y_len, esp_rail_h]);
                // Forward stop barrier
                translate([0, esp_y - rails_y_start, 0])
                    cube([esp_w + 3.2, 1.8, 3.5]);
            }
        }

        // ============================================================
        // 2. NEGATIVE CUTOUTS (Windows, chamfers, notches, holes)
        // ============================================================

        // Display Aperture Window, Chamfer, Notch & Pilot Holes
        on_front_face(disp_center_s)
            display_negative_cutouts();

        // Rotary Encoder Shaft Hole (Clean 7.5mm through-hole)
        on_front_face(enc_center_s)
            cylinder(d = enc_hole_dia, h = 40, center = true);

        // USB-C Port Cutout on rear bottom wall
        translate([case_w / 2 - usbc_w / 2, case_base_d - wall - 5, wall + 1.5])
            cube([usbc_w, wall + 10, usbc_h]);

        // 4 Rear Cover Screw Holes (Drilled into column shelves)
        hole_x_l = wall + col_size / 2;
        hole_x_r = case_w - wall - col_size / 2;
        hole_z_b = wall + col_size / 2;
        hole_z_t = case_h - wall - col_size / 2;

        for (hx = [hole_x_l, hole_x_r]) {
            for (hz = [hole_z_b, hole_z_t]) {
                translate([hx, case_base_d - cover_t - hole_depth, hz])
                    rotate([-90, 0, 0])
                        cylinder(d = screw_hole_dia, h = hole_depth + 1);
            }
        }
    }
}

// ====================================================================
// Shared Display Mount Modules (Identical in front enclosure & test swatch)
// ====================================================================
module display_internal_features() {
    // 1. Support collar following the circular PCB rim, ending cleanly at tab intersection
    difference() {
        translate([0, 0, -wall - disp_standoff_h])
            cylinder(d = disp_collar_od, h = disp_standoff_h + 0.6);
        // Rectangular cutout for the 32.0mm PCB tab and flex ribbon
        // Starts at Y = -24.5mm (exact clean vertical cut matching tab width 32mm)
        translate([-tab_w / 2, -60, -wall - disp_standoff_h - 1])
            cube([tab_w, 60 - 24.5, disp_standoff_h + 3]);
    }

    // 2. Snap-fit alignment guide tabs (2.8mm height above collar with 0.35mm retention bead)
    tab_wall_t  = 1.6;
    tab_h_above = 2.8; // Extends 2.8mm above collar (covers 1.6mm PCB + 1.2mm retention lip)
    for (a = [45, 90, 135, 215, 325]) {
        rotate([0, 0, a]) {
            // Main vertical tab wall
            translate([disp_collar_od / 2, -2.5, -wall - disp_standoff_h - tab_h_above])
                cube([tab_wall_t, 5.0, disp_standoff_h + tab_h_above + 0.6]);

            // Snap-fit retention bead (0.35mm inward ramp at top edge)
            translate([disp_collar_od / 2 - 0.35, -2.0, -wall - disp_standoff_h - tab_h_above])
                hull() {
                    cube([0.35, 4.0, 0.7]);
                    translate([0.35, 0, 0.7])
                        cube([0.01, 4.0, 0.4]);
                }
        }
    }

    // 3. Two dedicated support boss pads for bottom tab mounting holes (Pitch 23.88mm, tx = ±11.94mm, ty = -29.8mm)

    for (tx = [-tab_hole_x, tab_hole_x]) {
        translate([tx, tab_hole_y, -wall - disp_standoff_h])
            cylinder(d = 4.8, h = disp_standoff_h + 0.6);

        // Locating studs projecting through PCB holes (2-R1 = Ø2.0mm holes in PCB)
        if (disp_mount_type == "pins") {
            translate([tx, tab_hole_y, -wall - disp_standoff_h - disp_pin_h])
                cylinder(d = disp_pin_dia, h = disp_pin_h + 0.1);
        }
    }
}



module display_negative_cutouts() {
    // Display Aperture Window (Cuts through front wall and collar)
    cylinder(d = disp_view_dia, h = 40, center = true);

    // Display Bezel Chamfer
    translate([0, 0, 0.5])
        cylinder(d1 = disp_view_dia, d2 = disp_view_dia + 3.5, h = 3.0);

    // Optional Screw Pilot Holes (Only active if disp_mount_type == "screws")
    if (disp_mount_type == "screws") {
        for (tx = [-tab_hole_x, tab_hole_x]) {
            translate([tx, tab_hole_y, -wall - disp_standoff_h - 1])
                cylinder(d = 1.8, h = disp_standoff_h + 3.0);
        }
    }
}
// Quick Test Swatch (Flat 5-minute print for fit verification)
// ====================================================================
module test_bezel_swatch() {
    swatch_w = 66.0;
    swatch_h = 70.0;

    // Flat on XY print bed (front face at Z=0, collar & pins pointing straight up in +Z)
    rotate([180, 0, 0]) {
        difference() {
            union() {
                translate([-swatch_w / 2, -swatch_h / 2 - 2.0, -wall])
                    cube([swatch_w, swatch_h, wall]);

                display_internal_features();
            }
            display_negative_cutouts();
        }
    }
}

// ====================================================================
// Part 2: Rear Backplate Cover (Printable Flat on Bed)
// ====================================================================
module back_cover() {
    cover_t         = 1.6;   // 1.6mm thickness (sits flush within cavity without sticking out)
    cover_w         = case_w - 2 * wall - 0.4;
    cover_h         = case_h - 2 * wall - 0.4;
    cover_corner_r  = 1.0;   // Small 1.0mm corner radius to fully fill inner cavity corners (no gap)
    col_size        = 8.0;

    difference() {
        // Flat rectangular plate with small 1.0mm rounded corners
        hull() {
            translate([cover_corner_r, cover_corner_r, 0])
                cylinder(r = cover_corner_r, h = cover_t);
            translate([cover_w - cover_corner_r, cover_corner_r, 0])
                cylinder(r = cover_corner_r, h = cover_t);
            translate([cover_w - cover_corner_r, cover_h - cover_corner_r, 0])
                cylinder(r = cover_corner_r, h = cover_t);
            translate([cover_corner_r, cover_h - cover_corner_r, 0])
                cylinder(r = cover_corner_r, h = cover_t);
        }

        // 4 Corner Screw Holes (Direct through-holes for standard button/pan head screws)
        boss_inset_x = col_size / 2 - 0.2;
        boss_inset_y = col_size / 2 - 0.2;

        for (cx = [boss_inset_x, cover_w - boss_inset_x]) {
            for (cy = [boss_inset_y, cover_h - boss_inset_y]) {
                translate([cx, cy, -1])
                    cylinder(d = screw_hole_dia + 0.4, h = cover_t + 2);
            }
        }

        // Horizontal Chimney Ventilation Louvers (Convection cooling)
        for (i = [0:5]) {
            translate([12, 32 + i * 11, -1])
                cube([cover_w - 24, 2.5, cover_t + 2]);
        }

        // USB-C Cable Notch at bottom center
        translate([cover_w / 2 - usbc_w / 2, -1, -1])
            cube([usbc_w, 8, cover_t + 2]);
    }
}


// ====================================================================
// Render Output
// ====================================================================
if (part == "front") {
    front_enclosure();
} else if (part == "back") {
    back_cover();
} else if (part == "test_bezel") {
    test_bezel_swatch();
} else {
    // Both parts side-by-side for preview
    front_enclosure();
    translate([case_w + 15, 0, 0])
        back_cover();
}
