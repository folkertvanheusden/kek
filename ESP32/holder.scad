$fa=1;$fs=0.1;
scale([10, 10, 10]) {
meter_plane_w = 7;
// ground plate
cube([10 + meter_plane_w,10,0.25]);
// side plane
x_meter = 11.5;
y_meter = 2+2;
w_meter = 4;
h_meter = 2.5;
holes_span = 5.4;
diam_screw = 0.3;
ethernet_w = 1.6;
ethernet_x = 10 + meter_plane_w / 2 - ethernet_w / 2;
difference() {
translate([0,9.95,0]) difference(){ cube([10 + meter_plane_w,0.25,10]);
    // hole for wires for rgb led panel
    translate([1,-0.1,7.01]) cube([2,0.7,1.2]);
    // gauge main
    translate([x_meter, -0.1, y_meter -0.25]) cube([w_meter+0.1,0.7,h_meter+0.5]);
};
    // gauge screw holes
    translate([x_meter + w_meter / 2 - holes_span / 2 + diam_screw /2, 11, y_meter + h_meter / 2 - diam_screw]) rotate([90,0,0]) cylinder(2,0.15+0.01,0.15+0.01);
    
    translate([x_meter + w_meter / 2 + holes_span / 2, 11, y_meter + h_meter / 2 - diam_screw]) rotate([90,0,0]) cylinder(2,0.15+0.01,0.15+0.01);

    // Ethernet
    color([1,0,0]) translate([ethernet_x, 9, 1.7 + 0.25 -1.6]) cube([1.6, 2, 1.3]);
}
// firmness
translate([9.5,9,0]) cube([0.25,1,3]);
translate([5.5,9,0]) cube([0.25,1,3]);

// ethernet holders
translate([ethernet_x - 0.1, 9, 0.25]) cube([0.1, 1, 1.0]);
translate([ethernet_x + ethernet_w, 9, 0.25]) cube([0.1, 1, 1.0]);
}