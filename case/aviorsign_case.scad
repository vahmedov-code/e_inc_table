// AviorSign case: ESP32-M075 V3.0 board, GDEM075F53 display, Vixion 357590 battery.
// Sizes measured by the owner on 2026-10-22; assumed values are marked.
//
// Coordinates: front view (as seen from the street through the glass), origin at the
// bottom-left corner of the board, x to the right, y up, z from the glass (0) into the room.
// The display FPC and the board's USB and button are on the bottom edge.
//
// Two parts, M2 self-tapping screws in the four corners:
//   part="front": frame + walls, printed glossy face down on the smooth plate, glued to the glass;
//   part="back":  cover with the battery bay and the button key, printed outer face down.

part = "assembly"; // front | back | assembly

// --- measured
board_w = 170; board_h = 130; board_t = 1.0;
disp_h = 112; disp_t = 1.4; disp_y = 9;          // flush with the board sides, 9 mm above and below
view_side = 2; view_top = 2; view_bottom = 10;   // glass edge to picture; the FPC side is wider
comp_h = 6.0;                                    // tallest part on the back (battery connector)
bat_w = 95; bat_h = 73; bat_t = 3.3;
// Parts on the back of the board, along the bottom edge, measured from the left seen from the street
// before the first test print. The test print showed USB and button swapped, so the whole
// back-of-board layout (USB, button, battery bay, posts) is mirrored with layout_flip.
layout_flip = true;
function fx(x) = layout_flip ? board_w - x : x;
usb_x = fx(103);                                 // micro-USB centre
btn_x = fx(67); btn_y = 5;                       // board button centre
// --- assumed: check before printing
tape_t = 0.2;        // double-sided tape between display and board
usb_up = 1.5;        // micro-USB centre above the board
btn_h = 5.0;         // button height above the board (the key works for 3.5 to 5 mm)
// --- case
front_t = 1.6; back_t = 1.6; wall = 4.0; gap = 0.4; gap_bottom = 1.0; clear = comp_h + 1;
pilot = 1.6; screw_hole = 2.4; pilot_in = 1.9;   // screw axes 1.9 mm diagonally outside the cavity corners

z_board = front_t + disp_t + tape_t;             // front face of the board
z_back = z_board + board_t;                      // back face of the board (components)
z_in = z_back + clear;                           // inside face of the cover
cav = [-gap, -gap_bottom, board_w + gap, board_h + gap];
out = [cav[0] - wall, cav[1] - wall, cav[2] + wall, cav[3] + wall];
screws = [[cav[0] - pilot_in, cav[1] - pilot_in], [cav[2] + pilot_in, cav[1] - pilot_in],
          [cav[0] - pilot_in, cav[3] + pilot_in], [cav[2] + pilot_in, cav[3] + pilot_in]];
win = [view_side, disp_y + view_bottom, board_w - view_side, disp_y + disp_h - view_top];

module box(x0, y0, x1, y1, z0, z1) translate([x0, y0, z0]) cube([x1 - x0, y1 - y0, z1 - z0]);
module rounded(r, z0, z1)
  translate([0, 0, z0]) linear_extrude(z1 - z0) offset(r) offset(-r)
    translate([out[0], out[1]]) square([out[2] - out[0], out[3] - out[1]]);

module front() {
  difference() {
    rounded(1.5, 0, z_in);
    box(cav[0], cav[1], cav[2], cav[3], front_t, z_in + 1);
    box(win[0], win[1], win[2], win[3], -1, front_t + 1);                    // display window
    for (s = screws) translate([s[0], s[1], z_in - 9]) cylinder(d = pilot, h = 10, $fn = 24);
    box(usb_x - 6, out[1] - 1, usb_x + 6, cav[1] + 1, z_back + usb_up - 3.75, z_back + usb_up + 3.75); // USB plug
  }
  // The board rests on these strips along its margins (no display there); the FPC passes in the middle below.
  box(3, disp_y + disp_h + 0.8, board_w - 3, board_h - 0.5, front_t - 0.01, z_board);
  for (x = [[3, 55], [115, board_w - 3]]) box(x[0], 0.5, x[1], disp_y - 0.8, front_t - 0.01, z_board);
}

module back() {
  lip_t = 1.2; lip_off = 0.3; lip_d = 2.0;
  // Flexible key over the button: free end 8 mm on one side of it, hinge 14 mm on the other.
  kf = fx(67 - 8); kh = fx(67 + 14);
  key = [min(kf, kh), 1, max(kf, kh), 9];
  difference() {
    union() {
      rounded(1.5, z_in, z_in + back_t);
      // Locating lip inside the walls, open along the bottom middle (FPC, button, USB).
      difference() {
        box(cav[0] + lip_off, cav[1] + lip_off, cav[2] - lip_off, cav[3] - lip_off, z_in - lip_d, z_in + 0.01);
        box(cav[0] + lip_off + lip_t, cav[1] + lip_off + lip_t, cav[2] - lip_off - lip_t, cav[3] - lip_off - lip_t, z_in - lip_d - 1, z_in + 1);
        box(45, cav[1] - 1, 125, cav[1] + 5, z_in - lip_d - 1, z_in + 1);
      }
      // Posts pressing the board onto the front strips, where the back of the board is empty.
      for (p = [[8, 40], [8, 100], [55, 75], [55, 110]]) translate([fx(p[0]), p[1], z_back + 0.1]) cylinder(d = 4, h = z_in - z_back, $fn = 24);
      // Battery bay: side walls and three ribs holding it down; open at the bottom for the wires.
      for (x = [67.4, 166.6]) box(fx(x) - 0.6, 62, fx(x) + 0.6, 128, z_back + 1, z_in + 0.01);
      for (x = [92, 117, 142]) box(fx(x) - 0.8, 64, fx(x) + 0.8, 124, z_back + bat_t + 0.3, z_in + 0.01);
      // Key post over the button.
      translate([btn_x, btn_y, z_back + btn_h + 0.3]) cylinder(d = 4, h = z_in - z_back - btn_h, $fn = 24);
    }
    for (s = screws) translate([s[0], s[1], z_in - 1]) cylinder(d = screw_hole, h = back_t + 2, $fn = 24);
    // U-shaped slot around the key.
    // U-shaped slot around the key, open on the hinge side.
    fl = kf < kh;   // free end on the left
    sx0 = fl ? key[0] - 0.8 : key[0]; sx1 = fl ? key[2] : key[2] + 0.8;
    for (b = [[sx0, key[1] - 0.8, sx1, key[1]], [sx0, key[3], sx1, key[3] + 0.8],
              fl ? [key[0] - 0.8, key[1] - 0.8, key[0], key[3] + 0.8] : [key[2], key[1] - 0.8, key[2] + 0.8, key[3] + 0.8]])
      box(b[0], b[1], b[2], b[3], z_in - 1, z_in + back_t + 1);
  }
}

module board_and_display() {
  color("royalblue") box(0, 0, board_w, board_h, z_board, z_back);
  color("whitesmoke") box(0, disp_y, board_w, disp_y + disp_h, front_t, front_t + disp_t);
  color("gold") box(min(fx(68.5), fx(68.5 + bat_w)), 57, max(fx(68.5), fx(68.5 + bat_w)), 57 + bat_h, z_back, z_back + bat_t);
  color("silver") box(usb_x - 4, -0.5, usb_x + 4, 5, z_back, z_back + 2.6);
  color("red") translate([btn_x, btn_y, z_back]) cylinder(d = 3.5, h = btn_h, $fn = 16);
}

if (part == "front") front();
else if (part == "back") translate([0, 0, z_in + back_t]) rotate([180, 0, 0]) back();   // outer face down
else { color("white", 0.9) front(); color("lightgray", 0.6) back(); board_and_display(); }
