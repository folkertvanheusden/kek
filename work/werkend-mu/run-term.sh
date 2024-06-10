#! /bin/sh

echo boot
echo 'rl(0,0)munix'

rm ~/temp/ramdisk/test2.log

RS=128

echo 'socat TCP-LISTEN:2334,reuseaddr pty,link=/tmp/virtualcom0,raw'

./build/kek -r work/werkend-mu/unix_v7m_rl0.dsk -r work/werkend-mu/unix_v7m_rl1.dsk -b -S $RS -L info,debug -l ~/temp/ramdisk/test2.log -P -1 /tmp/virtualcom0 -d  # -t
