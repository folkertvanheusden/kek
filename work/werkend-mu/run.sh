#! /bin/sh

echo boot
echo 'rl(0,0)unix'

LF=~/temp/ramdisk/test2.log
rm $LF

if [ "$1" = "fast" ] ; then
./build/kek -r work/werkend-mu/unix_v7m_rl0.dsk -r work/werkend-mu/unix_v7m_rl1.dsk -b -S 256 -L error,info -l $LF -P -R rl02 -n
elif [ "$1" = "medium" ] ; then
./build/kek -r work/werkend-mu/unix_v7m_rl0.dsk -r work/werkend-mu/unix_v7m_rl1.dsk -b -L error,info -l $LF -d -S 256 -P -R rl02
else
./build/kek -r work/werkend-mu/unix_v7m_rl0.dsk -r work/werkend-mu/unix_v7m_rl1.dsk -b -L error,debug -l $LF -d -t -X -S 256 -P -R rl02
fi
