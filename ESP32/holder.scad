$fa=1;$fs=0.1;
scale([10, 10, 10]) {
// ground plate
cube([17,10,0.25]);
// side plane
x_meter = 11.5;
y_meter = 2+2;
w_meter = 4;
h_meter = 2.5;
holes_span = 5.4;
diam_screw = 0.3;
difference() {
translate([0,9.95,0]) difference(){ cube([17,0.25,10]);
    translate([-0.01,-0.1,7.01]) cube([3,0.7,3]);
    // gauge
    translate([x_meter, -0.1, y_meter -0.25]) cube([w_meter+0.1,0.7,h_meter+0.5]);
};

    // gauge
    translate([x_meter + w_meter / 2 - holes_span / 2 + diam_screw /2, 11, y_meter + h_meter / 2 - diam_screw]) rotate([90,0,0]) cylinder(2,0.15+0.01,0.15+0.01);
    
    translate([x_meter + w_meter / 2 + holes_span / 2, 11, y_meter + h_meter / 2 - diam_screw]) rotate([90,0,0]) cylinder(2,0.15+0.01,0.15+0.01);

    // Ethernet
    color([1,0,0]) translate([10 + 7 / 2 - 1.6 / 2, 9, 0.3 + 1.7 + 0.25 -1.6]) cube([1.6, 2, 1.3]);
}
translate([9.5,9,0]) cube([0.25,1,3]);
translate([5.5,9,0]) cube([0.25,1,3]);
}