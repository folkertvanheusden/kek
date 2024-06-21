$fa=1;$fs=0.1;
scale([10, 10, 10]) {
// ground plate
cube([10,10,0.5]);
// RS232
translate([0.3,0.3,0]) cylinder(r=0.3, h=1.5);
translate([2.8,0.3,0]) cylinder(r=0.3, h=1.5);
translate([0.3,2.1,0]) cylinder(r=0.3, h=1.5);
translate([2.8,2.1,0]) cylinder(r=0.3, h=1.5);
// SD card reader
translate([10-0.5-0.15, 0.3,0]) cylinder(r=0.3, h=1.5);
translate([10-0.5-1.95, 0.3,0]) cylinder(r=0.3, h=1.5);
translate([10-0.5-0.15, 4.1,0]) cylinder(r=0.3, h=1.5);
translate([10-0.5-1.95, 4.1,0]) cylinder(r=0.3, h=1.5);
// lolin
translate([0.3, 10-0.3-1.5, 0]) cylinder(r=0.3, h=1.5);
translate([0.3, 8-0.3-1.5, 0]) cylinder(r=0.3, h=1.5);
// side plane
translate([0,9.95,0]) difference(){ cube([10,0.5,10]);
    translate([-0.01,-0.1,7.01]) cube([3,0.7,3]);
};
translate([9.5,7,0]) cube([0.5,3,3]);
translate([5.5,7,0]) cube([0.5,3,3]);
}