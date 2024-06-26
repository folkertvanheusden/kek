#! /bin/sh

echo boot
echo 'rl(0,0)unix'

rm ~/temp/ramdisk/test2.log

if [ "$1" = "fast" ] ; then
./build/kek -r work/werkend-mu/unix_v7m_rl0.dsk -r work/werkend-mu/unix_v7m_rl1.dsk -b -S 256 -L error,info -l ~/temp/ramdisk/test2.log -P -R rl02
elif [ "$1" = "medium" ] ; then
./build/kek -r work/werkend-mu/unix_v7m_rl0.dsk -r work/werkend-mu/unix_v7m_rl1.dsk -b -L error,info -l ~/temp/ramdisk/test2.log -d -S 256 -P -R rl02
else
./build/kek -r work/werkend-mu/unix_v7m_rl0.dsk -r work/werkend-mu/unix_v7m_rl1.dsk -b -L error,debug -l ~/temp/ramdisk/test2.log -d -t -X -S 256 -P -R rl02
fi
