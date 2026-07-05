# REQUIRES: sparc
# RUN: llvm-mc -filetype=obj -triple=sparcv9 %s -o %t.o
# RUN: ld.lld -shared %t.o -o %t.so
# RUN: llvm-readelf -r %t.so | FileCheck %s

# RUN: llvm-mc -filetype=obj -triple=sparcv9 --defsym ERR=1 %s -o %t.err.o
# RUN: not ld.lld -shared %t.err.o -o /dev/null 2>&1 | FileCheck --check-prefix=ERR %s

# CHECK:      R_SPARC_RELATIVE
# CHECK-NOT:  R_SPARC_UA64

# ERR: R_SPARC_UA64 relocation at offset {{[0-9]+}} against non-preemptible symbol local is not 8-byte aligned

.data
.p2align 3
aligned:
  .xword 0
  .reloc aligned, R_SPARC_UA64, local

.ifdef ERR
  .byte 0
unaligned:
  .xword 0
  .reloc unaligned, R_SPARC_UA64, local
.endif

.hidden local
local:
  .xword 0
