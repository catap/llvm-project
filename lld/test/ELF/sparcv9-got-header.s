# REQUIRES: sparc
# RUN: llvm-mc -filetype=obj -triple=sparcv9 %s -o %t.o
# RUN: ld.lld -shared %t.o -o %t.so
# RUN: llvm-readelf -S -x .got %t.so | FileCheck %s

# CHECK:      .got PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000008
# CHECK-LABEL: Hex dump of section '.got':
# CHECK-NEXT:  0x{{[0-9a-f]+}} {{[0-9a-f ]*[1-9a-f][0-9a-f ]*}}

.data
.xword _GLOBAL_OFFSET_TABLE_
