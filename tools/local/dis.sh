#!/bin/bash
# dis.sh IMAGE BASE START END : disassemble [START,END) of a raw big-endian PPC image loaded at BASE
img=$1; base=$(($2)); s=$(($3)); e=$(($4))
tmp=$(mktemp -d)
dd if="$img" of=$tmp/c.bin bs=4 skip=$(( (s - base)/4 )) count=$(( (e - s)/4 )) status=none
llvm-objcopy -I binary -O elf32-powerpc --rename-section .data=.text,code,alloc,load,contents $tmp/c.bin $tmp/c.elf
llvm-objdump -d --mcpu=pwr7 --adjust-vma=$s $tmp/c.elf | grep -E '^ *[0-9a-f]+:' | sed -E 's/<_binary[^>]*>//; s/^ +//'
rm -rf $tmp
